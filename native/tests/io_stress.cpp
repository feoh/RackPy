// SPDX-License-Identifier: GPL-3.0-or-later
// RP-31J: a module's audio port under stress, built twice by CMake: with
// AddressSanitizer and UndefinedBehaviorSanitizer, and with ThreadSanitizer
// (tests/io/test_io_native.py runs both). No Rack host: Rack's threads are
// reproduced, not run.
//
// A driver thread runs the port's blocks (input, then output). As master it
// steps the module's frames itself between them, as Core Audio's port steps
// Rack's engine; as a secondary, an engine thread steps them continuously in
// its own time. Meanwhile the main thread, as RackPy's UI step does, prepares
// converters for device rates that keep changing and frees retired ones, and
// starts the stream again, as Rack's drivers do on the main thread. The
// module loops the device's input back to it. As master, at equal rates,
// every block the device gets back must hold the counter the driver sent, in
// order (gaps where a restart clears frames): a torn or reordered frame
// shows. A secondary's frames cross threads at the engine's pace, so there
// every sample must only be one the driver could have sent.
#include "../io/audio_bridge.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace rackpy;

namespace {

int failures = 0;
std::atomic<int> threadFailures{0};

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

constexpr int BLOCK = 128;
constexpr float STEP = 1.f / 16777216.f;  // the counter's unit: exact in float up to 2^24

/** The engine's half: one frame of the module, looping the device's input back to it. */
void moduleFrame(io::AudioBridge& bridge) {
	float frame[2];
	bridge.shift(frame);
	bridge.push(frame);
}

/** The device's rate for block `b`: equal to the engine's most of the time, so the counter can be checked. */
float rateOf(int b) {
	switch ((b / 64) % 4) {
		case 1: return 44100.f;
		case 3: return 96000.f;
		default: return 48000.f;
	}
}

void run(bool master, int blocks) {
	io::AudioBridge bridge(2, 2);
	const float engineRate = 48000.f;
	std::atomic<bool> done{false};
	std::atomic<int> deviceRate{48000};  // what the main thread observes
	std::thread engine;
	if (!master)
		engine = std::thread([&] {
			while (!done.load(std::memory_order_acquire)) {
				for (int n = 0; n < 32; n++)
					moduleFrame(bridge);
				std::this_thread::yield();
			}
		});
	std::thread driver([&] {
		std::vector<float> in(2 * BLOCK), out(2 * BLOCK);
		uint32_t counter = 1;
		for (int b = 0; b < blocks; b++) {
			float rate = rateOf(b);
			deviceRate.store((int) rate, std::memory_order_relaxed);
			for (int i = 0; i < BLOCK; i++) {
				in[(size_t) (2 * i)] = (float) (counter++ % 16777216u) * STEP;
				in[(size_t) (2 * i + 1)] = -0.25f;
			}
			int requested = bridge.input(in.data(), 2, BLOCK, master, engineRate, rate, 2, 2);
			if (master)
				for (int n = 0; n < requested; n++)
					moduleFrame(bridge);
			bridge.output(out.data(), 2, BLOCK, engineRate);
			for (int i = 0; i < 2 * BLOCK; i++)
				if (!std::isfinite(out[(size_t) i]) || std::fabs(out[(size_t) i]) > 1.f) {
					threadFailures.fetch_add(1);
					break;
				}
			// At equal rates, what comes back is the counter in order, with gaps, or silence.
			if (master && rate == engineRate && rateOf(b - 1) == engineRate) {
				float last = 0.f;
				for (int i = 0; i < BLOCK; i++) {
					float x = out[(size_t) (2 * i)];
					if (x == 0.f)
						continue;
					if (!(x > last) || out[(size_t) (2 * i + 1)] != -0.25f) {
						threadFailures.fetch_add(1);
						break;
					}
					last = x;
				}
			}
		}
		done.store(true, std::memory_order_release);
	});
	// The main thread: UI steps, and Rack's drivers starting and stopping the stream.
	int steps = 0;
	while (!done.load(std::memory_order_acquire)) {
		bridge.maintain((float) deviceRate.load(std::memory_order_relaxed), engineRate, 2, 2);
		if (++steps % 50 == 0)
			bridge.startStream();
		(void) bridge.stats.droppedFrames.load();
		(void) bridge.toEngineFrames();
		std::this_thread::yield();
	}
	driver.join();
	if (engine.joinable())
		engine.join();
	bridge.maintain(0.f, 0.f, 0, 0);
	CHECK(bridge.stats.blocks.load() == (uint64_t) blocks);
	CHECK(threadFailures.load() == 0);
}

}  // namespace

int main(int argc, char** argv) {
	int blocks = argc > 1 ? std::atoi(argv[1]) : 2000;
	run(true, blocks);
	run(false, blocks);
	if (failures)
		std::fprintf(stderr, "%d check(s) failed\n", failures);
	else
		std::printf("io stress: all checks passed\n");
	return failures ? 1 : 0;
}
