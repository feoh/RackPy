// SPDX-License-Identifier: GPL-3.0-or-later
// The laboratory Bench (RP-22A): an offline host for RackPy modules, stepped
// from Python on the main thread. It follows Rack 2.6.6's Engine through
// public API only (src/engine/Engine.cpp at the pinned commit): stepFrame's
// module order, process or processBypass, cable propagation, expander
// message flips and the frame counter; addModule, removeModule, addCable,
// removeCable, bypassModule, resetModule, randomizeModule, prepareSave,
// setSampleRate and setMasterModule with their events, and stepBlock's
// expander updates (setExpanderModule, which is PRIVATE, rewritten with
// public fields). It cannot reproduce engine worker threads, the audio driver
// thread, master-module timing, parameter smoothing, CPU meters or plug
// lights (docs/explanation/callbacks.md).
#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>
#include <vector>

#include "laboratory.hpp"
#include "../bindings/engine/module.hpp"
#include "../bindings/support/hooks.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/interpreter.hpp"

namespace rackpy {
namespace laboratory {

namespace {

using RackModule = rack::engine::Module;
using M = rack::engine::Module;

/** How deep this thread is inside a Bench operation that runs module code.
Rack's engine lock is not recursive, so Rack refuses (by deadlocking) what a
Bench refuses here: stepping or editing a bench from a module's callback. */
thread_local int busyDepth = 0;

struct Busy {
	Busy() { busyDepth++; }
	~Busy() { busyDepth--; }
	Busy(const Busy&) = delete;
	Busy& operator=(const Busy&) = delete;
};

void refuseInsideCallback(const char* what) {
	if (busyDepth > 0)
		throw runtime::Laboratory(std::string(what) + " inside a laboratory callback: Rack's engine lock is not "
			"recursive, so Rack's engine would deadlock here. Do it after the bench operation returns");
}

struct BenchModule {
	py::object handle;  // keeps the Python-owned module alive
	module::Module* module;
	bool bypassed = false;
};

struct BenchCable {
	int64_t id;
	module::Module* outputModule;
	int outputId;
	module::Module* inputModule;
	int inputId;
};

struct Bench;
std::vector<Bench*> benches;  // live benches, for finalization (main thread)

struct Bench {
	float sampleRate;
	float sampleTime;
	int64_t frame = 0;
	int64_t block = 0;
	int64_t nextCableId = 0;
	std::vector<BenchModule> modules;
	std::vector<BenchCable> cables;  // sorted by input, as Rack's
	module::Module* master = nullptr;

	explicit Bench(float rate) : sampleRate(rate), sampleTime(1.f / rate) { benches.push_back(this); }
	~Bench() {
		drop();
		benches.erase(std::remove(benches.begin(), benches.end(), this), benches.end());
	}
	Bench(const Bench&) = delete;
	Bench& operator=(const Bench&) = delete;

	/** Forget every module and cable without events (garbage collection, finalization). */
	void drop() noexcept {
		master = nullptr;
		for (auto& bm : modules)
			bm.module->onBench = false;
		cables.clear();
		std::vector<BenchModule> doomed;
		doomed.swap(modules);
	}

	BenchModule* find(const RackModule* m) {
		for (auto& bm : modules)
			if (bm.module == m)
				return &bm;
		return nullptr;
	}
	BenchModule& entry(py::handle value) {
		runtime::requireMainThread("using a laboratory Bench");
		if (!py::isinstance<bindings::ModuleRef>(value))
			throw py::type_error("a laboratory Bench takes rackpy.raw.engine.Module handles");
		BenchModule* bm = find(&value.cast<const bindings::ModuleRef&>().get());
		if (!bm)
			throw runtime::Ownership("the module is not on this bench");
		return *bm;
	}

