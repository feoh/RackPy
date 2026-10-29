// SPDX-License-Identifier: GPL-3.0-or-later
// Test that a separately loaded kernel library receives RackPy's I/O services.
#include <rack.hpp>
#include <rackpy/io.hpp>
#include <rackpy/kernel.hpp>

namespace {
struct Probe : rackpy::Kernel {
	int output;
	rackpy::io::Link link;
	explicit Probe(int index) : output(index) {}
	bool process(rack::engine::Module& module, const rack::engine::Module::ProcessArgs&) noexcept override {
		module.outputs[(size_t) output].setVoltage(rackpy::io::frame(module, link) ? 10.f : 0.f);
		return true;
	}
};
}
RACKPY_KERNEL_LIBRARY("iotest", registrar) {
	registrar.add(rackpy::KernelType{"Probe", "I/O services test", {{"output", rackpy::Field::OUTPUT, 0, true, "output"}}, {},
		[](const std::vector<double>& values) { return std::make_unique<Probe>((int) values[0]); }});
}
RACKPY_IO
