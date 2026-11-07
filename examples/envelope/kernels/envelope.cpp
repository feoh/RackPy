// SPDX-License-Identifier: GPL-3.0-or-later
// Original RackPy envelope kernel. Only the Rack public plugin entry point is included.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <algorithm>
#include <cmath>

namespace {

struct Envelope : rackpy::Kernel {
	int attack, decay, loop, trigger, envelope, inverted, eoc;
	rack::dsp::SchmittTrigger edge;
	rack::dsp::PulseGenerator pulse;
	enum class Stage { IDLE, ATTACK, DECAY };
	Stage stage = Stage::IDLE;
	float value = 0.f;
	float attack_coefficient = 0.f;
	float decay_coefficient = 0.f;
	float sample_rate = 0.f;
	float last_attack = -1.f;
	float last_decay = -1.f;

	explicit Envelope(const std::vector<double>& v)
		: attack(static_cast<int>(v[0])), decay(static_cast<int>(v[1])), loop(static_cast<int>(v[2])),
		  trigger(static_cast<int>(v[3])), envelope(static_cast<int>(v[4])), inverted(static_cast<int>(v[5])),
		  eoc(static_cast<int>(v[6])) {}

	static float seconds(float knob) noexcept {
		// 1 ms at zero, 2 s at one; exponential time is useful over the whole knob travel.
		return 0.001f * std::pow(2000.f, rack::math::clamp(knob, 0.f, 1.f));
	}

	void update_coefficients(float rate, rack::engine::Module& m) noexcept {
		if (rate <= 0.f)
			return;
		sample_rate = rate;
		last_attack = m.params[attack].getValue();
		last_decay = m.params[decay].getValue();
		attack_coefficient = 1.f - std::exp(-1.f / (seconds(last_attack) * rate));
		decay_coefficient = 1.f - std::exp(-1.f / (seconds(last_decay) * rate));
	}

	bool process(rack::engine::Module& m, const rack::engine::Module::ProcessArgs& args) noexcept override {
		if (sample_rate != args.sampleRate || last_attack != m.params[attack].getValue() ||
		    last_decay != m.params[decay].getValue())
			update_coefficients(args.sampleRate, m);
		if (edge.process(m.inputs[trigger].getVoltage())) {
			stage = Stage::ATTACK;
			pulse.reset();
		}
		if (stage == Stage::ATTACK) {
			value += (10.f - value) * attack_coefficient;
			if (value >= 9.99f) {
				value = 10.f;
				stage = Stage::DECAY;
			}
		} else if (stage == Stage::DECAY) {
			value += (0.f - value) * decay_coefficient;
			if (value <= 0.01f) {
				value = 0.f;
				pulse.trigger(0.001f);
				stage = m.params[loop].getValue() >= 0.5f ? Stage::ATTACK : Stage::IDLE;
			}
		}
		bool eoc_active = pulse.process(args.sampleTime);
		m.outputs[envelope].setVoltage(value);
		m.outputs[inverted].setVoltage(10.f - value);
		m.outputs[eoc].setVoltage(eoc_active ? 10.f : 0.f);
		return true;
	}

	void reset() noexcept override {
		edge.reset();
		pulse.reset();
		stage = Stage::IDLE;
		value = 0.f;
	}

	void sampleRateChange(float, float) noexcept override { sample_rate = 0.f; }
};

}  // namespace

RACKPY_KERNEL_LIBRARY("envelope", registrar) {
	registrar.add(rackpy::KernelType{
		"AD",
		"Native exponential attack/decay envelope with optional looping and end-of-cycle pulse.",
		{
			{"attack", rackpy::Field::PARAM, 0, true, "attack time, 1 ms to 2 s"},
			{"decay", rackpy::Field::PARAM, 1, true, "decay time, 1 ms to 2 s"},
			{"loop", rackpy::Field::PARAM, 2, true, "restart after each cycle"},
			{"trigger", rackpy::Field::INPUT, 0, true, "trigger input"},
			{"envelope", rackpy::Field::OUTPUT, 0, true, "0 to 10 V envelope"},
			{"inverted", rackpy::Field::OUTPUT, 1, true, "10 V minus envelope"},
			{"eoc", rackpy::Field::OUTPUT, 2, true, "1 ms end-of-cycle pulse"},
		},
		{},
		[](const std::vector<double>& v) { return std::make_unique<Envelope>(v); },
	});
}
