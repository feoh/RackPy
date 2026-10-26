// SPDX-License-Identifier: GPL-3.0-or-later
// rackpy.raw.app.CableWidget (RP-28E): cable widgets Python makes, and their
// engine cables.
//
// Rack keeps one rule this file must not break: no engine cable without a
// cable widget may touch a module that has a module widget, because Rack
// asserts as it deletes such a module (RP-24B). So a Python cable widget
// changes Rack's engine only while it is in Rack's rack, as in Rack's own
// drag flow: update_cable and set_cable act there, and release_cable and
// RackWidget.remove_cable also take the engine cable out.
//
// A cable widget lives only in Rack's cable container: its Add and Remove
// handlers move its two plugs in and out of Rack's plug container, and Rack
// asserts as it deletes a plug that is still there. So Rack's dispatch alone
// sends those events (a Python override's super() call reaches Rack's code
// then), and other parents are refused (widget/managed.cpp).
//
// Its port fields are raw pointers. In the rack, Rack keeps them right (a
// module's removal disconnects its cables first); while the widget is
// detached, RackPy checks the ports Python set before each use.
#include <pybind11/pybind11.h>

#include <array>
#include <memory>
#include <string>

#include <rack.hpp>

#include "../engine/engine.hpp"
#include "../history/history.hpp"
#include "app.hpp"
#include "app_internal.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/json.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

namespace {

namespace app = rack::app;
using CW = app::CableWidget;
using rack::engine::Port;

const CW* dispatching = nullptr;  // the cable widget whose Add or Remove Rack is dispatching

struct DispatchScope {
	const CW* previous;
	explicit DispatchScope(const CW* w) : previous(dispatching) { dispatching = w; }
	~DispatchScope() { dispatching = previous; }
};

struct CableWidgetT : Widget<CW> {
	// The ports Python set, checked while the widget is detached.
	std::array<Target, 4> tracked;
	void onAdd(const AddEvent& e) override {
		DispatchScope scope(this);
		Widget<CW>::onAdd(e);
	}
	void onRemove(const RemoveEvent& e) override {
		DispatchScope scope(this);
		Widget<CW>::onRemove(e);
	}
};

app::PortWidget** field(CW& cw, int i) {
	switch (i) {
		case 0: return &cw.inputPort;
		case 1: return &cw.outputPort;
		case 2: return &cw.hoveredInputPort;
		default: return &cw.hoveredOutputPort;
	}
}

bool inRack(const CW& cw) {
	auto* c = rack::contextGet();
	return c && c->scene && c->scene->rack && cw.parent == c->scene->rack->getCableContainer();
}

/** Clears a port field whose widget died while the cable widget was detached. */
CW& synced(py::handle self) {
	auto& cw = native<CW>(self);
	auto* t = dynamic_cast<CableWidgetT*>(&cw);
	if (!t)
		return cw;
	for (int i = 0; i < 4; ++i) {
		app::PortWidget*& port = *field(cw, i);
		Target& target = t->tracked[i];
		if (port != target.value) {
			// Rack set it (a drag in the rack): Rack keeps it right while the widget is there.
			target = inRack(cw) && port ? widgets::target(port) : Target{};
			continue;
		}
		if (!port)
			continue;
		try {
			target.get();
		}
		catch (const std::exception&) {
			port = nullptr;  // its module widget died meanwhile
			target = Target{};
		}
	}
	return cw;
}

void setPort(py::handle self, int i, py::handle value) {
	auto& cw = synced(self);
	auto* t = dynamic_cast<CableWidgetT*>(&cw);
	if (value.is_none()) {
		*field(cw, i) = nullptr;
		if (t)
			t->tracked[i] = Target{};
		return;
	}
	auto* port = dynamic_cast<app::PortWidget*>(argument(value));
	if (!port)
		throw py::type_error("expected a port widget (rackpy.raw.app.PortWidget, or Rack's)");
	Port::Type wanted = (i % 2 == 0) ? Port::INPUT : Port::OUTPUT;
	if (port->type != wanted)
		throw std::invalid_argument(wanted == Port::INPUT ? "an input port belongs on the input end"
														   : "an output port belongs on the output end");
	if (!widgets::portCableSafe(*port) || !port->module)
		throw runtime::Ownership("Rack cables only ports with a module that their module widget's factory made (or "
			"Rack's own)");
	*field(cw, i) = port;
	if (t)
		t->tracked[i] = widgets::target(port);
}

CW& inTheRack(py::handle self, const char* what) {
	auto& cw = synced(self);
	if (!inRack(cw))
		throw runtime::Ownership(std::string(what) + " changes Rack's engine, so the cable widget must be in Rack's rack "
			"(RackWidget.add_cable) first: a cable outside it would outlive its modules");
	return cw;
}

/** Rack's updateCable needs both ends in Rack's rack, on modules in its engine. */
void requireCablePorts(CW& cw) {
	auto* rackWidget = rack::contextGet()->scene->rack;
	for (app::PortWidget* port : {cw.inputPort, cw.outputPort}) {
		if (!port)
			continue;
		auto* mw = port->getAncestorOfType<app::ModuleWidget>();
		if (!mw || !mw->module || rackWidget->getModule(mw->module->id) != mw || mw->module != port->module)
			throw runtime::Ownership("a cable's ports must be on module widgets in Rack's rack");
	}
}

}  // namespace

