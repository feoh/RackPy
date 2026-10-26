// SPDX-License-Identifier: GPL-3.0-or-later
// Laboratory modules (RP-22A): Python overrides of Module::process,
// processBypass and the deprecated step(), with Rack's base implementations
// as their superclass.
#include <atomic>

#include "laboratory.hpp"
#include "../bindings/engine/module.hpp"
#include "../bindings/support/hooks.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/interpreter.hpp"

namespace rackpy {
namespace laboratory {

namespace {

std::atomic<bool> laboratoryEnabled{false};

using ProcessArgs = rack::engine::Module::ProcessArgs;

/** Rack's const reference, as a Python value that may outlive the call. */
py::object argsObject(const ProcessArgs& args) {
	return py::cast(ProcessArgs(args));
}

Module& laboratoryHandle(const bindings::ModuleRef& r) {
	Module* m = laboratoryModule(r.get());
	if (!m)
		throw runtime::Laboratory("this is not a laboratory module (rackpy.laboratory.ModuleClass.create makes one)");
	return *m;
}

/** Laboratory callbacks nested in this thread: a callback that calls Rack's code
that calls a callback, and so on. Each level holds C++ frames that Python's
own recursion limit does not see, so the laboratory bounds the nesting. */
constexpr int NESTING_LIMIT = 200;
thread_local int nesting = 0;

/** Call a Python override for Rack's code. */
template <typename F>
void dispatch(const char* what, F&& call) {
	bindings::runPythonHook(what, [&] {
		if (nesting >= NESTING_LIMIT) {
			PyErr_Format(PyExc_RecursionError, "laboratory callbacks nested more than %d deep (in %s)", NESTING_LIMIT, what);
			throw py::error_already_set();
		}
		nesting++;
		struct Leave {
			~Leave() { nesting--; }
		} leave;
		call();
	});
}

/** Run Rack's code for a Python caller: an override's error inside is raised here. */
template <typename F>
void forPython(F&& f) {
	bindings::ErrorCapture capture;
	f();
	capture.rethrow();
}

}  // namespace

bool enabled() {
	return laboratoryEnabled.load(std::memory_order_acquire);
}

void requireEnabled(const char* what) {
	if (!enabled())
		throw runtime::Laboratory(std::string(what) + " needs the callback laboratory, which is disabled: Python never runs "
			"on Rack's audio threads. Trusted code may call rackpy.laboratory.enable() to run Python callbacks offline "
			"on a laboratory Bench, for experiments and tests; that is not real-time audio");
}

// Every override runs where its caller is: a Bench step or a base_* call from
// Python, on the main thread with the GIL, so an error goes back to that
// caller. Anything else (no GIL, another thread) is contained by callPython,
// which never runs Python on Rack's own threads.

void Module::process(const ProcessArgs& args) {
	if (!processFn) {
		rack::engine::Module::process(args);  // Rack's: calls the deprecated step()
		return;
	}
	dispatch("laboratory Module.process", [&] { processFn.get()(argsObject(args)); });
}

void Module::processBypass(const ProcessArgs& args) {
	if (!processBypassFn) {
		rack::engine::Module::processBypass(args);  // Rack's: the bypass routes
		return;
	}
	dispatch("laboratory Module.process_bypass", [&] { processBypassFn.get()(argsObject(args)); });
}

void Module::step() {
	if (!stepFn) {
		rack::engine::Module::step();
		return;
	}
	dispatch("laboratory Module.step", [&] { stepFn.get()(); });
}

void bindModules(py::module_ lab) {
	lab.def("enable", [] {
		runtime::requireMainThread("enabling the callback laboratory");
		if (!laboratoryEnabled.exchange(true))
			WARN("RackPy: the callback laboratory is enabled: Python module callbacks run offline on laboratory "
				"benches, never in Rack's engine, and are not real-time audio");
	}, "Enable the callback laboratory for this process. Trusted code only; patches never enable it.");
	lab.def("disable", [] {
		runtime::requireMainThread("disabling the callback laboratory");
		laboratoryEnabled.store(false, std::memory_order_release);
	}, "Disable it again: creating laboratory modules and benches, and stepping benches, are refused.");
	lab.def("enabled", &enabled);

	lab.def("create_module", [](py::object configure) {
		requireEnabled("creating a laboratory module");
		runtime::requireMainThread("creating a laboratory module");
		return bindings::createWith(std::make_shared<module::Construction>(std::make_unique<Module>()), std::move(configure));
	}, py::arg("configure"),
		"Module.create(configure) for a laboratory module: a RackPy module that only a laboratory Bench runs.");
	lab.def("is_laboratory_module", [](const bindings::ModuleRef& r) { return laboratoryModule(r.get()) != nullptr; },
		py::arg("module"));
	lab.def("set_overrides", [](const bindings::ModuleRef& r, py::object process, py::object processBypass, py::object step) {
		r.state->requireConstruction();
		Module& m = laboratoryHandle(r);
		for (const py::object& f : {process, processBypass, step})
			if (!f.is_none() && !PyCallable_Check(f.ptr()))
				throw py::type_error("laboratory overrides must be callable or None");
		auto ref = [](const py::object& f) { return f.is_none() ? runtime::PyRef() : runtime::PyRef(f); };
		m.processFn = ref(process);
		m.processBypassFn = ref(processBypass);
		m.stepFn = ref(step);
	}, py::arg("module"), py::arg("process") = py::none(), py::arg("process_bypass") = py::none(),
		py::arg("step") = py::none(),
		"Install a laboratory module's Python overrides, called as process(args), process_bypass(args) and step(). "
		"Constructor-only; replaces earlier ones.");

	// Rack's own implementations, which a Python override reaches with super().
	lab.def("base_process", [](const bindings::ModuleRef& r, const ProcessArgs& args) {
		Module& m = laboratoryHandle(r);
		forPython([&] { m.rack::engine::Module::process(args); });
	}, py::arg("module"), py::arg("args"), "Rack's Module::process: calls step().");
	lab.def("base_process_bypass", [](const bindings::ModuleRef& r, const ProcessArgs& args) {
		Module& m = laboratoryHandle(r);
		forPython([&] { m.rack::engine::Module::processBypass(args); });
	}, py::arg("module"), py::arg("args"), "Rack's Module::processBypass: copies each bypass route's input to its output.");
	lab.def("base_step", [](const bindings::ModuleRef& r) {
		Module& m = laboratoryHandle(r);
		forPython([&] { m.rack::engine::Module::step(); });
	}, py::arg("module"), "Rack's deprecated Module::step: does nothing.");
}

void bind(py::module_ host) {
	py::module_ lab = host.def_submodule("laboratory", "The callback laboratory (RP-22); use rackpy.laboratory.");
	bindModules(lab);
	bindBench(lab);
	runtime::onFinalize([] { laboratoryEnabled.store(false); });
}

}  // namespace laboratory
}  // namespace rackpy
