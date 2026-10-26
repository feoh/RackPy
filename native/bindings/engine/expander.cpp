// SPDX-License-Identifier: GPL-3.0-or-later
// rack::engine::Module::Expander -> rackpy.raw.engine (RP-23A): expander views,
// RackPy expander messages and opaque foreign message pointers.
//
// Rack writes an expander's moduleId on the main thread and its module pointer
// on the engine thread (from moduleId, every block); its engine thread swaps
// the two message pointers and clears the flip flag at the end of a frame.
// So Python reads moduleId at any time, resolves `module` from it through
// Rack's engine while the engine runs the module, and touches the message
// pointers, their contents and the flip flag only while Rack's engine does not
// run the module (planning/rp-23-slices.md). A message pointer is a RackPy
// ExpanderMessage only if the registry knows its address (native/engine/
// expanders.hpp); any other pointer is an opaque ForeignMessage and is never
// dereferenced.
#include "expander.hpp"

#include <pybind11/stl.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../engine/expanders.hpp"
#include "engine.hpp"
#include "engine_internal.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
using Expander = rack::engine::Module::Expander;
using engine::MessageBuffer;

namespace {

/** The module an expander view belongs to, found again at every use. */
struct Target {
	RackModule* module;
	module::Module* own;  // the same module if RackPy made it, else null
	bool engineRuns;  // Rack's engine processes it (the message pointers and flag are the engine thread's)
};

/** A Module.Expander view: one side of a module's expanders. It never owns the module. */
struct ExpanderView {
	OwnerState rackpy;  // a RackPy module
	std::optional<EngineModule> foreign;  // or another plugin's
	uint8_t side;

