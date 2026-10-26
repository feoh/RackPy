// SPDX-License-Identifier: GPL-3.0-or-later
// DSP graph plans (RP-21B): a graph Python lowered (rackpy.dsp.graph.Plan),
// checked again here and run by a native Kernel, the plan interpreter.
//
// Each plan gets its own KernelType, "rackpy.Graph", whose fields are the
// ports, params and lights the plan uses (so Module.set_kernel checks its
// wiring like any kernel's) followed by its controls (so Kernel.set reaches
// them by name). Publication, the control queue, frame-boundary replacement
// and reclamation are RP-17A's KernelSlot, unchanged. Python-free.
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rackpy/graph.hpp"
#include "rackpy/kernel.hpp"

namespace rackpy {
namespace graph {

/** A plan as Python lowered it (rackpy.dsp.graph.Plan.to_dict()), not yet checked. */
struct PlanSpec {
	struct Node {
		std::string op;  // a node type of rackpy.dsp.graph.OPS, or a native extension's "namespace.Name"
		std::vector<long long> inputs;
		std::vector<double> args;
		std::vector<std::string> names;  // an extension's argument names, one per arg (any order)
	};
	struct Control {
		std::string name;
		long long node;
		double defaultValue;
	};
	std::vector<Node> nodes;
	std::vector<std::pair<long long, long long>> latches;
	std::vector<Control> controls;
};

/** The most nodes a plan may have. */
constexpr size_t MAX_NODES = 4096;

/** Checks `spec` completely and returns its kernel type, which owns the
checked plan. Throws std::invalid_argument naming the node at fault. Main thread. */
std::shared_ptr<const KernelType> planType(const PlanSpec& spec);

/** The values a kernel of `type` starts with: the plan's wiring and its controls' defaults. */
std::vector<double> planDefaults(const KernelType& type);

/** Native extension node types, from kernel libraries (main thread). `name` is "namespace.Name". */
void registerNodeType(NodeType type);
const NodeType* findNodeType(const std::string& name);
std::vector<const NodeType*> nodeTypes();

}  // namespace graph
}  // namespace rackpy
