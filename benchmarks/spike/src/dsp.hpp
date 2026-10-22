// SPDX-License-Identifier: GPL-3.0-or-later
// RP-07 benchmark DSP. Native modules and RackPy-style kernels both call these
// functions, so their arithmetic is identical and outputs can be compared bit
// for bit. Spike code, not RackPy.
#pragma once
#include <rack.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace bench {

using rack::simd::float_4;

/** Tutorial-style sine oscillator. */
struct Sine {
	float phase = 0.f;
	float step(float pitch, float sampleTime) {
		float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch);
		phase += freq * sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;
		return 5.f * std::sin(2.f * float(M_PI) * phase);
	}
};

/** 16 voices, one float at a time. */
struct PolySine {
	std::array<float, 16> phase{};
	void step(const float* pitch, float* out, int channels, float sampleTime) {
		for (int c = 0; c < channels; c++) {
			float freq = rack::dsp::FREQ_C4 * std::pow(2.f, pitch[c]);
			phase[c] += freq * sampleTime;
			if (phase[c] >= 1.f)
				phase[c] -= 1.f;
			out[c] = 5.f * std::sin(2.f * float(M_PI) * phase[c]);
		}
	}
};

/** 16 voices, four at a time with Rack's SIMD library. */
struct PolySineSimd {
	std::array<float_4, 4> phase{};
	void step(const float* pitch, float* out, int channels, float sampleTime) {
		for (int c = 0; c < channels; c += 4) {
			float_4 p = float_4::load(pitch + c);
			float_4 freq = rack::dsp::FREQ_C4 * rack::simd::pow(2.f, p);
			float_4& ph = phase[c / 4];
			ph += freq * sampleTime;
			ph -= rack::simd::floor(ph);
			(5.f * rack::simd::sin(2.f * float(M_PI) * ph)).store(out + c);
		}
	}
};

inline float gain(float in, float g) {
	return in * g;
}

/** Fixed-capacity single-producer, single-consumer ring. Never blocks; a full ring drops. */
template <typename T, size_t N>
struct SpscRing {
	static_assert((N & (N - 1)) == 0, "N must be a power of two");
	std::array<T, N> items{};
	std::atomic<size_t> head{0};  // written by the consumer
	std::atomic<size_t> tail{0};  // written by the producer
	std::atomic<uint64_t> dropped{0};

	bool push(const T& v) {
		size_t t = tail.load(std::memory_order_relaxed);
		if (t - head.load(std::memory_order_acquire) == N) {
			dropped.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		items[t & (N - 1)] = v;
		tail.store(t + 1, std::memory_order_release);
		return true;
	}
	bool pop(T& v) {
		size_t h = head.load(std::memory_order_relaxed);
		if (h == tail.load(std::memory_order_acquire))
			return false;
		v = items[h & (N - 1)];
		head.store(h + 1, std::memory_order_release);
		return true;
	}
};

/** A parameter change from the UI: what RackPy's command queue carries. */
struct Command {
	int32_t slot;
	float value;
};

}  // namespace bench