	Target target() const {
		runtime::requireMainThread("using a module's expander");
		if (foreign) {
			RackModule& m = foreign->get();
			return {&m, nullptr, !foreign->pythonOwned()};
		}
		module::Module& m = lockOwner(rackpy)->get();
		return {&m, &m, m.engineOwned.load(std::memory_order_acquire)};
	}
	Expander& expander(const Target& t) const { return t.module->getExpander(side); }
};

/** A pointer that is not a RackPy message: identity and truth only, never read. */
struct ForeignMessage {
	const void* pointer;
};

const char* sideName(uint8_t side) {
	return side ? "right" : "left";
}

void refuseWhileEngineRuns(const Target& t, const char* what) {
	if (t.engineRuns)
		throw runtime::Ownership(std::string(what) + ": Rack's engine runs this module, and its engine thread reads and "
			"writes the message pointers and the flip flag every frame; native kernels exchange messages there, and "
			"Python only while the engine does not run the module");
}

/** A neighbour as Python sees it: a RackPy module's own handle (engine- or Python-owned), an
EngineModule for another plugin's, or None. */
py::object moduleHandle(RackModule* m) {
	if (auto* own = dynamic_cast<module::Module*>(m))
		return liveModuleObject(*own);
	return engineModuleHandle(m);
}

py::object messageObject(void* pointer) {
	if (!pointer)
		return py::none();
	if (MessageBuffer* buffer = engine::findMessage(pointer))
		return py::cast(buffer->shared_from_this());
	return py::cast(ForeignMessage{pointer});
}

/** Takes `buffer` out of module `m`'s keeping, if it is there and no slot of `m` still holds it. */
void release(module::Module& m, MessageBuffer* buffer) {
	if (!buffer || buffer->liveOwner() != &m)
		return;
	for (uint8_t side : {0, 1}) {
		const Expander& e = m.getExpander(side);
		if (e.producerMessage == buffer->data || e.consumerMessage == buffer->data)
			return;
	}
	buffer->owner = nullptr;
	buffer->ownerToken.reset();
	auto& held = m.expanderMessages;
	held.erase(std::remove_if(held.begin(), held.end(), [buffer](const auto& b) { return b.get() == buffer; }), held.end());
}

void assignMessage(const ExpanderView& v, bool producer, py::handle value) {
	Target t = v.target();
	const char* what = producer ? "producer_message" : "consumer_message";
	refuseWhileEngineRuns(t, what);
	if (!t.own)
		throw runtime::Ownership(std::string(what) + ": only RackPy modules hold RackPy expander messages; another "
			"plugin's module allocates its own");
	module::Module& m = *t.own;
	Expander& e = v.expander(t);
	void*& slot = producer ? e.producerMessage : e.consumerMessage;
	void* other = producer ? e.consumerMessage : e.producerMessage;
	if (value.is_none()) {
		MessageBuffer* old = engine::findMessage(slot);
		slot = nullptr;
		release(m, old);
		return;
	}
	if (!py::isinstance<MessageBuffer>(value))
		throw py::type_error(std::string(what) + " takes an ExpanderMessage or None: RackPy does not take raw pointers");
	std::shared_ptr<MessageBuffer> buffer = value.cast<std::shared_ptr<MessageBuffer>>();
	if (slot == buffer->data)
		return;
	if (other == buffer->data)
		throw py::value_error(std::string(what) + ": the other slot of this side already holds this message; Rack swaps "
			"the two, so each needs its own");
	if (MessageBuffer* pair = engine::findMessage(other))
		if (pair->schema != buffer->schema || pair->size != buffer->size)
			throw py::value_error(std::string(what) + ": the two messages of a side must have the same schema and size, "
				"as Rack swaps them (the other is " + pair->schema + "[" + std::to_string(pair->size) + "])");
	if (module::Module* owner = buffer->liveOwner())
		throw runtime::Ownership(std::string(what) + ": this message is already in " +
			(owner == &m ? "another expander slot of this module" : "another module's expander") + "; take it out first");
	MessageBuffer* old = engine::findMessage(slot);
	slot = buffer->data;
	buffer->owner = &m;
	buffer->ownerToken = m.token;
	m.expanderMessages.push_back(buffer);
	release(m, old);
}

/** A message whose contents Python may touch now: refused while Rack's engine runs the module holding it. */
MessageBuffer& accessible(MessageBuffer& b, const char* what) {
	runtime::requireMainThread("using an expander message");
	if (module::Module* owner = b.liveOwner())
		if (owner->engineOwned.load(std::memory_order_acquire))
			throw runtime::Ownership(std::string(what) + ": Rack's engine runs the module whose expander holds this "
				"message, and its engine thread writes and swaps it every frame");
	return b;
}

size_t index(const MessageBuffer& b, long long i) {
	long long n = (long long) b.size;
	if (i < 0)
		i += n;
	if (i < 0 || i >= n)
		throw py::index_error("index out of range (the message holds " + std::to_string(b.size) + " floats)");
	return (size_t) i;
}

py::object neighbour(const ExpanderView& v) {
	Target t = v.target();
	Expander& e = v.expander(t);
	if (!t.engineRuns)
		return moduleHandle(e.module);  // only Rack (or a laboratory Bench) sets it; never dangling here
	// Rack's engine thread writes the pointer: find the module by the ID instead, as Rack's engine does.
	int64_t id = e.moduleId;
	if (id < 0)
		return py::none();
	auto token = runtime::binding().current();
	if (!token || !token->alive() || !token->context->engine)
		return py::none();
	runtime::refuseEngineReentry("finding an expander's module");
	RackModule* found;
	{
		py::gil_scoped_release release;
		runtime::EngineLease lease(*token);
		found = token->context->engine->getModule(id);
	}
	return moduleHandle(found);
}

template <typename Class>
void bindAccessors(Class& cls, std::function<ExpanderView(py::object, uint8_t)> view) {
	cls.def_property_readonly("left_expander", [view](py::object self) { return view(self, 0); },
			"Rack's leftExpander: a view of the module's left expander (read-only; change its fields).")
		.def_property_readonly("right_expander", [view](py::object self) { return view(self, 1); },
			"Rack's rightExpander.")
		.def("get_left_expander", [view](py::object self) { return view(self, 0); })
		.def("get_right_expander", [view](py::object self) { return view(self, 1); })
		.def("get_expander", [view](py::object self, int side) {
			if (side < 0 || side > 255)
				throw py::value_error("get_expander: side is a uint8_t (0 to 255); 0 is left, any other value right");
			return view(self, side ? 1 : 0);
		}, py::arg("side"), "Rack's getExpander: the left expander for 0, the right one for any other value.");
}

}  // namespace

