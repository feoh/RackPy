// SPDX-License-Identifier: GPL-3.0-or-later
// RP-06 lifecycle probe host: records which thread runs every Module and
// ModuleWidget callback in a real Rack host, and exercises the GIL, engine-lock,
// context and teardown policies proposed in ADR 003. Spike code, not RackPy.
#include <pybind11/embed.h>

#include <rack.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "host_api.h"

namespace py = pybind11;

namespace lc {

// --- thread roles and bookkeeping (native only) ------------------------------

std::thread::id mainThread;
thread_local bool processThread = false;
// >0 while this thread is inside a callback that Rack dispatches under the
// engine's exclusive lock. Engine calls from Python are refused there.
thread_local int exclusiveDepth = 0;

enum Role { MAIN = 0, ENGINE = 1, OTHER = 2 };
const char* const ROLE_NAMES[] = {"main", "engine", "other"};
std::atomic<uint64_t> gilAcquires[3];
std::atomic<uint64_t> refusedEngineThreadPython{0};

Role role() {
	if (std::this_thread::get_id() == mainThread)
		return MAIN;
	return processThread ? ENGINE : OTHER;
}

size_t tid() {
	return std::hash<std::thread::id>()(std::this_thread::get_id()) % 100000;
}

// The context RackPy binds to. Python-created threads have no Rack context
// (contextGet() is thread-local), so engine calls from them scope this one.
std::atomic<rack::Context*> boundContext{nullptr};
std::atomic<bool> contextAlive{false};
// RackPy-owned lease: every engine call from Python holds it shared. Shutdown
// takes it exclusively, so in-flight calls finish before the engine is deleted.
std::shared_mutex lease;

struct ContextScope {
	rack::Context* previous;
	explicit ContextScope(rack::Context* c) : previous(rack::contextGet()) {
		rack::contextSet(c);
	}
	~ContextScope() {
		rack::contextSet(previous);
	}
};

struct ExclusiveScope {
	ExclusiveScope() {
		exclusiveDepth++;
	}
	~ExclusiveScope() {
		exclusiveDepth--;
	}
};

struct Token {
	std::atomic<bool> alive{true};
	std::atomic<int64_t> moduleId{-1};
	std::atomic<uint64_t> processed{0};
	std::atomic<float> gain{1.f};
};

struct StaleHandle {
	int64_t moduleId;
};
struct EngineRefused {
	const char* why;
};

struct Handle {
	std::shared_ptr<Token> token;
	const Token& live() const {
		if (!token->alive.load() || !contextAlive.load())
			throw StaleHandle{token->moduleId.load()};
		return *token;
	}
	int64_t id() const { return token->moduleId.load(); }
	bool alive() const { return token->alive.load() && contextAlive.load(); }
	float gain() const { return live().gain.load(); }
	void setGain(float g) {
		live();
		token->gain.store(g);
	}
	uint64_t processed() const { return live().processed.load(); }
};

// Strong references to Python callables; dropped before Py_FinalizeEx.
py::object callbacks;
PyThreadState* mainState = nullptr;
bool pythonReady = false;

void event(const char* name, int64_t moduleId, const char* detail = "") {
	INFO("RACKPY_LC event=%s role=%s tid=%zu module=%lld exclusive=%d %s", name, ROLE_NAMES[role()], tid(),
		(long long) moduleId, exclusiveDepth, detail);
}

/** Every Rack-to-Python call goes through here so GIL use can be counted per thread role. */
template <typename F>
void callPython(const char* what, F&& f) {
	if (!pythonReady || callbacks.ptr() == nullptr)
		return;
	Role r = role();
	if (r == ENGINE) {
		// Production rule: no Python on engine threads. Count and refuse.
		refusedEngineThreadPython++;
		return;
	}
	gilAcquires[r]++;
	PyGILState_STATE g = PyGILState_Ensure();
	try {
		f();
	}
	catch (const std::exception& e) {
		WARN("RACKPY_LC python %s raised: %s", what, e.what());
	}
	PyGILState_Release(g);
}

bool hasCallback(const char* name) {
	return py::hasattr(callbacks, name);
}

/** Earliest teardown signal a plugin gets: the sentinel's onContextDestroy (window deleted first in
~Context) or, headless, its destructor during scene deletion. Both come before the engine is deleted. */
void shutdownBegin(const char* source) {
	static bool begun = false;
	if (begun)
		return;
	begun = true;
	event("shutdown begin", -1, source);
	callPython("on_context_closing", [&] {
		if (hasCallback("on_context_closing"))
			callbacks.attr("on_context_closing")();
	});
	{
		std::unique_lock<std::shared_mutex> drain(lease);
		contextAlive.store(false);
	}
	event("shutdown leases drained", -1);
}

struct Sentinel : rack::widget::Widget {
	void onContextDestroy(const ContextDestroyEvent& e) override {
		shutdownBegin("sentinel onContextDestroy");
		Widget::onContextDestroy(e);
	}
	~Sentinel() override { shutdownBegin("sentinel destructor"); }
};

// --- probe module --------------------------------------------------------------

struct ProbeModule : rack::engine::Module {
	std::shared_ptr<Token> token = std::make_shared<Token>();
	std::string state = "{}";