	// --- Rack's Engine::addModule and removeModule -----------------------------
	void add(py::object value) {
		requireEnabled("adding a module to a laboratory Bench");
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("adding a module to a bench");
		if (!py::isinstance<bindings::ModuleRef>(value))
			throw py::type_error("a laboratory Bench takes rackpy.raw.engine.Module handles");
		const auto& ref = value.cast<const bindings::ModuleRef&>();
		module::Module& m = ref.get();
		if (ref.state->constructing)
			throw runtime::Ownership("a module can go on a bench only after its constructor callback returns");
		if (m.engineOwned.load(std::memory_order_acquire))
			throw runtime::Ownership("Rack's engine has this module; a bench takes only modules Python owns");
		if (m.onBench)
			throw runtime::Ownership("the module is already on a laboratory Bench");
		// Rack assigns a random ID if the module has none or a taken one; a bench, the lowest free one.
		auto taken = [&](int64_t id) {
			return std::any_of(modules.begin(), modules.end(), [&](const BenchModule& bm) { return bm.module->id == id; });
		};
		if (m.id < 0 || taken(m.id)) {
			int64_t id = 0;
			while (taken(id))
				id++;
			m.id = id;
		}
		modules.push_back(BenchModule{value, &m});
		m.onBench = true;
		Busy busy;
		bindings::ErrorCapture capture;
		M::AddEvent eAdd;
		m.onAdd(eAdd);
		M::SampleRateChangeEvent eSrc;
		eSrc.sampleRate = sampleRate;
		eSrc.sampleTime = sampleTime;
		m.onSampleRateChange(eSrc);
		capture.rethrow();
	}

	void remove(py::object value) {
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("removing a module from a bench");
		BenchModule& bm = entry(value);
		module::Module& m = *bm.module;
		for (const auto& c : cables)
			if (c.inputModule == &m || c.outputModule == &m)
				throw runtime::Ownership("cables are still connected to this module (Rack removes them first)");
		bindings::ErrorCapture capture;
		{
			Busy busy;
			M::RemoveEvent eRemove;
			m.onRemove(eRemove);
			if (master == &m)
				setMasterNoLock(nullptr);
			// Rack's removeModule: the other modules' expanders (the module itself included), then its own.
			for (auto& other : modules) {
				for (uint8_t side = 0; side < 2; side++) {
					RackModule::Expander& expander = other.module->getExpander(!side);
					if (expander.moduleId == m.id)
						expander.moduleId = -1;
					if (expander.module == &m)
						setExpanderModule(*other.module, nullptr, !side);
				}
			}
			for (uint8_t side = 0; side < 2; side++) {
				m.getExpander(side).moduleId = -1;
				setExpanderModule(m, nullptr, side);
			}
		}
		auto it = std::find_if(modules.begin(), modules.end(), [&](const BenchModule& b) { return b.module == &m; });
		py::object handle = std::move(it->handle);
		modules.erase(it);
		m.onBench = false;
		capture.rethrow();
	}

	// --- Rack's Engine::addCable and removeCable ---------------------------------
	int64_t connect(py::object output, int outputId, py::object input, int inputId) {
		requireEnabled("connecting modules on a laboratory Bench");
		refuseInsideCallback("connecting modules on a bench");
		module::Module& out = *entry(output).module;
		module::Module& in = *entry(input).module;
		module::Construction::index(outputId, out.outputs.size());
		module::Construction::index(inputId, in.inputs.size());
		bool inputWasConnected = false;
		bool outputWasConnected = false;
		for (const auto& c : cables) {
			if (c.inputModule == &in && c.inputId == inputId)
				inputWasConnected = true;
			if (c.outputModule == &out && c.outputId == outputId)
				outputWasConnected = true;
		}
		int64_t id = nextCableId++;
		cables.push_back(BenchCable{id, &out, outputId, &in, inputId});
		std::sort(cables.begin(), cables.end(), [](const BenchCable& a, const BenchCable& b) {
			return std::make_tuple(a.inputModule, a.inputId) < std::make_tuple(b.inputModule, b.inputId);
		});
		if (!inputWasConnected)
			in.inputs[inputId].channels = 1;
		if (!outputWasConnected)
			out.outputs[outputId].channels = 1;
		Busy busy;
		bindings::ErrorCapture capture;
		if (!inputWasConnected) {
			M::PortChangeEvent e;
			e.connecting = true;
			e.type = rack::engine::Port::INPUT;
			e.portId = inputId;
			in.onPortChange(e);
		}
		if (!outputWasConnected) {
			M::PortChangeEvent e;
			e.connecting = true;
			e.type = rack::engine::Port::OUTPUT;
			e.portId = outputId;
			out.onPortChange(e);
		}
		capture.rethrow();
		return id;
	}

