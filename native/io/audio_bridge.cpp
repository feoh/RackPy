// SPDX-License-Identifier: GPL-3.0-or-later
// A module's audio port (RP-31J): see audio_bridge.hpp. Each step names the
// lines of Rack's src/core/Audio.cpp (Rack 2.6.6) it reproduces.
#include "audio_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace rackpy {
namespace io {

// --- FrameRing ---------------------------------------------------------------------------------

FrameRing::FrameRing(int stride)
	: stride(stride), data(stride > 0 ? new float[2 * AUDIO_RING_FRAMES * (size_t) stride]() : nullptr) {}

void FrameRing::requestClear() noexcept {
	size_t t = tail.load(std::memory_order_acquire);
	size_t c = clearTo.load(std::memory_order_relaxed);
	while (c < t && !clearTo.compare_exchange_weak(c, t, std::memory_order_acq_rel, std::memory_order_relaxed)) {
	}
}

size_t FrameRing::size() const noexcept {
	size_t t = tail.load(std::memory_order_acquire);
	size_t h = std::max(head.load(std::memory_order_acquire), clearTo.load(std::memory_order_acquire));
	return t > h ? t - h : 0;
}

size_t FrameRing::space() const noexcept {
	return AUDIO_RING_FRAMES - (tail.load(std::memory_order_relaxed) - head.load(std::memory_order_acquire));
}

float* FrameRing::writeSpan() noexcept {
	if (!data)
		return nullptr;
	return data.get() + (tail.load(std::memory_order_relaxed) % AUDIO_RING_FRAMES) * (size_t) stride;
}

void FrameRing::produce(size_t frames) noexcept {
	if (frames == 0)
		return;
	// DoubleRingBuffer::endIncr: copy what was written to its other copy.
	size_t t = tail.load(std::memory_order_relaxed);
	size_t i = t % AUDIO_RING_FRAMES, s = (size_t) stride;
	size_t e1 = i + frames, e2 = std::min(e1, AUDIO_RING_FRAMES);
	float* d = data.get();
	std::memcpy(d + (AUDIO_RING_FRAMES + i) * s, d + i * s, sizeof(float) * (e2 - i) * s);
	if (e1 > AUDIO_RING_FRAMES)
		std::memcpy(d, d + AUDIO_RING_FRAMES * s, sizeof(float) * (e1 - AUDIO_RING_FRAMES) * s);
	tail.store(t + frames, std::memory_order_release);
}

const float* FrameRing::readSpan() noexcept {
	size_t h = head.load(std::memory_order_relaxed);
	size_t c = clearTo.load(std::memory_order_acquire);
	if (c > h) {
		h = c;
		head.store(h, std::memory_order_release);
	}
	return data ? data.get() + (h % AUDIO_RING_FRAMES) * (size_t) stride : nullptr;
}

void FrameRing::consume(size_t frames) noexcept {
	head.store(head.load(std::memory_order_relaxed) + frames, std::memory_order_release);
}

// --- Converters ----------------------------------------------------------------------------------

uint64_t Setting::pack() const noexcept {
	if (inRate <= 0 || outRate <= 0 || channels <= 0)
		return 0;
	return ((uint64_t) (uint32_t) inRate << 32) | ((uint64_t) ((uint32_t) outRate & 0xffffffu) << 8) |
		(uint64_t) (channels & 0xff);
}

Setting Setting::unpack(uint64_t packed) noexcept {
	return {(int) (packed >> 32), (int) ((packed >> 8) & 0xffffffu), (int) (packed & 0xff)};
}

Converter::Converter(Setting setting) : setting(setting) {
	// Audio.cpp:35-36 and 90-91 (outputSrc) or 120-121 (inputSrc): each change starts the speex state afresh.
	src.setQuality(RESAMPLER_QUALITY);
	src.setRates(setting.inRate, setting.outRate);
	src.setChannels(setting.channels);
}

ConverterSlot::~ConverterSlot() {
	Converter* c;
	while (retired.pop(c))
		delete c;
	delete prepared.exchange(nullptr);
	delete using_;
}

bool ConverterSlot::retire() noexcept {
	if (!using_)
		return true;
	if (!retired.push(using_))
		return false;  // the main thread has not emptied the ring yet: try again next block
	using_ = nullptr;
	inUse.store(0, std::memory_order_release);
	return true;
}

Converter* ConverterSlot::acquire(Setting setting) noexcept {
	if (using_ && !using_->spent && using_->setting == setting)
		return using_;
	wanted.store(setting.pack(), std::memory_order_relaxed);
	if (!retire())
		return nullptr;
	// Room to retire a prepared converter of another setting; only this thread pushes.
	if (!prepared.load(std::memory_order_relaxed) || retired.size() >= 8)
		return nullptr;
	Converter* next = prepared.exchange(nullptr, std::memory_order_acq_rel);
	if (!next)
		return nullptr;
	if (next->setting != setting) {
		retired.push(next);
		return nullptr;
	}
	using_ = next;
	inUse.store(setting.pack(), std::memory_order_release);
	wanted.store(0, std::memory_order_relaxed);
	return using_;
}

void ConverterSlot::release() noexcept {
	wanted.store(0, std::memory_order_relaxed);
	if (using_ && !retire()) {
		using_->spent = true;  // not fresh any more: never used again
		inUse.store(0, std::memory_order_release);
	}
}

void ConverterSlot::prepare(Setting setting) {
	uint64_t packed = setting.pack();
	if (!packed || setting.inRate == setting.outRate || inUse.load(std::memory_order_acquire) == packed)
		return;
	// The driver only ever takes the prepared converter (an exchange), so reading it here is safe.
	Converter* waiting = prepared.load(std::memory_order_acquire);
	if (waiting && waiting->setting == setting)
		return;
	auto* fresh = new Converter(setting);
	delete prepared.exchange(fresh, std::memory_order_acq_rel);
}

void ConverterSlot::maintain(Setting observed) {
	Converter* c;
	while (retired.pop(c))
		delete c;
	// What the driver missed comes first; otherwise what the main thread sees now.
	uint64_t w = wanted.load(std::memory_order_relaxed);
	prepare(w ? Setting::unpack(w) : observed);
}

void convert(Converter* converter, const Setting& setting, const float* in, int inStride, int* inFrames, float* out,
	int outStride, int* outFrames) noexcept {
	if (converter) {
		converter->src.process(in, inStride, inFrames, out, outStride, outFrames);
		return;
	}
	// SampleRateConverter::process without speex state: a copy.
	int frames = std::min(*inFrames, *outFrames);
	for (int i = 0; i < frames; i++)
		for (int c = 0; c < setting.channels; c++)
			out[outStride * i + c] = in[inStride * i + c];
	*inFrames = frames;
	*outFrames = frames;
}

// --- AudioBridge -------------------------------------------------------------------------------

AudioBridge::AudioBridge(int inputs, int outputs) : inputs(inputs), outputs(outputs), toEngine(inputs), toDevice(outputs) {}

int AudioBridge::input(const float* input, int inputStride, int frames, bool master, float engineRate, float rate,
	int devIn, int devOut) noexcept {
	stats.blocks.fetch_add(1, std::memory_order_relaxed);
	if (!(rate > 0.f))
		rate = engineRate;  // no device rate: as if it matched, rather than Core's infinite ratio
	// Audio.cpp:55-57: the port's channels and rate, cached for the engine and processOutput.
	deviceIn.store(devIn, std::memory_order_relaxed);
	deviceOut.store(devOut, std::memory_order_relaxed);
	deviceRate = rate;
	float ratio = engineRate / rate;
	// Audio.cpp:77-82: a secondary port keeps latency low by clearing a ring that holds too much.
	int maxEngineFrames = (int) std::ceil(frames * ratio * 2.0) - 1;
	if (!master && (int) toEngine.size() > maxEngineFrames) {
		toEngine.requestClear();
		stats.clears.fetch_add(1, std::memory_order_relaxed);
	}
	if (devIn > 0) {
		// Audio.cpp:84-95: the master starts from an empty ring, then the block, at the engine's rate.
		if (master)
			toEngine.requestClear();
		Setting setting{(int) rate, (int) engineRate, devIn};
		Converter* converter = nullptr;
		if (setting.inRate != setting.outRate) {
			converter = toEngineSrc.acquire(setting);
			if (!converter) {
				// No converter for this setting yet: a silent block that still steps the engine in time.
				stats.resamplerMisses.fetch_add(1, std::memory_order_relaxed);
				requested = std::max((int) std::ceil(frames * ratio) - (int) toDevice.size(), 0);
				return requested;
			}
		}
		else {
			toEngineSrc.release();
		}
		int inputFrames = frames;
		int outputFrames = (int) toEngine.space();
		convert(converter, setting, input, inputStride, &inputFrames, toEngine.writeSpan(), inputs, &outputFrames);
		toEngine.produce((size_t) outputFrames);
		// Audio.cpp:97: exactly as many frames as the ring holds.
		requested = (int) toEngine.size();
	}
	else {
		// Audio.cpp:100-101: enough frames to fill the device's block.
		requested = std::max((int) std::ceil(frames * ratio) - (int) toDevice.size(), 0);
	}
	return requested;
}

void AudioBridge::output(float* output, int outputStride, int frames, float engineRate) noexcept {
	float rate = deviceRate > 0.f ? deviceRate : engineRate;
	float ratio = engineRate / rate;
	int devOut = deviceOut.load(std::memory_order_relaxed);
	if (devOut > 0) {
		// Audio.cpp:118-140: the module's frames at the device's rate, clamped, the rest of the block zeroed.
		Setting setting{(int) engineRate, (int) rate, devOut};
		Converter* converter = nullptr;
		bool ready = true;
		if (setting.inRate != setting.outRate) {
			converter = toDeviceSrc.acquire(setting);
			ready = converter != nullptr;
		}
		else {
			toDeviceSrc.release();
		}
		int outputFrames = 0;
		if (ready) {
			const float* span = toDevice.readSpan();
			int inputFrames = (int) toDevice.size();
			outputFrames = frames;
			convert(converter, setting, span, outputs, &inputFrames, output, outputStride, &outputFrames);
			toDevice.consume((size_t) inputFrames);
			for (int i = 0; i < outputFrames; i++)
				for (int j = 0; j < devOut; j++)
					output[i * outputStride + j] = rack::math::clamp(output[i * outputStride + j], -1.f, 1.f);
		}
		else {
			stats.resamplerMisses.fetch_add(1, std::memory_order_relaxed);
			toDevice.requestClear();  // what could not be converted is dropped
		}
		for (int i = outputFrames; i < frames; i++)
			for (int j = 0; j < devOut; j++)
				output[i * outputStride + j] = 0.f;
	}
	// Audio.cpp:146-150: keep latency low.
	int maxEngineFrames = (int) std::ceil(frames * ratio * 2.0) - 1;
	if ((int) toDevice.size() > maxEngineFrames) {
		toDevice.requestClear();
		stats.clears.fetch_add(1, std::memory_order_relaxed);
	}
}

void AudioBridge::startStream() noexcept {
	// Audio.cpp:155-159.
	toEngine.requestClear();
	toDevice.requestClear();
}

void AudioBridge::stopStream() noexcept {
	// Audio.cpp:161-166; portStop unsets the master (169-170).
	deviceIn.store(0, std::memory_order_relaxed);
	deviceOut.store(0, std::memory_order_relaxed);
	toEngine.requestClear();
	toDevice.requestClear();
}

void AudioBridge::shift(float* frame) noexcept {
	// Audio.cpp:315-334: the oldest device frame, or 0 V.
	const float* oldest = toEngine.readSpan();
	if (toEngine.size() > 0) {
		std::copy_n(oldest, inputs, frame);
		toEngine.consume(1);
		return;
	}
	std::fill_n(frame, inputs, 0.f);
	if (deviceIn.load(std::memory_order_relaxed) > 0)
		stats.emptyFrames.fetch_add(1, std::memory_order_relaxed);
}

void AudioBridge::push(const float* frame) noexcept {
	// Audio.cpp:261-297: a frame for the device while it has outputs, if the ring is not full.
	int devOut = deviceOut.load(std::memory_order_relaxed);
	if (devOut <= 0)
		return;
	if (toDevice.space() == 0) {
		stats.droppedFrames.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	float* slot = toDevice.writeSpan();
	for (int c = 0; c < outputs; c++)
		slot[c] = c < devOut ? frame[c] : 0.f;
	toDevice.produce(1);
}

void AudioBridge::sampleRateChanged() noexcept {
	// Audio.cpp:248-250.
	toEngine.requestClear();
	toDevice.requestClear();
}

void AudioBridge::portInput(rack::audio::Port& port, rack::engine::Module* module, bool engineOwned, const float* in,
	int inputStride, int frames) noexcept {
	try {
		// Audio.cpp:55-71. Rack's Port getters catch the driver's exceptions themselves.
		int devIn = std::min(port.getNumInputs(), inputs);
		int devOut = std::min(port.getNumOutputs(), outputs);
		float rate = port.getSampleRate();
		rack::Context* context = rack::contextGet();
		rack::engine::Engine* engine = context ? context->engine : nullptr;
		bool master = false;
		// Only a module in Rack's engine may become master (Core's always is).
		if (engine && module && engineOwned) {
			if (!engine->getMasterModule())
				engine->setMasterModule(module);
			master = engine->getMasterModule() == module;
			if (master)
				engine->setSuggestedSampleRate(rate);
		}
		input(in, inputStride, frames, master, engine ? engine->getSampleRate() : rate, rate, devIn, devOut);
	}
	catch (...) {
	}
}

void AudioBridge::portBuffer(rack::engine::Module* module) noexcept {
	try {
		// Audio.cpp:105-111.
		rack::Context* context = rack::contextGet();
		rack::engine::Engine* engine = context ? context->engine : nullptr;
		if (engine && module && requested > 0 && engine->getMasterModule() == module) {
			engine->stepBlock(requested);
			stats.steppedFrames.fetch_add((uint64_t) requested, std::memory_order_relaxed);
		}
	}
	catch (...) {
	}
}

void AudioBridge::portOutput(float* out, int outputStride, int frames) noexcept {
	rack::Context* context = rack::contextGet();
	float engineRate = context && context->engine ? context->engine->getSampleRate() : deviceRate;
	output(out, outputStride, frames, engineRate);
}

void AudioBridge::portStart() noexcept {
	startStream();
}

void AudioBridge::portStop(rack::engine::Module* module) noexcept {
	stopStream();
	// Audio.cpp:167-170: Rack may hold its engine's write lock here, so the variant without a lock.
	rack::Context* context = rack::contextGet();
	if (context && context->engine && module && context->engine->getMasterModule() == module)
		context->engine->setMasterModule_NoLock(nullptr);
}

namespace {

/** The settings Core's outputSrc and inputSrc would have for what the main thread observes; none without a rate. */
std::pair<Setting, Setting> observed(float rate, float engineRate, int devIn, int devOut) {
	if (!(rate > 0.f) || !(engineRate > 0.f))
		return {};
	return {Setting{(int) rate, (int) engineRate, devIn}, Setting{(int) engineRate, (int) rate, devOut}};
}

}  // namespace

void AudioBridge::prepare(float rate, float engineRate, int devIn, int devOut) {
	auto settings = observed(rate, engineRate, std::min(devIn, inputs), std::min(devOut, outputs));
	toEngineSrc.prepare(settings.first);
	toDeviceSrc.prepare(settings.second);
}

void AudioBridge::maintain(float rate, float engineRate, int devIn, int devOut) {
	auto settings = observed(rate, engineRate, std::min(devIn, inputs), std::min(devOut, outputs));
	toEngineSrc.maintain(settings.first);
	toDeviceSrc.maintain(settings.second);
}

}  // namespace io
}  // namespace rackpy
