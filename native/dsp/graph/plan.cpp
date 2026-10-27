// SPDX-License-Identifier: GPL-3.0-or-later
// DSP graph plans (RP-21B): checking a plan, its kernel type, and the plan
// interpreter, a Kernel that runs the plan's nodes in order once per frame.
//
// Everything the interpreter needs is allocated when the kernel is made, on
// the main thread; process() only reads and writes that storage and the
// module's ports, params and lights. Nodes dispatch through one switch, not
// a virtual call each (ADR 004); the compiled tier (RP-21C) removes even that.
// Native extension nodes from kernel libraries are virtual calls. Expander
// nodes (RP-23B) exchange lanes of "rackpy.graph" messages with neighbouring
// RackPy modules, after lock-free checks of the neighbour and its buffers.
#include "plan.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>

#include "../../engine/expanders.hpp"
#include "rackpy/graph.hpp"

namespace rackpy {
namespace graph {

namespace {

enum class Op : uint8_t {
	INPUT,
	PARAM,
	CONSTANT,
	CONTROL,
	SAMPLE_RATE,
	SAMPLE_TIME,
	ADD,
	SUB,
	MUL,
	DIV,
	MIN,
	MAX,
	LT,
	LE,
	GT,
	GE,
	NEG,
	ABS,
	CLAMP,
	WHERE,
	PITCH_TO_FREQ,
	SIN2PI,
	PHASOR,
	SCHMITT,
	PULSE,
	DELAY,
	OUTPUT,
	LIGHT,
	EXPANDER_SEND,
	EXPANDER_RECEIVE,
	EXTENSION,
};

struct OpInfo {
	const char* name;
	Op op;
	size_t inputs;
	size_t args;
	bool signal;
};

// The node types of rackpy.dsp.graph.OPS, with the same arities.
const OpInfo OPS[] = {
	{"input", Op::INPUT, 0, 1, true},
	{"param", Op::PARAM, 0, 1, true},
	{"constant", Op::CONSTANT, 0, 1, true},
	{"control", Op::CONTROL, 0, 1, true},
	{"sample_rate", Op::SAMPLE_RATE, 0, 0, true},
	{"sample_time", Op::SAMPLE_TIME, 0, 0, true},
	{"add", Op::ADD, 2, 0, true},
	{"sub", Op::SUB, 2, 0, true},
	{"mul", Op::MUL, 2, 0, true},
	{"div", Op::DIV, 2, 0, true},
	{"min", Op::MIN, 2, 0, true},
	{"max", Op::MAX, 2, 0, true},
	{"lt", Op::LT, 2, 0, true},
	{"le", Op::LE, 2, 0, true},
	{"gt", Op::GT, 2, 0, true},
	{"ge", Op::GE, 2, 0, true},
	{"neg", Op::NEG, 1, 0, true},
	{"abs", Op::ABS, 1, 0, true},
	{"clamp", Op::CLAMP, 3, 0, true},
	{"where", Op::WHERE, 3, 0, true},
	{"pitch_to_freq", Op::PITCH_TO_FREQ, 1, 0, true},
	{"sin2pi", Op::SIN2PI, 1, 0, true},
	{"phasor", Op::PHASOR, 1, 0, true},
	{"schmitt", Op::SCHMITT, 1, 2, true},
	{"pulse", Op::PULSE, 1, 1, true},
	{"delay", Op::DELAY, 0, 1, true},
	{"output", Op::OUTPUT, 1, 1, false},
	{"light", Op::LIGHT, 1, 1, false},
	{"expander_send", Op::EXPANDER_SEND, 1, 2, false},
	{"expander_receive", Op::EXPANDER_RECEIVE, 0, 2, true},
};

const OpInfo* findOp(const std::string& name) {
	for (const OpInfo& info : OPS)
		if (name == info.name)
			return &info;
	return nullptr;
}

/** One node of a checked plan. */
struct Step {
	Op op;
	uint16_t a = 0, b = 0, c = 0;  // input nodes
	int field = -1;  // INPUT, PARAM, OUTPUT, LIGHT: the kernel type's field holding the index; CONTROL: its slot
	float x = 0.f, y = 0.f;  // CONSTANT: x; SCHMITT: low, high; PULSE: duration; DELAY: initial
	int extension = -1;  // EXTENSION: index into Checked::extensions
	uint8_t side = 0;  // EXPANDER_*: 0 left, 1 right
	int lane = 0;  // EXPANDER_*: the lane of the message
};

/** A native extension node of a checked plan: its type, inputs and arguments in the type's order. */
struct ExtensionStep {
	const NodeType* type;
	std::vector<uint16_t> inputs;
	std::vector<double> args;
};

/** A checked plan, owned by its kernel type and shared by every kernel made from it. */
struct Checked {
	std::vector<Step> steps;
	std::vector<std::pair<uint16_t, uint16_t>> latches;  // (delay node, source node)
	std::vector<ExtensionStep> extensions;
	size_t phasors = 0, schmitts = 0, pulses = 0, delays = 0, links = 0;
};

std::vector<std::unique_ptr<NodeType>>& registry() {
	static std::vector<std::unique_ptr<NodeType>> types;
	return types;
}

[[noreturn]] void refuse(size_t node, const std::string& op, const std::string& why) {
	throw std::invalid_argument("graph node " + std::to_string(node) + " (" + op + "): " + why);
}

bool identifier(const std::string& name) {
	if (name.empty() || !(std::isalpha((unsigned char) name[0]) || name[0] == '_'))
		return false;
	for (char ch : name)
		if (!(std::isalnum((unsigned char) ch) || ch == '_'))
			return false;
	return true;
}

/** The plan interpreter. */
struct GraphKernel final : Kernel {
	struct Run {
		Op op;
		uint16_t self;  // the node, whose signal this writes
		uint16_t a, b, c;
		uint16_t state;  // index into the node type's state vector; EXPANDER_*: into links
		int index;  // a port, param or light index; EXPANDER_*: the lane's first value in the message
		uint8_t side;  // EXPANDER_*
	};
	/** The nodes that do work each frame. Constants and controls are not among them: their signals are
	written when the kernel is made and when a control changes (at a frame boundary, by the slot). */
	std::vector<Run> runs;
	std::vector<uint16_t> controlNodes;  // slot -> node
	std::vector<std::pair<uint16_t, uint16_t>> latches;  // (delay state, source node)
	std::vector<Signal> signals;
	std::vector<Phasor> phasors;
	std::vector<Schmitt> schmitts;
	std::vector<Pulse> pulses;
	std::vector<Delay> delays;
	std::vector<std::unique_ptr<Node>> extensions;
	std::vector<std::vector<const Signal*>> extensionInputs;
	// EXPANDER_SEND uses one (the neighbour), EXPANDER_RECEIVE two (the neighbour, then this module).
	std::vector<engine::Link> links;
	const engine::Schema* schema = engine::graphSchema();

