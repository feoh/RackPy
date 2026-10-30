// SPDX-License-Identifier: GPL-3.0-or-later
// RP-31J: a module's audio port without Python or a Rack host. The bridge is
// compared block for block, bit for bit, with Core Audio's AudioPort and
// Audio::process (Rack 2.6.6 src/core/Audio.cpp), transcribed below with
// Rack's own DoubleRingBuffer and SampleRateConverter; the rings, clear
// requests and converter handover are driven step by step.
#include "../io/endpoints.hpp"
#include "../module/module.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace rackpy;

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

bool same(float a, float b) {
	return std::memcmp(&a, &b, sizeof a) == 0;
}

/** Core Audio's AudioPort and the buffering half of Audio::process, as Audio.cpp:14-171 and 255-334 write them,
with the engine's answers (master, rates) and the port's channels passed in. NI is Core's NUM_AUDIO_INPUTS (to the
device), NO its NUM_AUDIO_OUTPUTS (from it). */
template <int NI, int NO>
struct CoreReference {
	rack::dsp::DoubleRingBuffer<rack::dsp::Frame<NI>, 32768> engineInputBuffer;
	rack::dsp::DoubleRingBuffer<rack::dsp::Frame<NO>, 32768> engineOutputBuffer;
	rack::dsp::SampleRateConverter<NI> inputSrc;
	rack::dsp::SampleRateConverter<NO> outputSrc;
	int deviceNumInputs = 0;
	int deviceNumOutputs = 0;
	float deviceSampleRate = 0.f;
	int requestedEngineFrames = 0;

	CoreReference() {
		// Rack's rings are a module's members (zeroed pages in practice); RackPy's are zeroed.
		std::memset(engineInputBuffer.data, 0, sizeof engineInputBuffer.data);
		std::memset(engineOutputBuffer.data, 0, sizeof engineOutputBuffer.data);
		inputSrc.setQuality(6);
		outputSrc.setQuality(6);
	}

	void processInput(const float* input, int inputStride, int frames, bool isMasterCached, float engineSampleRate,
		float rate, int numInputs, int numOutputs) {
		deviceNumInputs = std::min(numInputs, NO);
		deviceNumOutputs = std::min(numOutputs, NI);
		deviceSampleRate = rate;
		float sampleRateRatio = engineSampleRate / deviceSampleRate;
		int maxEngineFrames = (int) std::ceil(frames * sampleRateRatio * 2.0) - 1;
		if (!isMasterCached && (int) engineOutputBuffer.size() > maxEngineFrames)
			engineOutputBuffer.clear();
		if (deviceNumInputs > 0) {
			if (isMasterCached)
				engineOutputBuffer.clear();
			outputSrc.setRates(deviceSampleRate, engineSampleRate);
			outputSrc.setChannels(deviceNumInputs);
			int inputFrames = frames;
			int outputFrames = engineOutputBuffer.capacity();
			outputSrc.process(input, inputStride, &inputFrames, (float*) engineOutputBuffer.endData(), NO, &outputFrames);
			engineOutputBuffer.endIncr(outputFrames);
			requestedEngineFrames = engineOutputBuffer.size();
		}
		else {
			requestedEngineFrames = std::max((int) std::ceil(frames * sampleRateRatio) - (int) engineInputBuffer.size(), 0);
		}
	}

	void processOutput(float* output, int outputStride, int frames, float engineSampleRate) {
		float sampleRateRatio = engineSampleRate / deviceSampleRate;
		if (deviceNumOutputs > 0) {
			inputSrc.setRates(engineSampleRate, deviceSampleRate);
			inputSrc.setChannels(deviceNumOutputs);
			int inputFrames = engineInputBuffer.size();
			int outputFrames = frames;
			inputSrc.process((const float*) engineInputBuffer.startData(), NI, &inputFrames, output, outputStride, &outputFrames);
			engineInputBuffer.startIncr(inputFrames);
			for (int i = 0; i < outputFrames; i++)
				for (int j = 0; j < deviceNumOutputs; j++)
					output[i * outputStride + j] = rack::math::clamp(output[i * outputStride + j], -1.f, 1.f);
			for (int i = outputFrames; i < frames; i++)
				for (int j = 0; j < deviceNumOutputs; j++)
					output[i * outputStride + j] = 0.f;
		}
		int maxEngineFrames = (int) std::ceil(frames * sampleRateRatio * 2.0) - 1;
		if ((int) engineInputBuffer.size() > maxEngineFrames)
			engineInputBuffer.clear();
	}