	ProbeModule() {
		config(1, 1, 1, 0);
		configParam(0, 0.f, 1.f, 0.5f, "Probe");
		rack::Context* c = rack::contextGet();
		rack::Context* expected = nullptr;
		if (c && boundContext.compare_exchange_strong(expected, c)) {
			contextAlive.store(true);
			if (c->scene)
				c->scene->addChild(new Sentinel);
		}
		event("construct", -1, c ? "context=set" : "context=null");
	}
	~ProbeModule() override {
		token->alive.store(false);  // the only work deletion does for Python
		event("destruct", token->moduleId.load());
	}

	void process(const ProcessArgs& args) override {
		if (!processThread) {
			processThread = true;
			event("process(first on thread)", token->moduleId.load());
		}
		outputs[0].setVoltage(inputs[0].getNormalVoltage(1.f) * token->gain.load(std::memory_order_relaxed));
		token->processed.fetch_add(1, std::memory_order_relaxed);
	}

	void processBypass(const ProcessArgs& args) override {
		token->processed.fetch_add(1, std::memory_order_relaxed);
	}

	void onAdd(const AddEvent& e) override {
		token->moduleId.store(id);
		event("onAdd", id);
	}
	void onRemove(const RemoveEvent& e) override { event("onRemove", id); }

	/** Sleeps natively and reports whether process() advanced meanwhile (first call per callback only). */
	std::string concurrency(int& measured) {
		if (measured++ > 0)
			return "";
		uint64_t before = token->processed.load();
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		uint64_t after = token->processed.load();
		return rack::string::f("processed_during_30ms=%llu", (unsigned long long) (after - before));
	}

	void onReset(const ResetEvent& e) override {
		ExclusiveScope scope;
		static int measured = 0;
		event("onReset", id, concurrency(measured).c_str());
		Module::onReset(e);
		callPython("on_reset", [&] {
			if (hasCallback("on_reset"))
				callbacks.attr("on_reset")(Handle{token});
		});
	}
	void onRandomize(const RandomizeEvent& e) override {
		ExclusiveScope scope;
		event("onRandomize", id);
		Module::onRandomize(e);
	}
	void onBypass(const BypassEvent& e) override { event("onBypass", id); }
	void onUnBypass(const UnBypassEvent& e) override { event("onUnBypass", id); }
	void onPortChange(const PortChangeEvent& e) override { event("onPortChange", id); }
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		event("onSampleRateChange", id, rack::string::f("rate=%g", e.sampleRate).c_str());
	}
	void onExpanderChange(const ExpanderChangeEvent& e) override {
		event("onExpanderChange", id, rack::string::f("side=%d", (int) e.side).c_str());
	}
	void onSave(const SaveEvent& e) override { event("onSave", id); }
	void onSetMaster(const SetMasterEvent& e) override { event("onSetMaster", id); }
	void onUnsetMaster(const UnsetMasterEvent& e) override { event("onUnsetMaster", id); }

	json_t* dataToJson() override {
		static int measured = 0;
		event("dataToJson", id, concurrency(measured).c_str());
		// Synchronous adapter: Rack needs the JSON back before it continues.
		callPython("data_to_json", [&] {
			if (hasCallback("data_to_json"))
				state = py::cast<std::string>(callbacks.attr("data_to_json")(Handle{token}));
		});
		json_error_t error;
		json_t* rootJ = json_loads(state.c_str(), 0, &error);
		return rootJ ? rootJ : json_object();
	}
	void dataFromJson(json_t* rootJ) override {
		ExclusiveScope scope;
		char* text = json_dumps(rootJ, JSON_COMPACT | JSON_SORT_KEYS);
		state = text ? text : "{}";
		std::free(text);
		event("dataFromJson", id, state.c_str());
		callPython("data_from_json", [&] {
			if (hasCallback("data_from_json"))
				callbacks.attr("data_from_json")(Handle{token}, state);
		});
	}
};

