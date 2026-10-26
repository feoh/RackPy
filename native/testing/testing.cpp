// SPDX-License-Identifier: GPL-3.0-or-later
// RuntimeProbe: a module, a widget and a transferable child widget that let
// tests exercise the runtime in Rack. Test hooks only; see testing.hpp.
#include "testing.hpp"

#include "../bindings/engine/components.hpp"
#include "../bindings/engine/module.hpp"
#include "../bindings/widget/managed.hpp"
#include "../tests/quantity_reference.hpp"
#include "../plugin/model.hpp"
#include "../bindings/plugin.hpp"
#include "rackpy/runtime/json.hpp"

#include <pybind11/stl.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
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

/** Reference counts of every node that has one (not jansson's null/true/false singletons), preorder. */
void collectRefcounts(json_t* json, std::vector<size_t>& out) {
	if (!json || json_is_null(json) || json_is_boolean(json))
		return;
	out.push_back(size_t(json->refcount));
	size_t index;
	const char* key;
	json_t* item;
	if (json_is_array(json)) {
		json_array_foreach(json, index, item) {
			collectRefcounts(item, out);
		}
	}
	else if (json_is_object(json)) {
		json_object_foreach(json, key, item) {
			collectRefcounts(item, out);
		}
	}
}

std::vector<size_t> refcounts(json_t* json) {
	std::vector<size_t> out;
	collectRefcounts(json, out);
	return out;
}

/** While alive, counts the jansson blocks one thread allocates and frees. */
struct JsonAllocationProbe {
	static inline std::mutex mutex;
	static inline std::unordered_set<void*> blocks;
	static inline std::thread::id thread;
	static inline json_malloc_t previousMalloc = nullptr;
	static inline json_free_t previousFree = nullptr;
	static inline bool active = false;

	static void* countingMalloc(size_t size) {
		void* block = previousMalloc(size);
		if (block && std::this_thread::get_id() == thread) {
			std::lock_guard<std::mutex> lock(mutex);
			blocks.insert(block);
		}
		return block;
	}
	static void countingFree(void* block) {
		if (block) {
			std::lock_guard<std::mutex> lock(mutex);
			blocks.erase(block);
		}
		previousFree(block);
	}