	void disconnect(int64_t id) {
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("disconnecting a cable on a bench");
		auto it = std::find_if(cables.begin(), cables.end(), [&](const BenchCable& c) { return c.id == id; });
		if (it == cables.end())
			throw std::invalid_argument("no cable " + std::to_string(id) + " on this bench");
		BenchCable cable = *it;
		cables.erase(it);
		bool inputIsConnected = false;
		bool outputIsConnected = false;
		for (const auto& c : cables) {
			if (c.inputModule == cable.inputModule && c.inputId == cable.inputId)
				inputIsConnected = true;
			if (c.outputModule == cable.outputModule && c.outputId == cable.outputId)
				outputIsConnected = true;
		}
		rack::engine::Input& input = cable.inputModule->inputs[cable.inputId];
		if (!inputIsConnected) {
			input.channels = 0;
			for (uint8_t c = 0; c < rack::engine::PORT_MAX_CHANNELS; c++)
				input.setVoltage(0.f, c);
		}
		if (!outputIsConnected)
			cable.outputModule->outputs[cable.outputId].channels = 0;  // Rack keeps the output's voltages
		Busy busy;
		bindings::ErrorCapture capture;
		if (!inputIsConnected) {
			M::PortChangeEvent e;
			e.connecting = false;
			e.type = rack::engine::Port::INPUT;
			e.portId = cable.inputId;
			cable.inputModule->onPortChange(e);
		}
		if (!outputIsConnected) {
			M::PortChangeEvent e;
			e.connecting = false;
			e.type = rack::engine::Port::OUTPUT;
			e.portId = cable.outputId;
			cable.outputModule->onPortChange(e);
		}
		capture.rethrow();
	}

	// --- Rack's Engine::bypassModule and setSampleRate -------------------------------
	void setBypassed(py::object value, bool bypassed) {
		refuseInsideCallback("bypassing a module on a bench");
		BenchModule& bm = entry(value);
		if (bm.bypassed == bypassed)
			return;
		module::Module& m = *bm.module;
		for (rack::engine::Output& output : m.outputs)
			output.setChannels(0);  // zeros the voltages; a connected output keeps 1 channel
		bm.bypassed = bypassed;
		Busy busy;
		bindings::ErrorCapture capture;
		if (bypassed) {
			M::BypassEvent e;
			m.onBypass(e);
		}
		else {
			M::UnBypassEvent e;
			m.onUnBypass(e);
		}
		capture.rethrow();
	}

	void setSampleRate(float rate) {
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("changing a bench's sample rate");
		if (!std::isfinite(rate) || !(rate > 0.f))
			throw std::invalid_argument("the sample rate must be finite and positive");
		if (rate == sampleRate)
			return;
		sampleRate = rate;
		sampleTime = 1.f / rate;
		Busy busy;
		bindings::ErrorCapture capture;
		M::SampleRateChangeEvent e;
		e.sampleRate = sampleRate;
		e.sampleTime = sampleTime;
		for (size_t i = 0; i < modules.size(); i++)
			modules[i].module->onSampleRateChange(e);
		capture.rethrow();
	}

	// --- Rack's Engine::setMasterModule, resetModule, randomizeModule, prepareSave ------
	void setMasterNoLock(module::Module* m) {
		if (m == master)
			return;
		if (master) {
			M::UnsetMasterEvent e;
			master->onUnsetMaster(e);
		}
		master = m;
		if (master) {
			M::SetMasterEvent e;
			master->onSetMaster(e);
		}
	}

	void setMaster(py::object value) {
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("changing a bench's master module");
		module::Module* m = value.is_none() ? nullptr : entry(value).module;
		Busy busy;
		bindings::ErrorCapture capture;
		setMasterNoLock(m);
		capture.rethrow();
	}

	template <typename Dispatch>
	void moduleEvent(py::object value, const char* what, Dispatch dispatch) {
		refuseInsideCallback(what);
		module::Module& m = *entry(value).module;
		Busy busy;
		bindings::ErrorCapture capture;
		dispatch(m);
		capture.rethrow();
	}