rack::plugin::Model* model = nullptr;

// --- scripted scenario, driven from the first widget's step() on the UI thread ---

struct Scenario {
	int frame = 0;
	int resetsLeft = 0;
	int churnLeft = 0;
	ProbeModule* churned = nullptr;
	bool closing = false;
};
Scenario scenario;
int resetsRequested = 0;
int churnRequested = 0;
int framesBeforeClose = 0;
bool stopBeforeClose = true;
int slowUiMs = 0;
int slowUiFrames = 0;



struct ProbeWidget : rack::app::ModuleWidget {
	bool driver = false;
	static inline ProbeWidget* firstDriver = nullptr;

	ProbeWidget(ProbeModule* module) {
		setModule(module);
		box.size = rack::math::Vec(rack::app::RACK_GRID_WIDTH * 4, rack::app::RACK_GRID_HEIGHT);
		event("widget construct", module ? module->id : -1, module ? "module=set" : "module=null");
		if (module && !firstDriver) {
			firstDriver = this;
			driver = true;
		}
	}
	~ProbeWidget() override {
		if (firstDriver == this)
			firstDriver = nullptr;
		event("widget destruct", module ? module->id : -1);
	}

	ProbeModule* probe() { return static_cast<ProbeModule*>(module); }

	void step() override {
		ModuleWidget::step();
		static bool logged = false;
		if (!logged) {
			logged = true;
			event("widget step", module ? module->id : -1);
		}
		if (driver && module)
			drive();
	}
	void draw(const DrawArgs& args) override {
		static bool logged = false;
		if (!logged) {
			logged = true;
			event("widget draw", module ? module->id : -1);
		}
		ModuleWidget::draw(args);
	}
	void onContextCreate(const ContextCreateEvent& e) override {
		event("widget onContextCreate", module ? module->id : -1);
		ModuleWidget::onContextCreate(e);
	}
	void onContextDestroy(const ContextDestroyEvent& e) override {
		event("widget onContextDestroy", module ? module->id : -1);
		ModuleWidget::onContextDestroy(e);
	}
	void appendContextMenu(rack::ui::Menu* menu) override { event("appendContextMenu", module ? module->id : -1); }

	void scenarioCallback(const char* what) {
		callPython("on_scenario", [&] {
			if (hasCallback("on_scenario"))
				callbacks.attr("on_scenario")(what, Handle{probe()->token});
		});
	}

	/** Samples processed and wall time across a window, to show whether audio kept running. */
	struct Window {
		uint64_t processed = 0;
		double t = 0;
		void start(const Token& tok) {
			processed = tok.processed.load();
			t = rack::system::getTime();
		}
		std::string finish(const Token& tok, float rate) {
			double elapsed = rack::system::getTime() - t;
			uint64_t done = tok.processed.load() - processed;
			return rack::string::f("elapsed_ms=%.1f processed=%llu expected_at_rate=%.0f", elapsed * 1000,
				(unsigned long long) done, elapsed * rate);
		}
	};
	Window slowWindow, stallWindow;

