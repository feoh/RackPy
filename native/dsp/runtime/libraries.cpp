// SPDX-License-Identifier: GPL-3.0-or-later
// Kernel libraries (RP-17B): plugin-shipped native code that adds kernel
// types, and graph node types (RP-21C) if it defines RACKPY_GRAPH_NODES.
// Trusted like the rest of the plugin, but confined to its folder, checked
// against the kernel and graph ABIs, and loaded all-or-nothing per library.
#include <dlfcn.h>

#include <cctype>
#include <set>
#include <stdexcept>

#include "../graph/plan.hpp"
#include "runtime.hpp"

namespace rackpy {
namespace dsp {

namespace {

struct Collecting : KernelRegistrar {
	std::vector<KernelType> types;
	void add(KernelType type) override { types.push_back(std::move(type)); }
};

struct CollectingNodes : graph::NodeRegistrar {
	std::vector<graph::NodeType> types;
	void add(graph::NodeType type) override { types.push_back(std::move(type)); }
};

/** Runs a library's registration function, turning what it throws into std::runtime_error. */
template <typename F>
void registration(F call) {
	try {
		call();
	}
	catch (const std::exception& e) {
		throw std::runtime_error(std::string("its registration raised: ") + e.what());
	}
	catch (...) {
		throw std::runtime_error("its registration raised an exception");
	}
}

bool identifier(const std::string& name) {
	if (name.empty() || !(std::isalpha((unsigned char) name[0]) || name[0] == '_'))
		return false;
	for (char c : name)
		if (!(std::isalnum((unsigned char) c) || c == '_'))
			return false;
	return true;
}

/** Load one library; throws std::runtime_error with the reason it was skipped. */
void loadLibrary(const std::string& pluginDir, const std::string& relative) {
	if (relative.empty() || relative[0] == '/' || relative.find("..") != std::string::npos)
		throw std::runtime_error("the path must be relative to the plugin folder, without \"..\"");
	std::string path = rack::system::join(pluginDir, relative);
	if (!rack::system::isFile(path))
		throw std::runtime_error("no such file");
	std::string folder = rack::system::getCanonical(pluginDir);
	if (rack::system::getCanonical(path).compare(0, folder.size() + 1, folder + "/") != 0)
		throw std::runtime_error("it resolves outside the plugin folder");
	void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
	if (!handle)
		throw std::runtime_error(std::string("cannot load it: ") + dlerror());
	auto abi = (int (*)()) dlsym(handle, "rackpy_kernel_abi");
	auto ns = (const char* (*)()) dlsym(handle, "rackpy_kernel_namespace");
	auto registerKernels = (void (*)(KernelRegistrar&)) dlsym(handle, "rackpy_register_kernels");
	if (!abi || !ns || !registerKernels)
		throw std::runtime_error("it does not define RACKPY_KERNEL_LIBRARY");
	if (abi() != KERNEL_ABI)
		throw std::runtime_error("it was built for kernel ABI " + std::to_string(abi()) + ", but RackPy has " +
			std::to_string(KERNEL_ABI) + "; rebuild it against this RackPy's rackpy/kernel.hpp");
	std::string space = ns() ? ns() : "";
	if (!identifier(space))
		throw std::runtime_error("its namespace \"" + space + "\" is not an identifier");
	if (space == "rackpy" || space.compare(0, 7, "rackpy_") == 0)
		throw std::runtime_error("the namespace " + space + " is reserved for RackPy");
	Collecting collected;
	registration([&] { registerKernels(collected); });
	// Graph node types are optional; when present they load with the kernels, all or nothing.
	CollectingNodes nodes;
	auto graphAbi = (int (*)()) dlsym(handle, "rackpy_graph_abi");
	auto registerNodes = (void (*)(graph::NodeRegistrar&)) dlsym(handle, "rackpy_register_graph_nodes");
	if (graphAbi || registerNodes) {
		if (!graphAbi || !registerNodes)
			throw std::runtime_error("it defines only part of RACKPY_GRAPH_NODES");
		if (graphAbi() != graph::GRAPH_ABI)
			throw std::runtime_error("it was built for graph ABI " + std::to_string(graphAbi()) + ", but RackPy has " +
				std::to_string(graph::GRAPH_ABI) + "; rebuild it against this RackPy's rackpy/graph.hpp");
		registration([&] { registerNodes(nodes); });
	}
	std::set<std::string> nodeNames;
	for (auto& type : nodes.types) {
		if (!identifier(type.name))
			throw std::runtime_error("graph node type name \"" + type.name + "\" is not an identifier");
		type.name = space + "." + type.name;
		if (graph::findNodeType(type.name) || !nodeNames.insert(type.name).second)
			throw std::runtime_error("graph node type " + type.name + " is already registered");
		if (type.inputs < 0 || type.inputs > graph::MAX_NODE_INPUTS)
			throw std::runtime_error("graph node type " + type.name + " reads " + std::to_string(type.inputs) +
				" signals; at most " + std::to_string(graph::MAX_NODE_INPUTS));
		std::set<std::string> argNames;
		for (const auto& arg : type.args)
			if (!identifier(arg) || !argNames.insert(arg).second)
				throw std::runtime_error("graph node type " + type.name + " has a bad or repeated argument name \"" + arg + "\"");
		if (!type.create)
			throw std::runtime_error("graph node type " + type.name + " has no create function");
	}
	for (auto& type : collected.types) {
		if (!identifier(type.name))
			throw std::runtime_error("kernel type name \"" + type.name + "\" is not an identifier");
		type.name = space + "." + type.name;
		if (findType(type.name))
			throw std::runtime_error("kernel type " + type.name + " is already registered");
		if (!type.create)
			throw std::runtime_error("kernel type " + type.name + " has no create function");
	}
	for (auto& type : collected.types) {
		std::string name = type.name;
		registerType(std::move(type));
		INFO("RackPy: kernel type %s from %s", name.c_str(), relative.c_str());
	}
	for (auto& type : nodes.types) {
		std::string name = type.name;
		graph::registerNodeType(std::move(type));
		INFO("RackPy: graph node type %s from %s", name.c_str(), relative.c_str());
	}
}

}  // namespace

void loadKernelLibraries(const std::string& pluginDir, const std::vector<std::string>& libraries) {
	registerBuiltins();
	for (const auto& relative : libraries) {
		try {
			loadLibrary(pluginDir, relative);
		}
		catch (const std::exception& e) {
			WARN("RackPy skipped kernel library %s: %s", relative.c_str(), e.what());
		}
	}
}

}  // namespace dsp
}  // namespace rackpy