	GraphKernel(const Checked& plan, const KernelType& type, const std::vector<double>& values)
		: signals(plan.steps.size()), phasors(plan.phasors), schmitts(plan.schmitts), pulses(plan.pulses), delays(plan.delays),
		  links(plan.links) {
		std::vector<uint16_t> stateOf(plan.steps.size(), 0);
		size_t counts[4] = {0, 0, 0, 0};
		size_t linked = 0;
		// The controls are the type's NUMBER fields, in slot order, after the wiring.
		std::vector<float> controlValues;
		for (size_t i = 0; i < type.fields.size(); i++)
			if (type.fields[i].kind == Field::NUMBER)
				controlValues.push_back((float) values[i]);
		controlNodes.resize(controlValues.size());
		runs.reserve(plan.steps.size());
		for (size_t k = 0; k < plan.steps.size(); k++) {
			const Step& s = plan.steps[k];
			if (s.op == Op::CONSTANT) {
				mono(signals[k], s.x);
				continue;
			}
			if (s.op == Op::CONTROL) {
				controlNodes[(size_t) s.field] = (uint16_t) k;
				mono(signals[k], controlValues[(size_t) s.field]);
				continue;
			}
			Run r{s.op, (uint16_t) k, s.a, s.b, s.c, 0, s.field >= 0 ? (int) values[(size_t) s.field] : 0, s.side};
			switch (s.op) {
				case Op::PHASOR: r.state = (uint16_t) counts[0]++; break;
				case Op::SCHMITT:
					r.state = (uint16_t) counts[1];
					schmitts[counts[1]].low = s.x;
					schmitts[counts[1]++].high = s.y;
					break;
				case Op::PULSE:
					r.state = (uint16_t) counts[2];
					pulses[counts[2]++].duration = s.x;
					break;
				case Op::DELAY:
					r.state = (uint16_t) counts[3];
					delays[counts[3]++].initial = s.x;
					break;
				case Op::EXPANDER_SEND:
				case Op::EXPANDER_RECEIVE:
					r.state = (uint16_t) linked;
					linked += s.op == Op::EXPANDER_SEND ? 1 : 2;
					r.index = s.lane * LANE;
					break;
				case Op::EXTENSION: {
					// The library's create() runs its own code: RackPy's caller turns what it throws into ValueError.
					const ExtensionStep& e = plan.extensions[(size_t) s.extension];
					r.state = (uint16_t) extensions.size();
					extensions.push_back(e.type->create(e.args));
					if (!extensions.back())
						throw std::invalid_argument(e.type->name + " created no node");
					std::vector<const Signal*> inputs;
					for (uint16_t i : e.inputs)
						inputs.push_back(&signals[i]);
					extensionInputs.push_back(std::move(inputs));
					break;
				}
				default: break;
			}
			stateOf[k] = r.state;
			runs.push_back(r);
		}
		for (const auto& latch : plan.latches)
			latches.emplace_back(stateOf[latch.first], latch.second);
		reset();
	}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		Signal* s = signals.data();
		for (const Run& r : runs) {
			Signal& out = s[r.self];
			switch (r.op) {
				case Op::INPUT: readInput(out, m.inputs[r.index]); break;
				case Op::PARAM: mono(out, m.params[r.index].getValue()); break;
				case Op::CONSTANT:
				case Op::CONTROL: break;  // written in advance
				case Op::SAMPLE_RATE: mono(out, args.sampleRate); break;
				case Op::SAMPLE_TIME: mono(out, args.sampleTime); break;
				case Op::ADD: map2(out, s[r.a], s[r.b], [](float x, float y) { return add(x, y); }); break;
				case Op::SUB: map2(out, s[r.a], s[r.b], [](float x, float y) { return sub(x, y); }); break;
				case Op::MUL: map2(out, s[r.a], s[r.b], [](float x, float y) { return mul(x, y); }); break;
				case Op::DIV: map2(out, s[r.a], s[r.b], [](float x, float y) { return div(x, y); }); break;
				case Op::MIN: map2(out, s[r.a], s[r.b], [](float x, float y) { return fmin(x, y); }); break;
				case Op::MAX: map2(out, s[r.a], s[r.b], [](float x, float y) { return fmax(x, y); }); break;
				case Op::LT: map2(out, s[r.a], s[r.b], [](float x, float y) { return lt(x, y); }); break;
				case Op::LE: map2(out, s[r.a], s[r.b], [](float x, float y) { return le(x, y); }); break;
				case Op::GT: map2(out, s[r.a], s[r.b], [](float x, float y) { return gt(x, y); }); break;
				case Op::GE: map2(out, s[r.a], s[r.b], [](float x, float y) { return ge(x, y); }); break;
				case Op::NEG: map1(out, s[r.a], [](float x) { return neg(x); }); break;
				case Op::ABS: map1(out, s[r.a], [](float x) { return fabs(x); }); break;
				case Op::CLAMP:
					map3(out, s[r.a], s[r.b], s[r.c], [](float x, float lo, float hi) { return clamp(x, lo, hi); });
					break;
				case Op::WHERE:
					map3(out, s[r.a], s[r.b], s[r.c], [](float q, float x, float y) { return where(q, x, y); });
					break;
				case Op::PITCH_TO_FREQ: map1(out, s[r.a], [](float x) { return pitchToFreq(x); }); break;
				case Op::SIN2PI: map1(out, s[r.a], [](float x) { return sin2pi(x); }); break;
				case Op::PHASOR: phasors[r.state].process(out, s[r.a], args.sampleTime); break;
				case Op::SCHMITT: schmitts[r.state].process(out, s[r.a]); break;
				case Op::PULSE: pulses[r.state].process(out, s[r.a], args.sampleTime); break;
				case Op::DELAY: delays[r.state].process(out); break;
				case Op::OUTPUT: writeOutput(m.outputs[r.index], s[r.a]); break;
				case Op::LIGHT: writeLight(m.lights[r.index], s[r.a]); break;
				case Op::EXPANDER_SEND: {
					rack::engine::Module::Expander* flip = nullptr;
					size_t end = (size_t) (r.index + LANE);
					if (float* message = engine::sendBuffer(m, r.side, schema, end, links[r.state], flip)) {
						writeLane(message + r.index, s[r.a]);
						flip->requestMessageFlip();  // Rack's engine swaps the buffers at the end of the frame
					}
					break;
				}
				case Op::EXPANDER_RECEIVE: {
					size_t end = (size_t) (r.index + LANE);
					if (float* message = engine::receiveBuffer(m, r.side, schema, end, args.frame, links[r.state], links[r.state + 1u]))
						readLane(out, message + r.index);
					else
						out.channels = 0;
					break;
				}
				case Op::EXTENSION:
					extensions[r.state]->process(out, extensionInputs[r.state].data(), args);
					if (out.channels < 0 || out.channels > MAX_CHANNELS) {
						out.channels = 0;
						return false;  // the slot silences the module and records the failure
					}
					break;
			}
		}
		for (const auto& latch : latches)
			delays[latch.first].latch(s[latch.second]);
		return true;
	}