	void drive() {
		Scenario& s = scenario;
		auto* engine = APP->engine;
		int f = ++s.frame;
		const Token& tok = *probe()->token;
		if (f == 5) {
			event("scenario start", module->id);
			scenarioCallback("start");
			s.resetsLeft = resetsRequested;
			s.churnLeft = churnRequested;
		}
		// Slow-UI phase: Python on the UI thread (no engine lock) sleeps every frame.
		if (slowUiFrames > 0 && f >= 6 && f < 6 + slowUiFrames) {
			if (f == 6)
				slowWindow.start(tok);
			callPython("on_slow_ui", [&] {
				if (hasCallback("on_slow_ui"))
					callbacks.attr("on_slow_ui")(slowUiMs);
			});
			if (f == 6 + slowUiFrames - 1)
				event("slow ui result", module->id, slowWindow.finish(tok, engine->getSampleRate()).c_str());
			return;
		}
		if (f >= 6)
			f -= slowUiFrames;  // later steps keep their original frame numbers
		if (f < 10)
			return;
		if (s.resetsLeft > 0) {
			// Each reset takes the engine lock exclusively and calls Python from onReset.
			bool first = s.resetsLeft == resetsRequested;
			s.resetsLeft--;
			if (first)
				stallWindow.start(tok);
			engine->resetModule(module);
			if (first)
				event("exclusive stall result", module->id, stallWindow.finish(tok, engine->getSampleRate()).c_str());
			return;
		}
		if (f == 10 + resetsRequested + 1)
			engine->randomizeModule(module);
		else if (f == 10 + resetsRequested + 2)
			engine->bypassModule(module, true);
		else if (f == 10 + resetsRequested + 3)
			engine->bypassModule(module, false);
		else if (f == 10 + resetsRequested + 4)
			engine->setSuggestedSampleRate(48000.f);
		else if (f == 10 + resetsRequested + 5) {
			json_t* rootJ = engine->toJson();  // shared lock: audio keeps running
			engine->prepareSave();
			json_t* moduleJ = engine->moduleToJson(module);
			engine->moduleFromJson(module, moduleJ);  // exclusive lock
			json_decref(moduleJ);
			json_decref(rootJ);
		}
		else if (f == 10 + resetsRequested + 6)
			createContextMenu();
		else if (s.churnLeft > 0) {
			// Add and delete modules on the UI thread while Python threads hold handles.
			if (!s.churned) {
				s.churned = static_cast<ProbeModule*>(model->createModule());
				engine->addModule(s.churned);
				ProbeModule* added = s.churned;
				callPython("on_churn", [&] {
					if (hasCallback("on_churn"))
						callbacks.attr("on_churn")(Handle{added->token});
				});
			}
			else {
				engine->removeModule(s.churned);
				delete s.churned;
				s.churned = nullptr;
				s.churnLeft--;
			}
		}
		else if (!s.closing && f >= 10 + resetsRequested + 8 + 2 * churnRequested + framesBeforeClose) {
			s.closing = true;
			// Shutdown ordering step 1: stop Python workers while the context is alive.
			if (stopBeforeClose)
				scenarioCallback("stop");
			event("scenario close", module->id);
			APP->window->close();
		}
	}
};

// --- Python module ---------------------------------------------------------------

PYBIND11_EMBEDDED_MODULE(_rackpy_lifecycle, m) {
	py::register_exception_translator([](std::exception_ptr p) {
		try {
			if (p)
				std::rethrow_exception(p);
		}
		catch (const StaleHandle& e) {
			PyErr_Format(PyExc_ReferenceError, "module %lld or its Rack context is gone", (long long) e.moduleId);
		}
		catch (const EngineRefused& e) {
			PyErr_SetString(PyExc_RuntimeError, e.why);
		}
	});
	m.def("log", [](const std::string& msg) { INFO("RACKPY_LC py: %s", msg.c_str()); });
	m.def("thread_role", [] { return std::string(ROLE_NAMES[role()]); });
	m.def("thread_has_rack_context", [] { return rack::contextGet() != nullptr; });

	// Takes the engine's shared lock. RackPy policy: release the GIL first and
	// scope the bound context. `release_gil=False` exists only to show the deadlock.
	m.def(
		"engine_module_count",
		[](bool releaseGil) {
			if (exclusiveDepth > 0)
				throw EngineRefused{"engine calls are refused inside callbacks that Rack runs under the exclusive "
									"engine lock; they would deadlock this thread"};
			// getModuleIds() takes the engine's shared lock. (getNumModules() does not lock at all.)
			auto call = []() -> size_t {
				std::shared_lock<std::shared_mutex> held(lease);
				rack::Context* c = boundContext.load();
				if (!c || !contextAlive.load())
					throw StaleHandle{-1};
				ContextScope scope(c);
				return APP->engine->getModuleIds().size();
			};
			if (releaseGil) {
				py::gil_scoped_release release;
				return call();
			}
			return call();
		},
		py::arg("release_gil") = true);

	py::class_<Handle>(m, "ModuleHandle")
		.def_property_readonly("id", &Handle::id)
		.def_property_readonly("alive", &Handle::alive)
		.def_property("gain", &Handle::gain, &Handle::setGain)
		.def_property_readonly("processed", &Handle::processed);

	m.def("set_callbacks", [](py::object o) { callbacks = std::move(o); });
	m.def("gil_acquires", [] {
		return py::dict(py::arg("main") = gilAcquires[MAIN].load(), py::arg("engine") = gilAcquires[ENGINE].load(),
			py::arg("other") = gilAcquires[OTHER].load(), py::arg("refused_engine") = refusedEngineThreadPython.load());
	});
}

