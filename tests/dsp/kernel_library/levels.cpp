// SPDX-License-Identifier: GPL-3.0-or-later
// An example RackPy kernel library that exchanges expander messages with a
// schema of its own (RP-23C). "levels.Send" writes its input's channel count
// and voltages to the module on its right; "levels.Receive" outputs what
// arrives from its left, one frame later. The receiver owns the messages:
// its Python code gives the side it receives on two "levels.Levels" buffers
// of 17 values, allocate_messages(module.left_expander, "levels.Levels", 17).
#include <rack.hpp>
#include <rackpy/expanders.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <stdexcept>

namespace {

/** The layout this library defines for "levels.Levels": a channel count, then the voltages. */
constexpr size_t SIZE = 1 + rack::engine::PORT_MAX_CHANNELS;

const rackpy::expander::Schema* levels(const char* who) {
	// Main thread: the kernel's constructor. Null if RackPy gave this library no expander services.
	const rackpy::expander::Schema* schema = rackpy::expander::schema("levels.Levels");
	if (!schema)
		throw std::invalid_argument(std::string(who) + ": RackPy gave this library no expander services");
	return schema;
}

struct Send : rackpy::Kernel {
	int input;
	const rackpy::expander::Schema* schema = levels("levels.Send");
	rackpy::expander::Link link;  // RackPy's, to check the neighbour only when it changes

	explicit Send(const std::vector<double>& v) : input((int) v[0]) {
		if (input < 0)
			throw std::invalid_argument("levels.Send: input must be wired");
	}

	// Rack's engine thread, every frame: no allocation, locks, IO or exceptions.
	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs&) noexcept override {
		// The right neighbour's producer buffer (and the flip requested), only if the neighbour is a RackPy
		// module whose left side holds "levels.Levels" messages of at least SIZE values. Otherwise nothing.
		if (float* message = rackpy::expander::send(m, 1, schema, SIZE, link)) {
			rack::engine::Input& in = m.inputs[input];
			int channels = in.getChannels();
			message[0] = (float) channels;
			for (int c = 0; c < channels; c++)
				message[1 + c] = in.getVoltage(c);
		}
		return true;
	}
};

struct Receive : rackpy::Kernel {
	int output;
	const rackpy::expander::Schema* schema = levels("levels.Receive");
	rackpy::expander::Link neighbour, own;

	explicit Receive(const std::vector<double>& v) : output((int) v[0]) {
		if (output < 0)
			throw std::invalid_argument("levels.Receive: output must be wired");
	}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		// This module's consumer buffer on the left: what the neighbour wrote during the previous frame.
		float* message = rackpy::expander::receive(m, 0, schema, SIZE, args.frame, neighbour, own);
		int channels = message ? std::min(std::max((int) message[0], 0), rack::engine::PORT_MAX_CHANNELS) : 0;
		rack::engine::Output& out = m.outputs[output];
		out.setChannels(std::max(channels, 1));
		for (int c = 0; c < std::max(channels, 1); c++)
			out.setVoltage(c < channels ? message[1 + c] : 0.f, c);
		if (message)
			message[0] = 0.f;  // read once: a neighbour that stops sending is heard as silence
		return true;
	}
};

}  // namespace

RACKPY_KERNEL_LIBRARY("levels", registrar) {
	registrar.add(rackpy::KernelType{
		"Send",
		"Sends its input's channels to the module on its right, as a levels.Levels message.",
		{{"input", rackpy::Field::INPUT, 0, true, "the input port"}},
		{},
		[](const std::vector<double>& v) { return std::make_unique<Send>(v); },
	});
	registrar.add(rackpy::KernelType{
		"Receive",
		"Outputs the levels.Levels message from the module on its left, one frame later.",
		{{"output", rackpy::Field::OUTPUT, 0, true, "the output port"}},
		{},
		[](const std::vector<double>& v) { return std::make_unique<Receive>(v); },
	});
}

// RackPy gives this library its expander services when it loads it.
RACKPY_EXPANDERS