void bindExpanders(py::module_ engine, py::class_<ModuleRef>& module) {
	py::class_<MessageBuffer, std::shared_ptr<MessageBuffer>>(engine, "ExpanderMessage", py::is_final(),
		"A RackPy expander message buffer: `size` float32 values under a schema name, for one slot of a module's "
		"expander (producer_message or consumer_message). Allocated on the main thread; a module whose slot holds it "
		"keeps it alive.")
		.def(py::init([](const std::string& schema, int size) {
			runtime::requireMainThread("allocating an expander message");
			if (size < 1)
				throw py::value_error("an expander message holds at least 1 float");
			try {
				return engine::allocateMessage(schema, (size_t) size);
			}
			catch (const std::invalid_argument& e) {
				throw py::value_error(e.what());
			}
		}), py::arg("schema"), py::arg("size"))
		.def_property_readonly("schema", [](const MessageBuffer& b) { return b.schema; })
		.def_property_readonly("size", [](const MessageBuffer& b) { return b.size; })
		.def_property_readonly("owner", [](const MessageBuffer& b) {
			runtime::requireMainThread("using an expander message");
			module::Module* owner = b.liveOwner();
			return owner ? liveModuleObject(*owner) : py::none();
		}, "The RackPy module whose expander holds it, or None.")
		.def("__len__", [](const MessageBuffer& b) { return b.size; })
		.def("__getitem__", [](MessageBuffer& b, long long i) {
			return accessible(b, "reading a message").data[index(b, i)];
		}, py::arg("i"))
		.def("__setitem__", [](MessageBuffer& b, long long i, float x) {
			accessible(b, "writing a message").data[index(b, i)] = x;
		}, py::arg("i"), py::arg("x"))
		.def("tolist", [](MessageBuffer& b) {
			MessageBuffer& m = accessible(b, "reading a message");
			return std::vector<float>(m.data, m.data + m.size);
		}, "A copy of the values.")
		.def("write", [](MessageBuffer& b, const std::vector<float>& values) {
			MessageBuffer& m = accessible(b, "writing a message");
			if (values.size() != m.size)
				throw py::value_error("write: the message holds " + std::to_string(m.size) + " floats, not " +
					std::to_string(values.size()));
			std::copy(values.begin(), values.end(), m.data);
		}, py::arg("values"), "Sets every value.")
		.def("__repr__", [](const MessageBuffer& b) {
			return "<ExpanderMessage " + b.schema + "[" + std::to_string(b.size) + "]>";
		});

	py::class_<ForeignMessage>(engine, "ForeignMessage", py::is_final(),
		"A message pointer RackPy did not allocate (another plugin's): opaque. It compares equal to the same pointer "
		"and is never read, as only its owner knows what it points to.")
		.def("__bool__", [](const ForeignMessage&) { return true; })
		.def("__eq__", [](const ForeignMessage& a, py::handle b) {
			return py::isinstance<ForeignMessage>(b) && b.cast<const ForeignMessage&>().pointer == a.pointer;
		}, py::is_operator())
		.def("__hash__", [](const ForeignMessage& m) { return std::hash<const void*>()(m.pointer); })
		.def("__repr__", [](const ForeignMessage&) { return std::string("<ForeignMessage>"); });

	py::class_<ExpanderView>(module, "Expander", py::is_final(),
		"rack::engine::Module::Expander: one side of a module's message channel to its neighbour, as a view found "
		"again through the module at every use.")
		.def_property_readonly("side", [](const ExpanderView& v) { return (int) v.side; }, "0 for left, 1 for right.")
		.def_property("module_id", [](const ExpanderView& v) {
			Target t = v.target();
			return v.expander(t).moduleId;
		}, [](const ExpanderView& v, int64_t id) {
			Target t = v.target();
			refuseWhileEngineRuns(t, "module_id");
			if (id < -1)
				throw py::value_error("module_id is a module's ID, or -1 for none");
			v.expander(t).moduleId = id;
		}, "The neighbour's module ID, or -1. Rack sets it on the main thread; writable while Rack's engine does not run "
			"the module.")
		.def_property_readonly("module", &neighbour,
			"The neighbouring module (a RackPy Module, an EngineModule, or None). Read-only: Rack's engine sets the "
			"pointer from module_id every block. While the engine runs the module it is found by module_id.")
		.def_property("producer_message", [](const ExpanderView& v) {
			Target t = v.target();
			refuseWhileEngineRuns(t, "producer_message");
			return messageObject(v.expander(t).producerMessage);
		}, [](const ExpanderView& v, py::handle value) { assignMessage(v, true, value); },
			"Rack's producerMessage: an ExpanderMessage, a ForeignMessage or None. Assign an ExpanderMessage or None, on "
			"a RackPy module, while Rack's engine does not run it.")
		.def_property("consumer_message", [](const ExpanderView& v) {
			Target t = v.target();
			refuseWhileEngineRuns(t, "consumer_message");
			return messageObject(v.expander(t).consumerMessage);
		}, [](const ExpanderView& v, py::handle value) { assignMessage(v, false, value); },
			"Rack's consumerMessage, as producer_message.")
		.def_property("message_flip_requested", [](const ExpanderView& v) {
			Target t = v.target();
			refuseWhileEngineRuns(t, "message_flip_requested");
			return v.expander(t).messageFlipRequested;
		}, [](const ExpanderView& v, bool requested) {
			Target t = v.target();
			refuseWhileEngineRuns(t, "message_flip_requested");
			v.expander(t).messageFlipRequested = requested;
		}, "Whether the messages swap at the end of the frame; while Rack's engine does not run the module.")
		.def("request_message_flip", [](const ExpanderView& v) {
			Target t = v.target();
			refuseWhileEngineRuns(t, "request_message_flip");
			v.expander(t).requestMessageFlip();
		}, "Rack's requestMessageFlip, while Rack's engine does not run the module (a laboratory Bench swaps them).")
		.def("__repr__", [](const ExpanderView& v) { return std::string("<Module.Expander ") + sideName(v.side) + ">"; });

	bindAccessors(module, [](py::object self, uint8_t side) {
		const ModuleRef& ref = self.cast<const ModuleRef&>();
		return ExpanderView{OwnerState(ref.state), std::nullopt, side};
	});
}

void bindEngineModuleExpanders(py::class_<EngineModule>& cls) {
	bindAccessors(cls, [](py::object self, uint8_t side) {
		return ExpanderView{OwnerState(), self.cast<EngineModule>(), side};
	});
}

}  // namespace bindings
}  // namespace rackpy