	/** Audio::process's buffering: push the frame for the device, then shift the device's. */
	void frame(const float* toDevice, float* fromDevice) {
		if (deviceNumOutputs > 0) {
			rack::dsp::Frame<NI> inputFrame = {};
			for (int i = 0; i < deviceNumOutputs; i++)
				inputFrame.samples[i] = toDevice[i];
			if (!engineInputBuffer.full())
				engineInputBuffer.push(inputFrame);
		}
		if (!engineOutputBuffer.empty()) {
			rack::dsp::Frame<NO> outputFrame = engineOutputBuffer.shift();
			for (int i = 0; i < NO; i++)
				fromDevice[i] = outputFrame.samples[i];
		}
		else {
			for (int i = 0; i < NO; i++)
				fromDevice[i] = 0.f;
		}
	}
};

uint32_t seed = 1;
float noise() {
	seed = seed * 1664525u + 1013904223u;
	return ((float) (seed >> 8) / 16777216.f - 0.5f) * 2.4f;  // past ±1, so the clamp matters
}

struct Schedule {
	float deviceRate, engineRate;
	int block;
	bool master;
	int deviceInputs, deviceOutputs;  // the device's channels after the port's offsets
};

/** Runs the reference and the bridge through the same blocks: a loopback module whose device output is its device
input of the previous frame, as Core Audio patched to itself through a cable. A secondary's engine steps another
master's blocks of `otherBlock` frames. True if every device output sample is identical. */
template <int CH>
bool matchesCore(const Schedule& s, int blocks, int otherBlock) {
	auto reference = std::make_unique<CoreReference<CH, CH>>();
	io::AudioBridge bridge(CH, CH);
	bridge.prepare(s.deviceRate, s.engineRate, s.deviceInputs, s.deviceOutputs);
	int stride = std::max(s.deviceInputs, s.deviceOutputs) + 1;  // the device's interleaving, wider than the port
	std::vector<float> input((size_t) (s.block * stride)), outRef(input.size()), outBridge(input.size());
	float heldRef[CH] = {}, heldBridge[CH] = {};
	float fromRef[CH], fromBridge[CH];
	for (int b = 0; b < blocks; b++) {
		for (float& x : input)
			x = noise();
		reference->processInput(input.data(), stride, s.block, s.master, s.engineRate, s.deviceRate, s.deviceInputs,
			s.deviceOutputs);
		int requested = bridge.input(input.data(), stride, s.block, s.master, s.engineRate, s.deviceRate,
			s.deviceInputs, s.deviceOutputs);
		if (requested != reference->requestedEngineFrames)
			return false;
		int steps = s.master ? requested : otherBlock;
		for (int n = 0; n < steps; n++) {
			reference->frame(heldRef, fromRef);
			bridge.push(heldBridge);
			bridge.shift(fromBridge);
			for (int c = 0; c < CH; c++) {
				heldRef[c] = fromRef[c];
				heldBridge[c] = fromBridge[c];
			}
		}
		std::fill(outRef.begin(), outRef.end(), 0.f);
		std::fill(outBridge.begin(), outBridge.end(), 0.f);
		reference->processOutput(outRef.data(), stride, s.block, s.engineRate);
		bridge.output(outBridge.data(), stride, s.block, s.engineRate);
		for (size_t i = 0; i < outRef.size(); i++)
			if (!same(outRef[i], outBridge[i])) {
				std::fprintf(stderr, "block %d sample %zu: Core %g, bridge %g\n", b, i, outRef[i], outBridge[i]);
				return false;
			}
	}
	return bridge.stats.resamplerMisses.load() == 0;
}

void testTheBridgeIsCoreAudio() {
	// Master at equal rates (Rack's "auto" engine rate), 2 and 16 channels, all or fewer of them on the device.
	CHECK((matchesCore<2>({48000.f, 48000.f, 256, true, 2, 2}, 40, 0)));
	CHECK((matchesCore<16>({48000.f, 48000.f, 128, true, 16, 16}, 20, 0)));
	CHECK((matchesCore<8>({44100.f, 44100.f, 64, true, 3, 5}, 40, 0)));
	// Master with a fixed engine rate: resampled each way, with the converters the main thread prepared.
	CHECK((matchesCore<2>({44100.f, 48000.f, 256, true, 2, 2}, 40, 0)));
	CHECK((matchesCore<2>({96000.f, 44100.f, 512, true, 2, 2}, 30, 0)));
	// A secondary port: another master steps the engine in its own blocks, so frames wait, run short or overflow
	// Core's latency limit and are cleared.
	CHECK((matchesCore<2>({48000.f, 48000.f, 256, false, 2, 2}, 60, 200)));
	CHECK((matchesCore<2>({48000.f, 48000.f, 256, false, 2, 2}, 60, 700)));
	CHECK((matchesCore<4>({44100.f, 48000.f, 128, false, 4, 4}, 60, 150)));
	// Inputs only, outputs only.
	CHECK((matchesCore<2>({48000.f, 48000.f, 256, true, 2, 0}, 20, 0)));
	CHECK((matchesCore<2>({48000.f, 48000.f, 256, true, 0, 2}, 20, 0)));
}