	/** Rack's reset: the nodes' state starts over; controls keep the values Python set. */
	void reset() noexcept override {
		for (auto& p : phasors)
			p.reset();
		for (auto& t : schmitts)
			t.reset();
		for (auto& p : pulses)
			p.reset();
		for (auto& d : delays)
			d.reset();
		for (auto& e : extensions)
			e->reset();
	}

	/** At a frame boundary (RP-17A's slot), so the next frame's nodes all see the new value. */
	void setControl(int slot, double value) noexcept override {
		if (slot >= 0 && (size_t) slot < controlNodes.size())
			mono(signals[controlNodes[(size_t) slot]], (float) value);
	}
};

const OpInfo EXTENSION_INFO = {"extension", Op::EXTENSION, 0, 0, true};

/** Checks a native extension node: a registered type, its inputs, and its arguments by name. */
Step extensionStep(Checked& plan, size_t k, const PlanSpec::Node& node, const std::vector<const OpInfo*>& infos) {
	const NodeType* type = findNodeType(node.op);
	if (!type)
		refuse(k, node.op, "no native node type has this name (rackpy.dsp.graph.node_types() lists them)");
	if (node.inputs.size() != (size_t) type->inputs)
		refuse(k, node.op, "reads " + std::to_string(type->inputs) + " signals, not " + std::to_string(node.inputs.size()));
	ExtensionStep e{type, {}, std::vector<double>(type->args.size(), 0.0)};
	for (long long source : node.inputs) {
		if (source < 0 || (size_t) source >= k)
			refuse(k, node.op, "reads node " + std::to_string(source) + "; a node reads only earlier nodes");
		if (!infos[(size_t) source]->signal)
			refuse(k, node.op, "reads node " + std::to_string(source) + ", which makes no signal");
		e.inputs.push_back((uint16_t) source);
	}
	if (node.names.size() != node.args.size())
		refuse(k, node.op, "needs a name for each of its numbers");
	std::vector<bool> given(type->args.size(), false);
	for (size_t i = 0; i < node.names.size(); i++) {
		auto at = std::find(type->args.begin(), type->args.end(), node.names[i]);
		if (at == type->args.end())
			refuse(k, node.op, "takes no number \"" + node.names[i] + "\"");
		size_t slot = (size_t) (at - type->args.begin());
		if (given[slot])
			refuse(k, node.op, "is given \"" + node.names[i] + "\" twice");
		if (!std::isfinite(node.args[i]))
			refuse(k, node.op, "its numbers must be finite");
		given[slot] = true;
		e.args[slot] = node.args[i];
	}
	for (size_t slot = 0; slot < given.size(); slot++)
		if (!given[slot])
			refuse(k, node.op, "needs \"" + type->args[slot] + "\"");
	Step step{Op::EXTENSION};
	step.extension = (int) plan.extensions.size();
	plan.extensions.push_back(std::move(e));
	return step;
}

}  // namespace

void registerNodeType(NodeType type) {
	registry().push_back(std::make_unique<NodeType>(std::move(type)));
}

const NodeType* findNodeType(const std::string& name) {
	for (const auto& type : registry())
		if (type->name == name)
			return type.get();
	return nullptr;
}

std::vector<const NodeType*> nodeTypes() {
	std::vector<const NodeType*> out;
	for (const auto& type : registry())
		out.push_back(type.get());
	return out;
}

std::shared_ptr<const KernelType> planType(const PlanSpec& spec) {
	if (spec.nodes.empty() || spec.nodes.size() > MAX_NODES)
		throw std::invalid_argument("a graph plan has 1 to " + std::to_string(MAX_NODES) + " nodes, not " +
			std::to_string(spec.nodes.size()));
	auto plan = std::make_shared<Checked>();
	std::vector<Field> fields;
	std::map<std::pair<int, long long>, int> fieldOf;  // (kind, index) -> field
	std::set<long long> outputs, lights;
	std::set<std::pair<int, long long>> sends, receives;  // (side, lane)
	std::vector<const OpInfo*> infos;
	for (size_t k = 0; k < spec.nodes.size(); k++) {
		const PlanSpec::Node& node = spec.nodes[k];
		if (node.op.find('.') != std::string::npos) {
			plan->steps.push_back(extensionStep(*plan, k, node, infos));
			infos.push_back(&EXTENSION_INFO);
			continue;
		}
		const OpInfo* info = findOp(node.op);
		if (!info)
			refuse(k, node.op, "not a node type");
		infos.push_back(info);
		if (node.inputs.size() != info->inputs)
			refuse(k, node.op, "reads " + std::to_string(info->inputs) + " signals, not " + std::to_string(node.inputs.size()));
		if (node.args.size() != info->args)
			refuse(k, node.op, "takes " + std::to_string(info->args) + " numbers, not " + std::to_string(node.args.size()));
		Step step{info->op};
		uint16_t* slots[3] = {&step.a, &step.b, &step.c};
		for (size_t i = 0; i < node.inputs.size(); i++) {
			long long source = node.inputs[i];
			if (source < 0 || (size_t) source >= k)
				refuse(k, node.op, "reads node " + std::to_string(source) + "; a node reads only earlier nodes");
			if (!infos[(size_t) source]->signal)
				refuse(k, node.op, "reads node " + std::to_string(source) + ", which makes no signal");
			*slots[i] = (uint16_t) source;
		}
		for (double arg : node.args)
			if (!std::isfinite(arg))
				refuse(k, node.op, "its numbers must be finite");
		switch (info->op) {
			case Op::INPUT:
			case Op::PARAM:
			case Op::OUTPUT:
			case Op::LIGHT: {
				double index = node.args[0];
				if (index < 0 || index != std::floor(index) || index > (1 << 20))
					refuse(k, node.op, "its index must be a whole number from 0");
				Field::Kind kind = info->op == Op::INPUT ? Field::INPUT
					: info->op == Op::PARAM ? Field::PARAM
					: info->op == Op::OUTPUT ? Field::OUTPUT
					: Field::LIGHT;
				if (info->op == Op::OUTPUT && !outputs.insert((long long) index).second)
					refuse(k, node.op, "output " + std::to_string((long long) index) + " is already written");
				if (info->op == Op::LIGHT && !lights.insert((long long) index).second)
					refuse(k, node.op, "light " + std::to_string((long long) index) + " is already set");
				auto key = std::make_pair((int) kind, (long long) index);
				auto found = fieldOf.find(key);
				if (found == fieldOf.end()) {
					std::string name = std::string(info->name) + "[" + std::to_string((long long) index) + "]";
					found = fieldOf.emplace(key, (int) fields.size()).first;
					fields.push_back(Field{name, kind, index, true, "wired by the graph"});
				}
				step.field = found->second;
				break;
			}
			case Op::CONSTANT: step.x = (float) node.args[0]; break;
			case Op::SCHMITT:
				step.x = (float) node.args[0];
				step.y = (float) node.args[1];
				plan->schmitts++;
				break;
			case Op::PULSE:
				if (node.args[0] < 0)
					refuse(k, node.op, "its duration must not be negative");
				step.x = (float) node.args[0];
				plan->pulses++;
				break;
			case Op::DELAY:
				step.x = (float) node.args[0];
				plan->delays++;
				break;
			case Op::PHASOR: plan->phasors++; break;
			case Op::EXPANDER_SEND:
			case Op::EXPANDER_RECEIVE: {
				double side = node.args[0], lane = node.args[1];
				if (side != 0 && side != 1)
					refuse(k, node.op, "its side is 0 (left) or 1 (right)");
				if (lane < 0 || lane != std::floor(lane) || lane >= (double) engine::MAX_GRAPH_LANES)
					refuse(k, node.op, "its lane is a whole number from 0 to " + std::to_string(engine::MAX_GRAPH_LANES - 1));
				auto key = std::make_pair((int) side, (long long) lane);
				bool sending = info->op == Op::EXPANDER_SEND;
				if (!(sending ? sends : receives).insert(key).second)
					refuse(k, node.op, "lane " + std::to_string((long long) lane) + " of side " + std::to_string((int) side) +
						(sending ? " is already sent" : " is already received") + "; a lane carries one signal");
				step.side = (uint8_t) side;
				step.lane = (int) lane;
				plan->links += sending ? 1 : 2;
				break;
			}
			default: break;
		}
		plan->steps.push_back(step);
	}
	if (outputs.empty() && lights.empty() && sends.empty())
		throw std::invalid_argument("a graph plan writes no output, light or expander lane");

	std::vector<bool> fed(spec.nodes.size(), false);
	for (const auto& latch : spec.latches) {
		long long target = latch.first, source = latch.second;
		if (target < 0 || (size_t) target >= spec.nodes.size() || plan->steps[(size_t) target].op != Op::DELAY)
			throw std::invalid_argument("a graph latch feeds node " + std::to_string(target) + ", which is not a delay");
		if (source < 0 || (size_t) source >= spec.nodes.size() || !infos[(size_t) source]->signal)
			throw std::invalid_argument("graph node " + std::to_string(target) + " (delay) is fed from node " +
				std::to_string(source) + ", which makes no signal");
		if (fed[(size_t) target])
			refuse((size_t) target, "delay", "it is fed twice");
		fed[(size_t) target] = true;
		plan->latches.emplace_back((uint16_t) target, (uint16_t) source);
	}
	for (size_t k = 0; k < spec.nodes.size(); k++)
		if (plan->steps[k].op == Op::DELAY && !fed[k])
			refuse(k, "delay", "it is never fed");

	std::vector<std::string> controlNames;
	std::vector<bool> named(spec.nodes.size(), false);
	for (const PlanSpec::Control& control : spec.controls) {
		if (!identifier(control.name))
			throw std::invalid_argument("a graph control's name must be an identifier, not \"" + control.name + "\"");
		for (const auto& name : controlNames)
			if (name == control.name)
				throw std::invalid_argument("graph control " + control.name + " is named twice");
		if (control.node < 0 || (size_t) control.node >= spec.nodes.size() ||
			plan->steps[(size_t) control.node].op != Op::CONTROL || named[(size_t) control.node])
			throw std::invalid_argument("graph control " + control.name + " must name a control node of its own");
		if (!std::isfinite(control.defaultValue))
			throw std::invalid_argument("graph control " + control.name + "'s default must be finite");
		named[(size_t) control.node] = true;
		plan->steps[(size_t) control.node].field = (int) controlNames.size();
		controlNames.push_back(control.name);
		fields.push_back(Field{control.name, Field::NUMBER, control.defaultValue, false, "a control of the graph"});
	}
	for (size_t k = 0; k < spec.nodes.size(); k++)
		if (plan->steps[k].op == Op::CONTROL && !named[k])
			refuse(k, "control", "it has no name");

	auto type = std::make_shared<KernelType>();
	type->name = "rackpy.Graph";
	type->doc = "A DSP graph from Python (rackpy.dsp.graph), " + std::to_string(spec.nodes.size()) + " nodes, run by the plan interpreter.";
	type->fields = std::move(fields);
	type->controls = std::move(controlNames);
	std::shared_ptr<const Checked> checked = plan;
	const KernelType* self = type.get();
	type->create = [checked, self](const std::vector<double>& values) -> std::unique_ptr<Kernel> {
		if (values.size() != self->fields.size())
			throw std::invalid_argument("rackpy.Graph needs " + std::to_string(self->fields.size()) + " field values");
		return std::make_unique<GraphKernel>(*checked, *self, values);
	};
	return type;
}

std::vector<double> planDefaults(const KernelType& type) {
	std::vector<double> values;
	for (const Field& field : type.fields)
		values.push_back(field.defaultValue);
	return values;
}

}  // namespace graph
}  // namespace rackpy