int init(RackPySpikeHostInit* args) {
	mainThread = std::this_thread::get_id();
	std::string dir = args->plugin_dir;
	// Scenario parameters come from a file in the plugin directory, never from a patch.
	json_error_t error;
	json_t* cfg = json_load_file((dir + "/scenario.json").c_str(), 0, &error);
	if (cfg) {
		resetsRequested = (int) json_integer_value(json_object_get(cfg, "resets"));
		churnRequested = (int) json_integer_value(json_object_get(cfg, "churn"));
		framesBeforeClose = (int) json_integer_value(json_object_get(cfg, "frames_before_close"));
		slowUiMs = (int) json_integer_value(json_object_get(cfg, "slow_ui_ms"));
		slowUiFrames = (int) json_integer_value(json_object_get(cfg, "slow_ui_frames"));
		json_t* stopJ = json_object_get(cfg, "stop_before_close");
		stopBeforeClose = !stopJ || json_is_true(stopJ);
		json_decref(cfg);
	}

	PyPreConfig pre;
	PyPreConfig_InitIsolatedConfig(&pre);
	pre.utf8_mode = 1;
	if (PyStatus_Exception(Py_PreInitialize(&pre)))
		return 2;
	PyConfig config;
	PyConfig_InitIsolatedConfig(&config);
	config.install_signal_handlers = 0;
	config.site_import = 0;
	config.write_bytecode = 0;
	config.configure_c_stdio = 0;
	config.parse_argv = 0;
	PyStatus status = PyConfig_SetBytesString(&config, &config.home, args->python_home);
	if (!PyStatus_Exception(status))
		status = Py_InitializeFromConfig(&config);
	PyConfig_Clear(&config);
	if (PyStatus_Exception(status)) {
		std::snprintf(args->error, args->error_size, "init: %s", status.err_msg ? status.err_msg : "?");
		return 2;
	}
	try {
		py::module_::import("sys").attr("path").attr("insert")(0, dir);
		py::module_::import("lifecycle_plugin");
	}
	catch (const std::exception& e) {
		std::snprintf(args->error, args->error_size, "script: %s", e.what());
		mainState = PyEval_SaveThread();
		return 3;
	}
	pythonReady = true;
	mainState = PyEval_SaveThread();
	model = rack::createModel<ProbeModule, ProbeWidget>("Probe");
	static_cast<rack::plugin::Plugin*>(args->plugin)->addModel(model);
	return 0;
}

void destroy() {
	// Shutdown ordering step 3: Rack has deleted the context and every module.
	contextAlive.store(false);
	boundContext.store(nullptr);
	if (!mainState)
		return;
	event("destroy", -1);
	PyEval_RestoreThread(mainState);
	mainState = nullptr;
	try {
		if (hasCallback("on_shutdown"))
			callbacks.attr("on_shutdown")();
	}
	catch (const std::exception& e) {
		WARN("RACKPY_LC on_shutdown raised: %s", e.what());
	}
	INFO("RACKPY_LC gil main=%llu engine=%llu other=%llu refused_engine=%llu", (unsigned long long) gilAcquires[MAIN].load(),
		(unsigned long long) gilAcquires[ENGINE].load(), (unsigned long long) gilAcquires[OTHER].load(),
		(unsigned long long) refusedEngineThreadPython.load());
	// Shutdown ordering step 4: drop every strong reference. A py::object that is
	// still alive when Py_FinalizeEx returns (even a local) crashes Rack at exit.
	callbacks = py::object();
	pythonReady = false;
	INFO("RACKPY_LC Py_FinalizeEx rc=%d", Py_FinalizeEx());
}

}  // namespace lc

extern "C" __attribute__((visibility("default"))) int rackpy_spike_host_init(RackPySpikeHostInit* args) {
	try {
		return lc::init(args);
	}
	catch (...) {
		std::snprintf(args->error, args->error_size, "unexpected C++ exception");
		return 99;
	}
}

extern "C" __attribute__((visibility("default"))) void rackpy_spike_host_destroy() {
	try {
		lc::destroy();
	}
	catch (...) {
		WARN("RACKPY_LC unexpected C++ exception in destroy");
	}
}