void testRingsAndClears() {
	// An output-only bridge has a zero-stride input ring, with no allocation.
	// Asking for its empty spans must not perform pointer arithmetic on nullptr.
	io::FrameRing empty(0);
	CHECK(empty.readSpan() == nullptr && empty.writeSpan() == nullptr);
	empty.requestClear();
	CHECK(empty.readSpan() == nullptr);
	io::AudioBridge outputOnly(0, 1);
	float sample = 1.f;
	outputOnly.input(nullptr, 0, 1, true, 48000.f, 48000.f, 0, 1);
	outputOnly.shift(&sample);
	CHECK(sample == 1.f);

	io::FrameRing ring(3);
	CHECK(ring.size() == 0 && ring.space() == io::AUDIO_RING_FRAMES);
	// Fill most of the ring, consume it, then write across the end: the run stays contiguous.
	size_t first = io::AUDIO_RING_FRAMES - 5;
	ring.produce(first);
	ring.readSpan();
	ring.consume(first);
	float* w = ring.writeSpan();
	for (int i = 0; i < 10 * 3; i++)
		w[i] = (float) i;
	ring.produce(10);
	CHECK(ring.size() == 10);
	const float* r = ring.readSpan();
	bool contiguous = true;
	for (int i = 0; i < 10 * 3; i++)
		contiguous = contiguous && r[i] == (float) i;
	CHECK(contiguous);
	ring.consume(4);
	r = ring.readSpan();
	CHECK(r[0] == 12.f);  // frame 4's first value, from the ring's start
	// A clear request from any thread: the producer's view at once, the consumer's index at its next read.
	ring.requestClear();
	CHECK(ring.size() == 0);
	w = ring.writeSpan();
	w[0] = 99.f;
	ring.produce(1);
	CHECK(ring.size() == 1);
	r = ring.readSpan();
	CHECK(r[0] == 99.f);
	// The producer never overwrites what the consumer has not consumed: space counts the consumer's index.
	io::FrameRing small(1);
	small.produce(io::AUDIO_RING_FRAMES);
	CHECK(small.space() == 0);
	small.requestClear();
	CHECK(small.space() == 0 && small.size() == 0);  // until the consumer applies it
	small.readSpan();
	CHECK(small.space() == io::AUDIO_RING_FRAMES);
}

void testConverterHandover() {
	io::ConverterSlot slot;
	io::Setting a{44100, 48000, 2}, b{48000, 44100, 2};
	CHECK(io::Setting::unpack(a.pack()) == a && a.pack() != 0 && io::Setting{}.pack() == 0);
	// Nothing prepared: a miss, which the main thread then prepares.
	CHECK(slot.acquire(a) == nullptr);
	slot.maintain({});
	io::Converter* c = slot.acquire(a);
	CHECK(c && c->setting == a && slot.current() == a.pack());
	CHECK(slot.acquire(a) == c);  // kept while the setting stays
	// What the main thread observes is prepared ahead; an unequal setting retires the old one.
	slot.maintain(b);
	io::Converter* d = slot.acquire(b);
	CHECK(d && d != c && d->setting == b);
	// Equal rates: Rack's converter loses its state, so a return to b needs a fresh one.
	slot.release();
	CHECK(slot.current() == 0);
	CHECK(slot.acquire(b) == nullptr);
	slot.maintain({});
	io::Converter* e = slot.acquire(b);
	CHECK(e && e->setting == b);
	// A prepared converter of another setting is retired, never used.
	slot.maintain(a);
	CHECK(slot.acquire(b) == e);  // still b: the prepared a waits
	slot.release();
	CHECK(slot.acquire(io::Setting{44100, 96000, 1}) == nullptr);  // takes a, finds it wrong, retires it
	slot.maintain({});
	CHECK(slot.acquire(io::Setting{44100, 96000, 1}) != nullptr);
	slot.maintain({});  // frees what was retired
}

void testAMissIsSilentAndCounted() {
	io::AudioBridge bridge(2, 2);
	std::vector<float> in(2 * 512, 0.5f), out(2 * 512, 7.f);
	// No converter prepared for 44.1 kHz into 48 kHz: the block is silent, and the engine still steps in time.
	int requested = bridge.input(in.data(), 2, 512, true, 48000.f, 44100.f, 2, 2);
	CHECK(requested == (int) std::ceil(512 * (48000.f / 44100.f)));
	CHECK(bridge.stats.resamplerMisses.load() == 1 && bridge.toEngineFrames() == 0);
	bridge.output(out.data(), 2, 512, 48000.f);
	CHECK(bridge.stats.resamplerMisses.load() == 2);
	bool silent = true;
	for (float x : out)
		silent = silent && x == 0.f;
	CHECK(silent);
	// The main thread prepares what was missed; the next block converts.
	bridge.maintain(0.f, 0.f, 0, 0);
	requested = bridge.input(in.data(), 2, 512, true, 48000.f, 44100.f, 2, 2);
	CHECK(bridge.stats.resamplerMisses.load() == 2 && bridge.toEngineFrames() > 0 && requested > 0);
	CHECK(bridge.toEngineConverter().current() == (io::Setting{44100, 48000, 2}).pack());
}