	void prepareSave() {
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("saving a bench");
		Busy busy;
		bindings::ErrorCapture capture;
		for (size_t i = 0; i < modules.size(); i++) {
			M::SaveEvent e;
			modules[i].module->onSave(e);
		}
		capture.rethrow();
	}

	// --- Expanders: RackWidget and fromJson set the IDs; stepBlock follows them -------
	void setExpander(py::object value, int side, py::object neighbour) {
		refuseInsideCallback("placing a module next to another");
		if (side != 0 && side != 1)
			throw std::invalid_argument("side is 0 (left) or 1 (right)");
		module::Module& m = *entry(value).module;
		m.getExpander(side).moduleId = neighbour.is_none() ? -1 : entry(neighbour).module->id;
	}

	py::object expanderModule(py::object value, int side) {
		if (side != 0 && side != 1)
			throw std::invalid_argument("side is 0 (left) or 1 (right)");
		RackModule* other = entry(value).module->getExpander(side).module;
		BenchModule* bm = find(other);
		return bm ? bm->handle : py::none();
	}

	/** Rack's Module::setExpanderModule (PRIVATE): the pointer, and an ExpanderChangeEvent if it changed. */
	static void setExpanderModule(module::Module& m, RackModule* other, uint8_t side) {
		RackModule::Expander& expander = m.getExpander(side);
		if (expander.module != other) {
			expander.module = other;
			M::ExpanderChangeEvent e;
			e.side = side;
			m.onExpanderChange(e);
		}
	}

	/** Engine_updateExpander_NoLock. */
	void updateExpander(module::Module& m, uint8_t side) {
		RackModule::Expander& expander = m.getExpander(side);
		if (expander.moduleId >= 0) {
			if (!expander.module || expander.module->id != expander.moduleId) {
				RackModule* found = nullptr;
				for (const auto& bm : modules)
					if (bm.module->id == expander.moduleId)
						found = bm.module;
				setExpanderModule(m, found, side);
			}
		}
		else if (expander.module) {
			setExpanderModule(m, nullptr, side);
		}
	}

	// --- Rack's Engine::stepBlock and Engine_stepFrame -------------------------------
	void step(int frames) {
		requireEnabled("stepping a laboratory Bench");
		runtime::requireMainThread("stepping a laboratory Bench");
		refuseInsideCallback("stepping a bench");
		if (frames < 1 || frames > (1 << 16))
			throw std::invalid_argument("frames must be from 1 to 65536");
		Busy busy;
		{
			bindings::ErrorCapture capture;
			for (size_t i = 0; i < modules.size(); i++) {
				updateExpander(*modules[i].module, 0);
				updateExpander(*modules[i].module, 1);
			}
			capture.rethrow();
		}
		for (int i = 0; i < frames; i++)
			stepFrame();
		block++;
	}

	void stepFrame() {
		M::ProcessArgs args;
		args.sampleRate = sampleRate;
		args.sampleTime = sampleTime;
		args.frame = frame;
		// Engine_stepWorker with one thread: each module in the order it was added. A
		// callback's error stops the frame there, before the cables step, and is raised.
		for (size_t i = 0; i < modules.size(); i++) {
			BenchModule& bm = modules[i];
			bindings::ErrorCapture capture;
			if (bm.bypassed)
				bm.module->processBypass(args);
			else
				bm.module->process(args);
			capture.rethrow();
		}
		stepCables();
		for (const BenchModule& bm : modules) {
			for (RackModule::Expander* expander : {&bm.module->leftExpander, &bm.module->rightExpander}) {
				if (expander->messageFlipRequested) {
					std::swap(expander->producerMessage, expander->consumerMessage);
					expander->messageFlipRequested = false;
				}
			}
		}
		frame++;
	}