bool isPythonCableWidget(const rack::widget::Widget* w) {
	return dynamic_cast<const CableWidgetT*>(w) != nullptr;
}

void bindCableWidget(py::module_ mod) {
	auto cls = variant<CW, W, CableWidgetT>(mod, "CableWidget");
	cls.doc() = "Rack's cable widget, made by Python: set its ports, put it in Rack's rack (RackWidget.add_cable), then "
		"update_cable or set_cable makes its engine cable, as Rack's cable dragging does. Its plugs come with it.";
	cls.def_property("color", [](py::object self) { return native<CW>(self).color; },
			[](py::object self, NVGcolor c) { native<CW>(self).color = c; })
		.def_property_readonly("input_plug", [](py::object self) { return wrapper(native<CW>(self).inputPlug); },
			"Rack's plug at the input end, which the widget owns (a HostPlugWidget).")
		.def_property_readonly("output_plug", [](py::object self) { return wrapper(native<CW>(self).outputPlug); })
		.def_property("input_port", [](py::object self) { return wrapper(synced(self).inputPort); },
			[](py::object self, py::object port) { setPort(self, 0, port); },
			"Rack's: the input port widget (cable-safe ports only), or None.")
		.def_property("output_port", [](py::object self) { return wrapper(synced(self).outputPort); },
			[](py::object self, py::object port) { setPort(self, 1, port); })
		.def_property("hovered_input_port", [](py::object self) { return wrapper(synced(self).hoveredInputPort); },
			[](py::object self, py::object port) { setPort(self, 2, port); }, "Rack's: the port a dragged end is over.")
		.def_property("hovered_output_port", [](py::object self) { return wrapper(synced(self).hoveredOutputPort); },
			[](py::object self, py::object port) { setPort(self, 3, port); })
		.def_property_readonly("cable", [](py::object self) -> py::object {
			auto& cw = synced(self);
			return cw.cable ? bindings::engineCableHandle(cw.cable->id) : py::none();
		}, "Rack's: the engine cable the widget owns (a Cable found by ID), or None.")
		.def("is_complete", [](py::object self) { return synced(self).isComplete(); })
		.def("update_cable", [](py::object self) {
			checkMutation();
			auto& cw = inTheRack(self, "update_cable");
			requireCablePorts(cw);
			py::gil_scoped_release release;
			cw.updateCable();
		}, "Rack's: replaces the engine cable with one between the widget's ports (none if an end is missing). In "
			"Rack's rack only.")
		.def("set_cable", [](py::object self, py::object cable) {
			checkMutation();
			auto& cw = synced(self);
			if (cable.is_none()) {
				py::gil_scoped_release release;
				cw.setCable(nullptr);  // Rack's: removes and deletes the engine cable
				return;
			}
			inTheRack(self, "set_cable");
			auto* rackWidget = rack::contextGet()->scene->rack;
			rack::engine::Cable* raw = bindings::engineCableForWidget(cable, [&](const rack::engine::Cable& c) {
				// What Rack's setCable needs, or it raises after the engine has the cable.
				auto* out = rackWidget->getModule(c.outputModule->id);
				auto* in = rackWidget->getModule(c.inputModule->id);
				if (!out || !in)
					throw runtime::Ownership("both of the cable's modules need module widgets in Rack's rack");
				auto* outPort = out->getOutput(c.outputId);
				auto* inPort = in->getInput(c.inputId);
				if (!outPort || !inPort || !widgets::portCableSafe(*outPort) || !widgets::portCableSafe(*inPort))
					throw runtime::Ownership("the cable's ports need port widgets (made by their module widgets' factories)");
			});
			try {
				py::gil_scoped_release release;
				cw.setCable(raw);  // Rack's: adopts it and takes its ports
			}
			catch (...) {
				bindings::undoEngineCableForWidget(cable, raw);
				throw;
			}
			if (auto* t = dynamic_cast<CableWidgetT*>(&cw)) {
				t->tracked[0] = widgets::target(cw.inputPort);
				t->tracked[1] = widgets::target(cw.outputPort);
			}
		}, py::arg("cable"),
			"Rack's: the widget adopts a cable Python made (RackPy adds it to Rack's engine first, as Rack requires) "
			"and takes its ports from it; None removes and deletes the widget's engine cable. In Rack's rack only.")
		.def("get_cable", [](py::object self) -> py::object {
			auto& cw = synced(self);
			return cw.getCable() ? bindings::engineCableHandle(cw.getCable()->id) : py::none();
		})
		.def("release_cable", [](py::object self) -> py::object {
			checkMutation();
			auto& cw = synced(self);
			rack::engine::Cable* raw = cw.releaseCable();
			if (!raw)
				return py::none();
			auto token = runtime::binding().current();
			{
				py::gil_scoped_release release;
				runtime::EngineLease lease(*token);
				token->context->engine->removeCable(raw);
			}
			return bindings::pythonOwnedCable(raw);
		}, "Rack's: the widget gives up its engine cable. RackPy also takes it out of Rack's engine (a cable without a "
			"widget between module widgets would crash Rack when a module goes) and Python owns it.")
		.def("get_plug", [](py::object self, Port::Type type) { return wrapper(native<CW>(self).getPlug(type)); },
			py::arg("type"))
		.def("get_port", [](py::object self, Port::Type type) { return wrapper(synced(self).getPort(type)); }, py::arg("type"))
		.def("get_hovered_port", [](py::object self, Port::Type type) { return wrapper(synced(self).getHoveredPort(type)); },
			py::arg("type"))
		.def("get_input_pos", [](py::object self) { return synced(self).getInputPos(); },
			"Rack's: the input end in the rack's coordinates (the mouse when no port holds it).")
		.def("get_output_pos", [](py::object self) { return synced(self).getOutputPos(); })
		.def("merge_json", [](py::object self, py::handle data) {
			std::unique_ptr<json_t, decltype(&json_decref)> root(runtime::toJson(data), json_decref);
			native<CW>(self).mergeJson(root.get());
			return runtime::fromJson(root.get());
		}, py::arg("data"), "Rack's: the cable's JSON with its colour added; returns the result.")
		.def("from_json", [](py::object self, py::handle data) {
			std::unique_ptr<json_t, decltype(&json_decref)> root(runtime::toJson(data), json_decref);
			native<CW>(self).fromJson(root.get());
		}, py::arg("data"), "Rack's: the colour from the cable's JSON (Rack's next colour if it has none).");
	// Rack's Add and Remove handlers move the plugs; only Rack's dispatch may run them.
	cls.attr("on_add") = py::cpp_function([](py::object self, py::object event) {
		auto& cw = native<CW>(self);
		if (dispatching != &cw)
			throw runtime::Ownership("Rack sends a cable widget's Add event as the rack takes it (RackWidget.add_cable)");
		auto data = eventData(event);
		cw.CW::onAdd(nativeEvent<rack::widget::Widget::AddEvent>(data, "AddEvent"));
	}, py::name("on_add"), py::is_method(cls), py::arg("event"),
		"Rack's: puts the plugs in Rack's plug container. Only during Rack's dispatch (a subclass's super() call).");
	cls.attr("on_remove") = py::cpp_function([](py::object self, py::object event) {
		auto& cw = native<CW>(self);
		if (dispatching != &cw)
			throw runtime::Ownership("Rack sends a cable widget's Remove event as the rack lets it go");
		auto data = eventData(event);
		cw.CW::onRemove(nativeEvent<rack::widget::Widget::RemoveEvent>(data, "RemoveEvent"));
	}, py::name("on_remove"), py::is_method(cls), py::arg("event"),
		"Rack's: takes the plugs out of Rack's plug container. Only during Rack's dispatch.");
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
