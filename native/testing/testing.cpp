// SPDX-License-Identifier: GPL-3.0-or-later
// RuntimeProbe: a module, a widget and a transferable child widget that let
// tests exercise the runtime in Rack. Test hooks only; see testing.hpp.
#include "testing.hpp"

#include <pybind11/stl.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace py = pybind11;

namespace rackpy {
namespace testing {

using namespace rackpy::runtime;

namespace {

struct ProbeWidget;

rack::plugin::Model* model = nullptr;
PyRef stepCallback;
PyRef resetCallback;
std::vector<PyRef>* leaked = nullptr;  // deliberately never released

struct ProbeModule : rack::engine::Module {
	std::shared_ptr<ObjectToken> token;
	std::atomic<uint64_t> processed{0};
	std::atomic<bool> tryPython{false};
	std::unique_ptr<PyRef> engineDrop;
	std::atomic<bool> dropPending{false};
	ProbeWidget* widget = nullptr;

	ProbeModule() {
		config(1, 1, 1, 0);
		configParam(0, 0.f, 1.f, 0.5f, "Probe");
		token = std::make_shared<ObjectToken>(bindCurrentContext());
	}
	~ProbeModule() override {
		token->invalidate();  // the only thing deletion does for Python
	}
	void process(const ProcessArgs& args) override {
		processed.fetch_add(1, std::memory_order_relaxed);
		if (tryPython.exchange(false))
			callPython("RuntimeProbe process", [] {});  // must be refused: this is an engine thread
		if (dropPending.load(std::memory_order_acquire)) {
			engineDrop.reset();  // a native holder drops its Python reference on an engine thread
			dropPending.store(false, std::memory_order_release);
		}
	}
	void onReset(const ResetEvent& e) override {
		ExclusiveScope exclusive;  // Rack holds the engine lock exclusively here
		Module::onReset(e);
		callPython("RuntimeProbe reset", [this] {
			if (resetCallback)
				resetCallback.get()(handleFor(this));
		});
	}
	static py::object handleFor(ProbeModule* m);
};

struct ModuleHandle : Borrowed<ProbeModule> {};

py::object ProbeModule::handleFor(ProbeModule* m) {
	return identity(m, m->token->generation, [m] { return py::cast(ModuleHandle{{m, m->token, "RuntimeProbe module"}}); });
}

/** A widget Python can subclass and hand to Rack, which then owns and deletes it. */
struct ProbeChild : rack::widget::Widget {
	std::shared_ptr<ObjectToken> token = std::make_shared<ObjectToken>(nullptr);
	bool transferred = false;
	~ProbeChild() override {
		token->invalidate();
		if (transferred)
			noteTransferredDeleted();
	}
	virtual void onRemoved() {}
	void onRemove(const RemoveEvent& e) override {
		onRemoved();
		Widget::onRemove(e);
	}
};

struct PyProbeChild : ProbeChild, py::trampoline_self_life_support {
	void onRemoved() override {
		// Rack calls this on the main thread; take the GIL like any Rack-to-Python call.
		callPython("ProbeChild.on_removed", [this] {
			if (py::function f = py::get_override(static_cast<const ProbeChild*>(this), "on_removed"))
				f();
		});
	}
};

ProbeWidget* driver = nullptr;
int frame = 0;

struct ProbeWidget : rack::app::ModuleWidget {
	explicit ProbeWidget(ProbeModule* module) {
		setModule(module);
		box.size = rack::math::Vec(rack::app::RACK_GRID_WIDTH * 4, rack::app::RACK_GRID_HEIGHT);
		bindCurrentContext();
		if (module) {
			module->widget = this;
			if (!driver)
				driver = this;
		}
	}
	~ProbeWidget() override {
		if (driver == this)
			driver = nullptr;
		if (module)
			static_cast<ProbeModule*>(module)->widget = nullptr;
	}
	void step() override {
		ModuleWidget::step();
		if (driver != this || !module || !stepCallback)
			return;
		int f = ++frame;
		ProbeModule* m = static_cast<ProbeModule*>(module);
		callPython("RuntimeProbe step", [f, m] { stepCallback.get()(f, ProbeModule::handleFor(m)); });
	}
};

std::shared_ptr<ContextToken> liveContext() {
	std::shared_ptr<ContextToken> token = binding().current();
	if (!token)
		throw StaleHandle("RackPy has no live Rack context");
	checkContext(*token);
	return token;
}

ProbeModule& live(const ModuleHandle& h) {
	ProbeModule& m = h.get();
	if (!h.token->context)
		throw WrongContext("this module was created outside RackPy's bound Rack context");
	checkContext(*h.token->context);
	return m;
}

/** A Rack context nobody uses, to show that handles refuse to work under it. */
rack::Context* foreignContext() {
	static rack::Context* fake = new rack::Context;  // never deleted: deleting a Context tears down its members
	return fake;
}

}  // namespace

void define(py::module_ m) {
	py::class_<ModuleHandle>(m, "ModuleHandle")
		.def_property_readonly("alive", &ModuleHandle::alive)
		.def_property_readonly("generation", &ModuleHandle::generation)
		.def_property_readonly("module_id", [](const ModuleHandle& h) { return live(h).id; })
		.def_property_readonly("processed", [](const ModuleHandle& h) { return live(h).processed.load(); })
		.def_property_readonly("has_widget", [](const ModuleHandle& h) { return live(h).widget != nullptr; });

	py::classh<ProbeChild, PyProbeChild>(m, "ProbeChild")
		.def(py::init<>())
		.def("on_removed", &ProbeChild::onRemoved)
		.def_property_readonly("alive", [](const ProbeChild& c) { return c.token->alive(); });

	m.def("set_step_callback", [](py::object f) {
		requireMainThread("set_step_callback");
		stepCallback = PyRef(std::move(f));
	});
	m.def("set_reset_callback", [](py::object f) {
		requireMainThread("set_reset_callback");
		resetCallback = PyRef(std::move(f));
	});
	m.def("add_module", [] {
		requireMainThread("add_module");
		refuseEngineReentry("add_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		ProbeModule* pm = static_cast<ProbeModule*>(model->createModule());
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			APP->engine->addModule(pm);
		}
		return ProbeModule::handleFor(pm);
	});
	m.def("remove_module", [](const ModuleHandle& h) {
		requireMainThread("remove_module");
		refuseEngineReentry("remove_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		ProbeModule& pm = live(h);
		if (pm.widget)
			throw Ownership("this module belongs to a module widget, which Rack deletes");
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			APP->engine->removeModule(&pm);
		}
		delete &pm;
	});
	m.def("reset_module", [](const ModuleHandle& h) {
		requireMainThread("reset_module");
		refuseEngineReentry("reset_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		ProbeModule& pm = live(h);
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		APP->engine->resetModule(&pm);
	});
	m.def("engine_module_count", [] {
		if (!pythonAllowedHere())
			throw WrongThread("engine_module_count cannot run on Rack's own threads");
		refuseEngineReentry("engine_module_count");
		std::shared_ptr<ContextToken> ctx = liveContext();
		// Release the GIL before taking any engine lock (ADR 003, decision 2).
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		return APP->engine->getModuleIds().size();
	});
	m.def("try_python_on_engine_thread", [](const ModuleHandle& h) { live(h).tryPython.store(true); });
	m.def("drop_on_engine_thread", [](const ModuleHandle& h, py::object o) {
		requireMainThread("drop_on_engine_thread");
		ProbeModule& pm = live(h);
		if (pm.dropPending.load(std::memory_order_acquire))
			throw Ownership("a drop is already pending");
		pm.engineDrop = std::make_unique<PyRef>(std::move(o));
		pm.dropPending.store(true, std::memory_order_release);
	});
	m.def("attach_child", [](const ModuleHandle& h, py::object child) {
		requireMainThread("attach_child");
		ProbeModule& pm = live(h);
		if (!pm.widget)
			throw Ownership("this module has no widget to attach to");
		std::unique_ptr<ProbeChild> owned;
		try {
			owned = child.cast<std::unique_ptr<ProbeChild>>();
		}
		catch (const std::exception& e) {
			throw Ownership(std::string("this ProbeChild cannot be handed to Rack: it already belongs to Rack (") + e.what() + ")");
		}
		owned->transferred = true;
		noteTransferred();
		pm.widget->addChild(owned.release());  // Rack's widget tree owns it now
	});
	m.def("with_foreign_context", [](py::function f) {
		std::shared_ptr<ContextToken> refused = binding().observe(foreignContext());
		if (refused)
			throw std::logic_error("the foreign context was accepted");
		ContextScope scope(foreignContext());
		return f();
	});
	m.def("close_window", [] {
		requireMainThread("close_window");
		APP->window->close();
	});
	m.def("leak_reference", [](py::object o) {
		if (!leaked)
			leaked = new std::vector<PyRef>();
		leaked->emplace_back(std::move(o));
	});
	m.def("record_diagnostic", [](const std::string& message) { diagnose(DiagnosticCode::Test, message.c_str()); });
	m.def("finalize_outcome", &finalizeOutcome);

	// Drop the hooks' own callback references when the plugin is destroyed (but not the deliberate leaks).
	onFinalize([] {
		stepCallback.reset();
		resetCallback.reset();
	});
}

void registerModels(rack::plugin::Plugin* plugin) {
	model = rack::createModel<ProbeModule, ProbeWidget>("RuntimeProbe");
	plugin->addModel(model);
}

}  // namespace testing
}  // namespace rackpy