	/** Engine_stepFrameCables: a single cable copies its output; stacked cables sum. */
	void stepCables() {
		auto finitize = [](float x) { return std::isfinite(x) ? x : 0.f; };
		auto first = cables.begin();
		while (first != cables.end()) {
			rack::engine::Input* input = &first->inputModule->inputs[first->inputId];
			auto end = first;
			while (++end != cables.end()) {
				if (!(end->inputId == first->inputId && end->inputModule == first->inputModule))
					break;
			}
			if (end - first == 1) {
				rack::engine::Output* output = &first->outputModule->outputs[first->outputId];
				for (uint8_t c = 0; c < output->channels; c++)
					input->voltages[c] = finitize(output->voltages[c]);
				for (uint8_t c = output->channels; c < input->channels; c++)
					input->voltages[c] = 0.f;
				input->channels = output->channels;
			}
			else {
				uint8_t channels = 0;
				for (auto it = first; it < end; ++it)
					channels = std::max(channels, it->outputModule->outputs[it->outputId].channels);
				for (uint8_t c = 0; c < std::max(channels, input->channels); c++)
					input->voltages[c] = 0.f;
				input->channels = channels;
				for (auto it = first; it < end; ++it) {
					rack::engine::Output* output = &it->outputModule->outputs[it->outputId];
					if (output->channels == 1) {
						float value = finitize(output->voltages[0]);
						for (uint8_t c = 0; c < channels; c++)
							input->voltages[c] += value;
					}
					else {
						for (uint8_t c = 0; c < output->channels; c++)
							input->voltages[c] += finitize(output->voltages[c]);
					}
				}
			}
			first = end;
		}
	}