void testStreamsAndFullRings() {
	io::AudioBridge bridge(1, 1);
	std::vector<float> in(256, 0.25f);
	bridge.input(in.data(), 1, 256, true, 48000.f, 48000.f, 1, 1);
	CHECK(bridge.toEngineFrames() == 256 && bridge.deviceInputs() == 1 && bridge.deviceOutputs() == 1);
	bridge.startStream();  // Core clears both rings
	CHECK(bridge.toEngineFrames() == 0);
	float frame = 1.f;
	bridge.push(&frame);
	bridge.stopStream();
	CHECK(bridge.deviceInputs() == 0 && bridge.deviceOutputs() == 0 && bridge.toDeviceFrames() == 0);
	bridge.push(&frame);  // no device outputs: nothing goes
	CHECK(bridge.toDeviceFrames() == 0);
	// The module's ring fills when nothing consumes it: the rest is dropped and counted.
	bridge.input(in.data(), 1, 1, false, 48000.f, 48000.f, 1, 1);
	float one = 0.f;
	bridge.output(&one, 1, 1, 48000.f);  // the device's side applies the stop's clear
	for (size_t n = 0; n < io::AUDIO_RING_FRAMES + 10; n++)
		bridge.push(&frame);
	CHECK(bridge.toDeviceFrames() == io::AUDIO_RING_FRAMES && bridge.stats.droppedFrames.load() == 10);
	// Underflow: 0 V, counted while the device has inputs.
	float got = 5.f;
	bridge.shift(&got);
	bridge.shift(&got);
	CHECK(got == 0.f && bridge.stats.emptyFrames.load() >= 1);
}

rack::engine::Module::ProcessArgs frameArgs(int64_t n) {
	rack::engine::Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	args.frame = n;
	return args;
}

void testAModuleRunsAFrameOfItsPortAroundItsKernel() {
	dsp::registerBuiltins();
	module::Module m;
	m.config(0, 2, 2, 0);
	m.inputs[0].channels = 1;  // patched
	m.outputs[0].channels = 1;
	m.audio = std::make_shared<io::AudioBridge>(2, 2);
	m.publish();
	CHECK(m.active && m.frame.audioInputs == 2 && m.frame.audioOutputs == 2);
	auto hosted = std::make_unique<dsp::Hosted>();
	hosted->type = dsp::findType("rackpy.AudioInterface");
	hosted->kernel = hosted->type->create({0, 0, 2});
	m.kernels.attach(std::move(hosted), false);
	std::vector<float> in = {0.5f, -0.25f, 0.5f, -0.25f};
	int requested = m.audio->input(in.data(), 2, 2, true, 48000.f, 48000.f, 2, 2);
	CHECK(requested == 2);
	m.inputs[0].setVoltage(3.f);
	for (int n = 0; n < requested; n++)
		m.process(frameArgs(n));
	CHECK(m.outputs[0].getVoltage() == 5.f && m.outputs[1].getVoltage() == -2.5f);  // Core's 10 V full scale
	std::vector<float> out(4, 9.f);
	m.audio->output(out.data(), 2, 2, 48000.f);
	CHECK(out[0] == 3.f / 10.f && out[1] == 0.f && out[2] == 3.f / 10.f);  // an unpatched input sends 0
	// Rack's sample-rate change clears both rings, as Core's onSampleRateChange does.
	m.audio->input(in.data(), 2, 2, true, 48000.f, 48000.f, 2, 2);
	rack::engine::Module::SampleRateChangeEvent e;
	e.sampleRate = 44100.f;
	e.sampleTime = 1.f / 44100.f;
	m.onSampleRateChange(e);
	CHECK(m.audio->toEngineFrames() == 0);
	m.io::Endpoints::detach();
	CHECK(!m.active && !m.audio && m.frame.audioInputs == 0);
}

}  // namespace

int main() {
	runtime::markMainThread();
	testRingsAndClears();
	testConverterHandover();
	testTheBridgeIsCoreAudio();
	testAMissIsSilentAndCounted();
	testStreamsAndFullRings();
	testAModuleRunsAFrameOfItsPortAroundItsKernel();
	if (failures)
		std::fprintf(stderr, "%d check(s) failed\n", failures);
	else
		std::printf("io paths: all checks passed\n");
	return failures ? 1 : 0;
}