	JsonAllocationProbe() {
		if (active)
			throw std::logic_error("json_allocations cannot be nested");
		active = true;
		json_get_alloc_funcs(&previousMalloc, &previousFree);
		thread = std::this_thread::get_id();
		blocks.clear();
		json_set_alloc_funcs(countingMalloc, countingFree);
	}
	~JsonAllocationProbe() {
		// Blocks counted here and freed later go back to the allocator that made them.
		json_set_alloc_funcs(previousMalloc, previousFree);
		active = false;
	}
	size_t live() {
		std::lock_guard<std::mutex> lock(mutex);
		return blocks.size();
	}
};

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
		config(1, 1, 1, 1);
		configParam(0, 0.f, 1.f, 0.5f, "Probe");
		token = std::make_shared<ObjectToken>(bindCurrentContext());
	}
	~ProbeModule() override {
		token->invalidate();  // the only thing deletion does for Python
	}
	void process(const ProcessArgs& args) override {
		processed.fetch_add(1, std::memory_order_relaxed);
		// Engine-side writes the component tests observe from the UI thread.
		outputs[0].setVoltage(params[0].getValue() * 10.f);
		lights[0].setBrightness(params[0].getValue());
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
	m.def("probe_components", [](const ModuleHandle& h) {
		// Rack-owned components, borrowed with the module's token (RP-13; RP-14 hands these out publicly).
		ProbeModule& pm = live(h);
		return py::make_tuple(bindings::borrowParam(&pm.params[0], h.token), bindings::borrowInput(&pm.inputs[0], h.token),
			bindings::borrowOutput(&pm.outputs[0], h.token), bindings::borrowLight(&pm.lights[0], h.token));
	});
	// RP-14B2a: independent native quantity results from actual Rack's Engine.
	m.def("quantity_reference", [] {
		requireMainThread("quantity_reference");
		auto context = liveContext();
		std::unique_ptr<json_t, decltype(&json_decref)> result(nullptr, json_decref);
		{
			py::gil_scoped_release release;
			EngineLease lease(*context);
			result.reset(quantityReference());
		}
		return fromJson(result.get());
	});
	// RP-14A: superclass dispatch off the engine, not a public callback mapping.
	m.def("module_process_bypass", [](const bindings::ModuleRef& r) {
		r.get().processBypass({48000.f, 1.f / 48000.f, 0});
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
	// RP-25: Rack-owned parents for managed Python widgets, built as Rack builds them.
	m.def("add_probe_module", [] {
		// As the module browser adds a module: Module, engine, ModuleWidget, rack.
		requireMainThread("add_probe_module");
		refuseEngineReentry("add_probe_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		ProbeModule* pm = static_cast<ProbeModule*>(model->createModule());
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			APP->engine->addModule(pm);
		}
		APP->scene->rack->addModuleAtMouse(model->createModuleWidget(pm));
		return ProbeModule::handleFor(pm);
	});
	m.def("module_widget_add_child", [](const ModuleHandle& h, py::object child) {
		requireMainThread("module_widget_add_child");
		ProbeModule& pm = live(h);
		if (!pm.widget)
			throw Ownership("this module has no widget to attach to");
		widgets::transferToHost(*pm.widget, child);
	});
	m.def("remove_probe_module", [](const ModuleHandle& h) {
		// As ModuleWidget::removeAction, without history: the rack lets go, then
		// ~ModuleWidget deletes its children before the engine removes the module.
		requireMainThread("remove_probe_module");
		refuseEngineReentry("remove_probe_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		ProbeWidget* mw = live(h).widget;
		if (!mw)
			throw Ownership("this module has no widget to remove");
		if (mw == driver)
			throw Ownership("the module running the tests cannot remove itself");
		// Cable removal and module deletion take the engine lock: release the GIL.
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		APP->scene->rack->removeModule(mw);
		delete mw;
	});
	m.def("render_browser_preview", [](py::object child) {
		// As the module browser's ModelBox::createPreview, then the ModelBox's
		// deletion: a null-module ModuleWidget in a zoomed framebuffer, stepped once.
		requireMainThread("render_browser_preview");
		liveContext();
		std::unique_ptr<rack::widget::Widget> preview(new rack::widget::TransparentWidget);
		auto* zoom = new rack::widget::ZoomWidget;
		preview->addChild(zoom);
		auto* fb = new rack::widget::FramebufferWidget;
		if (APP->window->pixelRatio < 2.0)
			fb->oversample = 2.0;
		zoom->addChild(fb);
		auto* container = new rack::widget::Widget;
		fb->addChild(container);
		rack::app::ModuleWidget* mw = model->createModuleWidget(nullptr);
		container->addChild(mw);
		container->box.size = mw->box.size;
		// The browser draws fb through the ZoomWidget; render() directly needs fb's own clip box.
		fb->box.size = mw->box.size;
		widgets::transferToHost(*mw, child);
		mw->step();
		zoom->setZoom(0.5f);
		fb->render();
	});
	m.def("recreate_graphics_context", [] {
		// As a host that recreates its window, which Rack Free never does: Rack's
		// own ContextDestroy and ContextCreate events, through the whole scene.
		requireMainThread("recreate_graphics_context");
		liveContext();
		NVGcontext* vg = APP->window->vg;
		rack::widget::Widget::ContextDestroyEvent destroyed;
		destroyed.vg = vg;
		APP->scene->onContextDestroy(destroyed);
		rack::widget::Widget::ContextCreateEvent created;
		created.vg = vg;
		APP->scene->onContextCreate(created);
	});
	m.def("framebuffer_pixels", [](py::object frame) {
		// The RGBA bytes of a FramebufferWidget's last render, top row first, read back from
		// its GL framebuffer (NanoVG draws y down; GL rows run bottom up). Premultiplied alpha.
		requireMainThread("framebuffer_pixels");
		liveContext();
		auto* fbw = dynamic_cast<rack::widget::FramebufferWidget*>(widgets::argument(frame));
		if (!fbw)
			throw std::invalid_argument("framebuffer_pixels needs a FramebufferWidget");
		if (!fbw->getFramebuffer())
			throw std::invalid_argument("framebuffer_pixels needs a rendered FramebufferWidget (size " +
				std::to_string(fbw->getFramebufferSize().x) + " x " + std::to_string(fbw->getFramebufferSize().y) + ")");
		rack::math::Vec size = fbw->getFramebufferSize();
		int w = int(size.x), h = int(size.y);
		std::vector<unsigned char> rows(size_t(w) * h * 4);
		GLint previous = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
		nvgluBindFramebuffer(fbw->getFramebuffer());
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
		glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
		std::string out(rows.size(), '\0');
		for (int y = 0; y < h; ++y)
			std::copy_n(rows.data() + size_t(h - 1 - y) * w * 4, size_t(w) * 4, out.begin() + size_t(y) * w * 4);
		return py::make_tuple(w, h, py::bytes(out));
	});
	m.def("window_should_close", [] {
		requireMainThread("window_should_close");
		liveContext();
		return bool(glfwWindowShouldClose(APP->window->win));
	});
	m.def("cancel_window_close", [] {
		// Undoes Window::close() before the frame ends, so a test of it leaves Rack running.
		requireMainThread("cancel_window_close");
		liveContext();
		glfwSetWindowShouldClose(APP->window->win, GLFW_FALSE);
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
	// RP-16A: what Rack itself does with a registered model, so tests drive
	// Python models through Rack's own code paths.
	auto findModel = [](const std::string& pluginSlug, const std::string& modelSlug) {
		rack::plugin::Model* found = rack::plugin::getModel(pluginSlug, modelSlug);
		if (!found)
			throw std::invalid_argument("no model " + pluginSlug + "/" + modelSlug);
		return found;
	};
	m.def("browser_add_module", [findModel](const std::string& pluginSlug, const std::string& modelSlug) {
		// As Browser's chooseModel: Module, engine, ModuleWidget, rack, an undoable ModuleAdd.
		requireMainThread("browser_add_module");
		refuseEngineReentry("browser_add_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		rack::plugin::Model* found = findModel(pluginSlug, modelSlug);
		rack::engine::Module* module = found->createModule();
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			APP->engine->addModule(module);
		}
		rack::app::ModuleWidget* mw = found->createModuleWidget(module);
		APP->scene->rack->addModuleAtMouse(mw);
		auto* add = new rack::history::ModuleAdd;
		add->name = "add module";
		add->setModule(mw);
		APP->history->push(add);
		return module->id;
	}, py::arg("plugin_slug"), py::arg("model_slug"));
	m.def("remove_module_widget", [](int64_t id) {
		// As the module's context menu "Delete": ModuleWidget::removeAction, undoable.
		requireMainThread("remove_module_widget");
		refuseEngineReentry("remove_module_widget");
		liveContext();
		rack::app::ModuleWidget* mw = APP->scene->rack->getModule(id);
		if (!mw)
			throw std::invalid_argument("no module widget with that ID");
		py::gil_scoped_release release;
		mw->removeAction();
	});
	m.def("history_undo", [] {
		requireMainThread("history_undo");
		refuseEngineReentry("history_undo");
		liveContext();
		py::gil_scoped_release release;
		APP->history->undo();
	});
	m.def("model_preview", [findModel](const std::string& pluginSlug, const std::string& modelSlug) {
		// As the browser's ModelBox preview: a module widget without a module, drawn, then deleted.
		requireMainThread("model_preview");
		liveContext();
		rack::plugin::Model* found = findModel(pluginSlug, modelSlug);
		std::unique_ptr<rack::app::ModuleWidget> mw(found->createModuleWidget(nullptr));
		std::vector<std::string> labels;
		for (auto* child : mw->children)
			if (auto* label = dynamic_cast<rack::ui::Label*>(child))
				labels.push_back(label->text);
		bool hasModule = mw->module != nullptr;
		return py::make_tuple(mw->box.size.x, mw->box.size.y, labels, hasModule, mw->model == found);
	}, py::arg("plugin_slug"), py::arg("model_slug"));
	m.def("press_key", [](int key, int mods) {
		// A key press and release through Rack's own input dispatcher, as GLFW delivers them.
		requireMainThread("press_key");
		refuseEngineReentry("press_key");
		liveContext();
		rack::math::Vec pos = APP->scene->box.size.div(2);
		py::gil_scoped_release release;
		APP->event->handleKey(pos, key, 0, GLFW_PRESS, mods);
		APP->event->handleKey(pos, key, 0, GLFW_RELEASE, mods);
	}, py::arg("key"), py::arg("mods") = 0);
	// RP-15D: the plugin settings hooks exactly as Rack's loader calls them.
	m.def("settings_to_json_as_rack", []() -> py::object {
		json_t* json;
		{
			py::gil_scoped_release release;
			json = rackpy::bindings::settingsToJson();
		}
		if (!json)
			return py::none();
		std::unique_ptr<json_t, decltype(&json_decref)> owner(json, json_decref);
		return py::make_tuple(fromJson(json));
	}, "None when Rack would get NULL, else a 1-tuple with the JSON Rack would store.");
	m.def("settings_from_json_as_rack", [](py::handle value) {
		std::unique_ptr<json_t, decltype(&json_decref)> json(toJson(value), json_decref);
		py::gil_scoped_release release;
		rackpy::bindings::settingsFromJson(json.get());
	});
	m.def("module_model", [](int64_t id) -> py::object {
		requireMainThread("module_model");
		std::shared_ptr<ContextToken> ctx = liveContext();
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		rack::engine::Module* module = APP->engine->getModule(id);
		if (!module) {
			py::gil_scoped_acquire gil;
			return py::none();
		}
		bool placeholder = dynamic_cast<rackpy::plugin::PlaceholderModule*>(module) != nullptr;
		std::string slug = module->model ? module->model->slug : "";
		py::gil_scoped_acquire gil;
		return py::make_tuple(slug, placeholder);
	}, py::arg("module_id"));
	m.def("leak_reference", [](py::object o) {
		if (!leaked)
			leaked = new std::vector<PyRef>();
		leaked->emplace_back(std::move(o));
	});
	m.def("record_diagnostic", [](const std::string& message) { diagnose(DiagnosticCode::Test, message.c_str()); });

	// Family 1 (RP-12): paths only Rack can take.
	m.def("throw_rack_exception", [](const std::string& message) { throw rack::Exception("%s", message.c_str()); });
	m.def("quantity_string_as_rack", [](rack::Quantity& q) {
		// As a widget would: on the main thread, without the GIL, so Python overrides go through callPython.
		py::gil_scoped_release release;
		return q.getString();
	});
	m.def("read_quantity_on_rack_thread", [](rack::Quantity& q, int reads) {
		// As Core MIDI-Map would: on a thread with a Rack context, where Python must never run.
		float last = 0.f;
		{
			py::gil_scoped_release release;
			std::thread worker([&] {
				rack::contextSet(foreignContext());
				for (int i = 0; i < reads; i++)
					last = q.getValue() + q.getMinValue() + q.getMaxValue() + q.getDefaultValue() - q.getMinValue() -
						q.getMaxValue() - q.getDefaultValue();
			});
			worker.join();
		}
		return last;
	});
	m.def("metadata_as_rack", [](const bindings::ModuleRef& r, const std::string& kind, int i) {
		// As a port or light tooltip would: the module's own slot, on the main
		// thread without the GIL, so Python overrides go through callPython.
		auto& m = r.get();
		auto& infos = kind == "input" ? m.inputInfos : m.outputInfos;
		std::string name, description;
		if (kind == "light") {
			rack::engine::LightInfo* info = m.lightInfos.at(i);
			if (!info)
				throw std::invalid_argument("this light has no metadata");
			py::gil_scoped_release release;
			name = info->getName();
			description = info->getDescription();
		}
		else {
			rack::engine::PortInfo* info = infos.at(i);
			py::gil_scoped_release release;
			name = info->getFullName();
			description = info->getDescription();
		}
		return py::make_tuple(name, description);
	});
	m.def("read_slot_quantity_as_engine", [](const bindings::ModuleRef& r, int id, int reads) {
		// As Core MIDI-Map would: a module's own slot quantity, on a Rack
		// thread with the live context and without the GIL. Mixed reads only.
		rack::engine::ParamQuantity* q = r.get().paramQuantities.at(id);
		rack::Context* context = rack::contextGet();
		if (!q || !context || !context->engine)
			throw std::invalid_argument("needs a configured quantity and Rack's live context");
		float last = 0.f;
		{
			py::gil_scoped_release release;
			std::thread worker([&] {
				rack::contextSet(context);
				for (int i = 0; i < reads; i++)
					last = q->getScaledValue() + q->getMinValue() + q->getMaxValue() + q->getDefaultValue() -
						q->getMinValue() - q->getMaxValue() - q->getDefaultValue();
			});
			worker.join();
		}
		return last;
	});
	// RP-14C2: Rack's side of engine ownership, until RP-16's Model::createModule.
	m.def("engine_adopt", [](const bindings::ModuleRef& r) {
		requireMainThread("engine_adopt");
		refuseEngineReentry("engine_adopt");
		std::shared_ptr<ContextToken> ctx = liveContext();
		module::Module* m = r.state->releaseToRack();
		// Test-only model, so that Rack can serialize (autosave) the module.
		m->model = model;
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			APP->engine->addModule(m);
		}
		return m->id;
	});
	m.def("engine_delete", [](const bindings::ModuleRef& r, py::object removed) {
		// As ModuleWidget::setModule(NULL): remove from the engine, then delete.
		// `removed` runs in between, while the module still exists.
		requireMainThread("engine_delete");
		refuseEngineReentry("engine_delete");
		std::shared_ptr<ContextToken> ctx = liveContext();
		module::Module& m = r.get();
		if (!m.engineOwned)
			throw Ownership("Rack's engine does not own this module");
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			APP->engine->removeModule(&m);
		}
		if (!removed.is_none())
			removed();
		// Rack deletes on the main thread without the GIL; so do we.
		py::gil_scoped_release release;
		delete &m;
	}, py::arg("module"), py::arg("removed") = py::none());
	m.def("engine_handle", [](int64_t id) -> py::object {
		// Rack hands out Module pointers (ModuleWidget, getModule): find the handle again.
		requireMainThread("engine_handle");
		std::shared_ptr<ContextToken> ctx = liveContext();
		rack::engine::Module* found = nullptr;
		{
			py::gil_scoped_release release;
			EngineLease lease(*ctx);
			found = APP->engine->getModule(id);
		}
		auto* m = dynamic_cast<module::Module*>(found);
		if (!m)
			return py::none();
		return bindings::moduleObject(module::Construction::handleState(*m));
	});
	m.def("engine_has_module", [](int64_t id) {
		requireMainThread("engine_has_module");
		std::shared_ptr<ContextToken> ctx = liveContext();
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		return APP->engine->getModule(id) != nullptr;
	});
	m.def("engine_reset", [](const bindings::ModuleRef& r) {
		requireMainThread("engine_reset");
		refuseEngineReentry("engine_reset");
		std::shared_ptr<ContextToken> ctx = liveContext();
		module::Module& m = r.get();
		if (!m.engineOwned)
			throw Ownership("Rack's engine does not own this module");
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		APP->engine->resetModule(&m);
	});
	// RP-14C3: the engine operations that dispatch lifecycle events.
	auto engineCall = [](const char* what, auto action) {
		requireMainThread(what);
		refuseEngineReentry(what);
		std::shared_ptr<ContextToken> ctx = liveContext();
		py::gil_scoped_release release;
		EngineLease lease(*ctx);
		return action();
	};
	auto owned = [](const bindings::ModuleRef& r) -> module::Module& {
		module::Module& m = r.get();
		if (!m.engineOwned)
			throw Ownership("Rack's engine does not own this module");
		return m;
	};
	m.def("engine_randomize", [engineCall, owned](const bindings::ModuleRef& r) {
		module::Module& mod = owned(r);
		engineCall("engine_randomize", [&] { APP->engine->randomizeModule(&mod); });
	});
	m.def("engine_bypass", [engineCall, owned](const bindings::ModuleRef& r, bool bypassed) {
		module::Module& mod = owned(r);
		engineCall("engine_bypass", [&] { APP->engine->bypassModule(&mod, bypassed); });
	});
	m.def("engine_sample_rate", [engineCall] {
		return engineCall("engine_sample_rate", [] { return APP->engine->getSampleRate(); });
	});
	m.def("engine_set_sample_rate", [engineCall](float rate) {
		engineCall("engine_set_sample_rate", [&] { APP->engine->setSuggestedSampleRate(rate); });
	});
	m.def("engine_set_master", [engineCall, owned](py::object module) {
		module::Module* mod = module.is_none() ? nullptr : &owned(module.cast<const bindings::ModuleRef&>());
		engineCall("engine_set_master", [&] { APP->engine->setMasterModule(mod); });
	});
	m.def("engine_prepare_save", [engineCall] { engineCall("engine_prepare_save", [] { APP->engine->prepareSave(); }); });
	m.def("engine_connect", [engineCall, owned](const bindings::ModuleRef& out, int outId, const bindings::ModuleRef& in, int inId) {
		auto* cable = new rack::engine::Cable;
		cable->outputModule = &owned(out);
		cable->outputId = module::Construction::index(outId, cable->outputModule->outputs.size());
		cable->inputModule = &owned(in);
		cable->inputId = module::Construction::index(inId, cable->inputModule->inputs.size());
		engineCall("engine_connect", [&] { APP->engine->addCable(cable); });
		return cable->id;
	});
	m.def("engine_disconnect", [engineCall](int64_t id) {
		engineCall("engine_disconnect", [&] {
			rack::engine::Cable* cable = APP->engine->getCable(id);
			if (!cable)
				throw std::invalid_argument("no such cable");
			APP->engine->removeCable(cable);
			delete cable;
		});
	});
	// Rack's own save and reload of one module (Module::toJson/fromJson), for
	// the RP-14 check that configuration and state survive. Python JSON
	// overrides are RP-15.
	m.def("engine_module_to_json", [engineCall, owned](const bindings::ModuleRef& r) {
		module::Module& mod = owned(r);
		json_t* json = engineCall("engine_module_to_json", [&] { return APP->engine->moduleToJson(&mod); });
		std::unique_ptr<json_t, decltype(&json_decref)> owner(json, json_decref);
		return fromJson(json);
	});
	m.def("engine_module_from_json", [engineCall, owned](const bindings::ModuleRef& r, py::handle data) {
		module::Module& mod = owned(r);
		std::unique_ptr<json_t, decltype(&json_decref)> json(toJson(data), json_decref);
		engineCall("engine_module_from_json", [&] { APP->engine->moduleFromJson(&mod, json.get()); });
	});
	// RP-15C: a module's JSON as text, the way Rack writes a patch (json_dumpf
	// with JSON_INDENT(2)) and reads one (json_loadf with no flags), so tests
	// reach the boundary with JSON that Python's own values cannot express.
	m.def("engine_module_to_json_text", [engineCall, owned](const bindings::ModuleRef& r) {
		module::Module& mod = owned(r);
		json_t* json = engineCall("engine_module_to_json_text", [&] { return APP->engine->moduleToJson(&mod); });
		std::unique_ptr<json_t, decltype(&json_decref)> owner(json, json_decref);
		char* text = json_dumps(json, JSON_INDENT(2));
		if (!text)
			throw std::runtime_error("jansson could not write the module's JSON");
		std::string result(text);
		json_malloc_t unusedMalloc;
		json_free_t free;
		json_get_alloc_funcs(&unusedMalloc, &free);  // json_dumps allocates with jansson's allocator
		free(text);
		return result;
	});
	m.def("rack_parses_json_text", [](const std::string& text) {
		json_error_t error;
		std::unique_ptr<json_t, decltype(&json_decref)> json(json_loadb(text.data(), text.size(), 0, &error), json_decref);
		return py::make_tuple(json != nullptr, std::string(error.text));
	});
	m.def("engine_module_from_json_text", [engineCall, owned](const bindings::ModuleRef& r, const std::string& text,
		std::optional<py::bytes> invalidUtf8Data) {
		module::Module& mod = owned(r);
		json_error_t error;
		std::unique_ptr<json_t, decltype(&json_decref)> json(json_loadb(text.data(), text.size(), 0, &error), json_decref);
		if (!json)
			throw py::value_error(std::string("Rack would refuse this patch: ") + error.text);
		if (invalidUtf8Data) {
			// Bytes jansson's loader would refuse, as a buggy C++ plugin's json_string_nocheck could produce.
			std::string bytes = *invalidUtf8Data;
			json_object_set_new(json.get(), "data", json_stringn_nocheck(bytes.data(), bytes.size()));
		}
		engineCall("engine_module_from_json_text", [&] { APP->engine->moduleFromJson(&mod, json.get()); });
	}, py::arg("module"), py::arg("text"), py::arg("invalid_utf8_data") = py::none());
	// RP-15C: jansson's references. json_refcounts converts a value and reports
	// every non-singleton node's reference count, preorder; json_borrowed_refcounts
	// parses text, converts the borrowed tree to Python and reports the counts
	// before and after.
	m.def("json_refcounts", [](py::handle value) {
		std::unique_ptr<json_t, decltype(&json_decref)> json(toJson(value), json_decref);
		return refcounts(json.get());
	});
	m.def("json_borrowed_refcounts", [](const std::string& text) {
		json_error_t error;
		std::unique_ptr<json_t, decltype(&json_decref)> json(json_loadb(text.data(), text.size(), 0, &error), json_decref);
		if (!json)
			throw py::value_error(error.text);
		std::vector<size_t> before = refcounts(json.get());
		py::object value = fromJson(json.get());
		return py::make_tuple(before, refcounts(json.get()), value);
	});
	// RP-15C: how many jansson blocks the main thread allocated while `f` ran
	// and had not freed when it returned. The counting allocator wraps the one
	// installed (json_set_alloc_funcs); blocks other threads allocate, and
	// blocks from before, are not counted.
	m.def("leak_json_integer", [] { (void) json_integer(1); });  // the probe's positive control
	m.def("json_allocations", [](py::function f) {
		requireMainThread("json_allocations");
		JsonAllocationProbe probe;
		f();
		return probe.live();
	});
	m.def("seed_random", [](uint64_t a, uint64_t b) {
		requireMainThread("seed_random");
		rack::random::local().seed(a, b);
	});
	// Rack's own superclass behaviour on a plain rack::engine::Module, for the
	// lifecycle adapters to match: reset after edits, then a seeded randomize.
	m.def("superclass_reference", [engineCall](uint64_t seed) {
		// A final, empty subclass: Rack's behaviour, and nothing for GCC to
		// speculatively devirtualize the deletion into.
		struct Plain final : rack::engine::Module {};
		auto* plain = new Plain;
		plain->config(4, 0, 0, 0);
		plain->configParam(0, 0.f, 10.f, 2.f);
		plain->configSwitch(1, 0.f, 3.f, 1.f, "Mode", {"A", "B", "C", "D"});
		plain->configButton(2);
		plain->configParam(3, -5.f, 5.f, 0.f)->randomizeEnabled = false;
		std::vector<float> reset, randomized;
		engineCall("superclass_reference", [&] {
			APP->engine->addModule(plain);
			for (auto& p : plain->params)
				p.setValue(1.f);
			APP->engine->resetModule(plain);
			for (auto& p : plain->params)
				reset.push_back(p.getValue());
			rack::random::local().seed(seed, 0);
			APP->engine->randomizeModule(plain);
			for (auto& p : plain->params)
				randomized.push_back(p.getValue());
			APP->engine->removeModule(plain);
			delete plain;
		});
		return py::make_tuple(reset, randomized);
	});
	m.def("finalize_outcome", &finalizeOutcome);

	// Drop the hooks' own callback references when the plugin is destroyed (but not the deliberate leaks).
	onFinalize([] {
		stepCallback.reset();
		resetCallback.reset();
	});
}

namespace {

/** RP-17A test kernel. Outputs: 0 counts frames, 1 is the sample rate in kHz,
2 counts resets. Fails once `fail_after` frames have run, if it is positive. */
struct ProbeKernel : rackpy::Kernel {
	float frames = 0, resets = 0, rate = 0;
	double failAfter;
	explicit ProbeKernel(const std::vector<double>& v) : failAfter(v[0]) {}
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		frames += 1;
		m.outputs[0].setVoltage(frames);
		m.outputs[1].setVoltage(rate / 1000.f);
		m.outputs[2].setVoltage(resets);
		return !(failAfter > 0 && frames >= failAfter);
	}
	void reset() noexcept override { resets += 1; }
	void sampleRateChange(float sampleRate, float) noexcept override { rate = sampleRate; }
	void setControl(int slot, double value) noexcept override {
		if (slot == 0)
			failAfter = value;
	}
};

}  // namespace

void registerModels(rack::plugin::Plugin* plugin) {
	if (!rackpy::dsp::findType("rackpy_test.Probe"))
		rackpy::dsp::registerType(rackpy::KernelType{
			"rackpy_test.Probe",
			"Test hook: counts frames, reports the sample rate and resets, can fail on purpose.",
			{{"fail_after", rackpy::Field::NUMBER, 0, false, "fail once this many frames have run (0: never)"}},
			{"fail_after"},
			[](const std::vector<double>& v) { return std::make_unique<ProbeKernel>(v); },
		});
	model = rack::createModel<ProbeModule, ProbeWidget>("RuntimeProbe");
	plugin->addModel(model);
}

}  // namespace testing
}  // namespace rackpy