	/** Rack's Engine::clear: every cable, then every module, with their events. */
	void clear() {
		runtime::requireMainThread("using a laboratory Bench");
		refuseInsideCallback("clearing a bench");
		while (!cables.empty())
			disconnect(cables.back().id);
		while (!modules.empty())
			remove(modules.back().handle);
	}
};

int traverseBench(PyObject* self, visitproc visit, void* arg) {
	Py_VISIT(Py_TYPE(self));
	auto& bench = py::cast<Bench&>(py::handle(self));
	for (const auto& bm : bench.modules)
		Py_VISIT(bm.handle.ptr());
	return 0;
}

int clearBench(PyObject* self) {
	py::cast<Bench&>(py::handle(self)).drop();
	return 0;
}

py::object moduleHandle(const Bench& bench, const RackModule* m) {
	for (const auto& bm : bench.modules)
		if (bm.module == m)
			return bm.handle;
	return py::none();
}

}  // namespace

void bindBench(py::module_ lab) {
	py::class_<Bench>(lab, "Bench", py::custom_type_setup([](PyHeapTypeObject* heap) {
		auto* type = &heap->ht_type;
		type->tp_flags |= Py_TPFLAGS_HAVE_GC;
		type->tp_traverse = traverseBench;
		type->tp_clear = clearBench;
	}), "An offline host that steps RackPy modules as Rack's engine would, from Python on the main thread.")
		.def(py::init([](float rate) {
			requireEnabled("creating a laboratory Bench");
			runtime::requireMainThread("creating a laboratory Bench");
			if (!std::isfinite(rate) || !(rate > 0.f))
				throw std::invalid_argument("the sample rate must be finite and positive");
			return std::make_unique<Bench>(rate);
		}), py::arg("sample_rate") = 44100.f)
		.def("add", &Bench::add, py::arg("module"),
			"Rack's addModule: AddEvent, then SampleRateChangeEvent. Gives the module the lowest free ID if it has none.")
		.def("remove", &Bench::remove, py::arg("module"), "Rack's removeModule: RemoveEvent. Disconnect its cables first.")
		.def("connect", &Bench::connect, py::arg("output_module"), py::arg("output_id"), py::arg("input_module"),
			py::arg("input_id"), "Rack's addCable: returns the cable's ID; PortChangeEvents for newly connected ports.")
		.def("disconnect", &Bench::disconnect, py::arg("cable_id"), "Rack's removeCable.")
		.def("set_bypassed", &Bench::setBypassed, py::arg("module"), py::arg("bypassed"),
			"Rack's bypassModule: outputs cleared, then BypassEvent or UnBypassEvent. The module's own "
			"is_bypassed() reads Rack's engine and stays False on a bench; ask the bench.")
		.def("is_bypassed", [](Bench& b, py::object m) { return b.entry(m).bypassed; }, py::arg("module"))
		.def("set_sample_rate", &Bench::setSampleRate, py::arg("sample_rate"),
			"A SampleRateChangeEvent for every module, as Rack's setSampleRate.")
		.def_property_readonly("sample_rate", [](const Bench& b) { return b.sampleRate; })
		.def_property_readonly("sample_time", [](const Bench& b) { return b.sampleTime; })
		.def_property_readonly("frame", [](const Bench& b) { return b.frame; }, "Frames stepped so far.")
		.def_property_readonly("block", [](const Bench& b) { return b.block; }, "step() calls completed so far.")
		.def("step", &Bench::step, py::arg("frames") = 1,
			"Rack's stepBlock: for each frame, process (or process_bypass) of every module in order, then the "
			"cables. A callback's error stops the frame before the cables step and is raised here.")
		.def_property_readonly("modules", [](const Bench& b) {
			runtime::requireMainThread("using a laboratory Bench");
			py::tuple out(b.modules.size());
			for (size_t i = 0; i < b.modules.size(); i++)
				out[i] = b.modules[i].handle;
			return out;
		}, "The modules, in the order the bench processes them.")
		.def_property_readonly("cables", [](const Bench& b) {
			runtime::requireMainThread("using a laboratory Bench");
			py::list out;
			std::vector<BenchCable> byId = b.cables;
			std::sort(byId.begin(), byId.end(), [](const BenchCable& x, const BenchCable& y) { return x.id < y.id; });
			for (const auto& c : byId)
				out.append(py::make_tuple(c.id, moduleHandle(b, c.outputModule), c.outputId, moduleHandle(b, c.inputModule),
					c.inputId));
			return out;
		}, "(cable_id, output_module, output_id, input_module, input_id) for each cable, by ID.")
		.def("clear", &Bench::clear, "Rack's Engine::clear: every cable, then every module, with their events.")
		.def("set_master_module", &Bench::setMaster, py::arg("module"),
			"Rack's setMasterModule: UnsetMasterEvent for the old master, SetMasterEvent for the new one (None for none). "
			"A bench has no audio clock: the master is only a designation.")
		.def_property_readonly("master_module", [](Bench& b) {
			BenchModule* bm = b.find(b.master);
			return bm ? bm->handle : py::none();
		})
		.def("reset_module", [](Bench& b, py::object m) {
			b.moduleEvent(m, "resetting a module on a bench", [](module::Module& mod) {
				requireRackContext(mod, "Rack's resetModule");
				M::ResetEvent e;
				mod.onReset(e);
			});
		}, py::arg("module"), "Rack's resetModule: a ResetEvent.")
		.def("randomize_module", [](Bench& b, py::object m) {
			b.moduleEvent(m, "randomizing a module on a bench", [](module::Module& mod) {
				requireRackContext(mod, "Rack's randomizeModule");
				M::RandomizeEvent e;
				mod.onRandomize(e);
			});
		}, py::arg("module"), "Rack's randomizeModule: a RandomizeEvent.")
		.def("prepare_save_module", [](Bench& b, py::object m) {
			b.moduleEvent(m, "saving a bench", [](module::Module& mod) {
				M::SaveEvent e;
				mod.onSave(e);
			});
		}, py::arg("module"), "Rack's prepareSaveModule: a SaveEvent.")
		.def("prepare_save", &Bench::prepareSave, "Rack's prepareSave: a SaveEvent for every module, in order.")
		.def("set_expander", &Bench::setExpander, py::arg("module"), py::arg("side"), py::arg("neighbour"),
			"Place `neighbour` (a module on this bench, or None) on the module's left (side 0) or right (side 1), as "
			"Rack's rack does for adjacent module widgets and patch loading do: it sets the expander's module ID. The "
			"pointer, and an ExpanderChangeEvent, follow at the next step(), as in Rack.")
		.def("get_expander_module", &Bench::expanderModule, py::arg("module"), py::arg("side"),
			"The module the expander points at since the last step() (Rack's Expander::module), or None.");

	runtime::onFinalize([] {
		for (Bench* bench : benches)
			bench->drop();
	});
}

}  // namespace laboratory
}  // namespace rackpy
