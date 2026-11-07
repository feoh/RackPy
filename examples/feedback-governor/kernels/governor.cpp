// SPDX-License-Identifier: GPL-3.0-or-later
// Original RackPy implementation. Its behavior is independently inspired by
// Feedback Governor by Daniel Boles; see ../PROVENANCE.md.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <cmath>

namespace {

constexpr float PI = 3.14159265358979323846f;

struct Governor final : rackpy::Kernel {
	int feedback, cap, tone, decay, kill_button, send, returned, cv, kill_input, output;
	float low = 0.f;
	float dc_in = 0.f;
	float dc_out = 0.f;
	bool was_reset = false;

	explicit Governor(const std::vector<double>& v)
		: feedback((int) v[0]), cap((int) v[1]), tone((int) v[2]), decay((int) v[3]), kill_button((int) v[4]),
		  send((int) v[5]), returned((int) v[6]), cv((int) v[7]), kill_input((int) v[8]), output((int) v[9]) {}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		const bool reset = m.params[kill_button].getValue() >= 0.5f ||
		                   (kill_input >= 0 && m.inputs[kill_input].getVoltage() >= 1.f);
		if (reset) {
			low = dc_in = dc_out = 0.f;
			m.outputs[output].setVoltage(0.f);
			was_reset = true;
			return true;
		}
		if (was_reset) {
			low = dc_in = dc_out = 0.f;
			was_reset = false;
		}

		const float sampleRate = std::max(args.sampleRate, 1.f);
		const float cutoff = std::clamp(m.params[tone].getValue(), 20.f, 20000.f);
		const float alpha = 1.f - std::exp(-2.f * PI * std::min(cutoff, 0.45f * sampleRate) / sampleRate);
		const float raw = m.inputs[returned].getVoltage();
		low += alpha * (raw - low);
		// One-pole DC blocker; its pole follows sample rate rather than a fixed sample count.
		const float pole = std::exp(-2.f * PI * 5.f / sampleRate);
		const float blocked = low - dc_in + pole * dc_out;
		dc_in = low;
		dc_out = blocked;

		const float cvVoltage = cv >= 0 ? m.inputs[cv].getVoltage() : 0.f;
		const float cvScale = std::clamp(cvVoltage * 0.1f, 0.f, 1.f);
		const float amount = std::clamp(m.params[feedback].getValue() + cvScale, 0.f, 1.f);
		const float perPass = std::clamp(m.params[decay].getValue(), 0.f, 1.f);
		const float governed = blocked * amount * perPass;
		const float ceiling = std::clamp(m.params[cap].getValue(), 1.f, 10.f);
		const float mixed = m.inputs[send].getVoltage() + governed;
		m.outputs[output].setVoltage(std::clamp(mixed, -ceiling, ceiling));
		return true;
	}

	void reset() noexcept override { low = dc_in = dc_out = 0.f; was_reset = false; }
	void sampleRateChange(float, float) noexcept override { low = dc_in = dc_out = 0.f; }
};

}  // namespace

RACKPY_KERNEL_LIBRARY("governor", registrar) {
	registrar.add(rackpy::KernelType{
		"FeedbackGovernor",
		"Mono controlled feedback return with tone filtering, DC removal, per-pass decay and a hard output ceiling.",
		{
			{"feedback", rackpy::Field::PARAM, 0, true, "feedback amount, 0 to 1"},
			{"cap", rackpy::Field::PARAM, 1, true, "absolute output ceiling in volts, 1 to 10"},
			{"tone", rackpy::Field::PARAM, 2, true, "low-pass cutoff in hertz"},
			{"decay", rackpy::Field::PARAM, 3, true, "per-pass return multiplier, 0 to 1"},
			{"kill_button", rackpy::Field::PARAM, 4, true, "momentary kill/reset"},
			{"send", rackpy::Field::INPUT, 0, true, "dry signal summed into the governed output"},
			{"return", rackpy::Field::INPUT, 1, true, "signal returned from the external feedback loop"},
			{"cv", rackpy::Field::INPUT, -1, false, "0 to 10 V adds feedback amount"},
			{"kill_input", rackpy::Field::INPUT, -1, false, "1 V kills and clears the return state"},
			{"output", rackpy::Field::OUTPUT, 0, true, "send plus governed return, hard capped"},
		},
		{},
		[](const std::vector<double>& v) { return std::make_unique<Governor>(v); },
	});
}
