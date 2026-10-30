// SPDX-License-Identifier: GPL-3.0-or-later
// A module's audio port (RP-31J): Rack's Core Audio buffering between an
// audio device's driver thread and the module's kernel on Rack's engine
// thread, Python-free.
//
// Rack calls a port's processInput, processBuffer and processOutput on the
// audio driver's thread, and the module's process() on an engine thread,
// which is that same driver thread while the port's module is Rack's master
// (the port steps Rack's engine in processBuffer). Between them, as Core's
// AudioPort (src/core/Audio.cpp), two rings of 32768 frames: the device's
// input, resampled to the engine's rate, and the module's frames, resampled
// to the device's rate. Each ring has one producer and one consumer and
// never blocks or allocates; Rack's clear() becomes a request any thread may
// make, applied by the consumer, so that the producer never moves the
// consumer's index.
//
// Rack's SampleRateConverter reallocates its speex state whenever its rates
// or channel count change, which the driver thread must not do. RackPy's main
// thread prepares fresh converters for the current or missed setting and,
// separately, a requested upcoming rate. Atomic handovers cannot displace
// the current converter; the driver retires converters it stops using through
// a ring that the main thread empties. A block
// whose setting has no converter ready yet is silent and counted.
//
// As Core Audio does, a port can claim the master, suggest its device rate,
// and step Rack's engine. These public Rack engine methods take Rack's own
// locks; RackPy's exchange code (input, output, shift, push) adds none.
#pragma once
#include <rack.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "../dsp/runtime/runtime.hpp"
#include "rackpy/io.hpp"

namespace rackpy {
namespace io {

/** Frames in each ring: Core Audio's DoubleRingBuffer size. */
constexpr size_t AUDIO_RING_FRAMES = 32768;
/** Core Audio's resampling quality (SampleRateConverter::setQuality). */
constexpr int RESAMPLER_QUALITY = 6;

/** Interleaved frames of `stride` floats with one producer and one consumer, stored twice so that any run of
frames up to the ring's size is contiguous (as Rack's dsp::DoubleRingBuffer). Allocated, zeroed, on the main
thread. */
class FrameRing {
public:
	explicit FrameRing(int stride);
	const int stride;

	/** Any thread: everything produced so far is dropped when the consumer next reads (Rack's clear()). */
	void requestClear() noexcept;
	/** Frames produced and neither consumed nor cleared (exact on the consumer's thread). */
	size_t size() const noexcept;

	// --- producer ---
	/** Frames the producer may write now. */
	size_t space() const noexcept;
	/** Where the next frames go: room for space() frames, contiguous. */
	float* writeSpan() noexcept;
	/** Publishes `frames` frames written at writeSpan(). */
	void produce(size_t frames) noexcept;

	// --- consumer ---
	/** Applies a pending clear, then where the oldest of size() frames start, contiguous. */
	const float* readSpan() noexcept;
	void consume(size_t frames) noexcept;

private:
	std::unique_ptr<float[]> data;
	std::atomic<size_t> head{0};  // advanced by the consumer
	std::atomic<size_t> tail{0};  // advanced by the producer
	std::atomic<size_t> clearTo{0};  // raised by requestClear()
};

/** What Rack's SampleRateConverter is set to: it starts afresh whenever this changes. Rates are whole Hz, as
SampleRateConverter::setRates takes them. */
struct Setting {
	int inRate = 0, outRate = 0, channels = 0;
	bool operator==(const Setting& o) const { return inRate == o.inRate && outRate == o.outRate && channels == o.channels; }
	bool operator!=(const Setting& o) const { return !(*this == o); }
	/** 0 for none; otherwise the three values, nonzero. */
	uint64_t pack() const noexcept;
	static Setting unpack(uint64_t packed) noexcept;
};

/** A resampler for one setting, made fresh on the main thread (speex allocates). */
struct Converter {
	explicit Converter(Setting setting);
	const Setting setting;
	rack::dsp::SampleRateConverter<MAX_AUDIO_CHANNELS> src;
	/** Driver thread: used, so no longer fresh; it goes once it is retired. */
	bool spent = false;
};

/** One direction's converter, handed from the main thread to the driver thread. */
class ConverterSlot {
public:
	ConverterSlot() = default;
	ConverterSlot(const ConverterSlot&) = delete;
	ConverterSlot& operator=(const ConverterSlot&) = delete;
	/** Main thread, with no driver using the slot. */
	~ConverterSlot();

	/** Driver thread: the converter for `setting`, picking up a prepared one if needed; null (a miss) when none is
	ready. */
	Converter* acquire(Setting setting) noexcept;
	/** Driver thread: the rates are equal, so Rack's converter has no state; the next unequal setting starts afresh. */
	void release() noexcept;

	/** Main thread: a fresh converter for `setting`, unless the driver uses or has been handed one. */
	void prepare(Setting setting);
	/** Main thread: prepare a future setting without replacing the converter for the current setting. */
	void prepareFuture(Setting setting);
	/** Main thread: frees what the driver retired, and prepares what it last missed, or else `observed`. */
	void maintain(Setting observed);
	/** The setting the driver's converter has (0: none), for tests. */
	uint64_t current() const noexcept { return inUse.load(std::memory_order_acquire); }

private:
	Converter* using_ = nullptr;  // driver thread
	std::atomic<Converter*> prepared{nullptr};  // main thread -> driver thread, current or missed setting
	std::atomic<Converter*> future{nullptr};  // main thread -> driver thread, anticipated setting
	// 0 = empty; FUTURE_BUSY = transient nonblocking reservation; otherwise
	// the packed setting of the pointer. Positive int input rates cannot pack
	// to UINT64_MAX. Both threads reserve before changing the pointer.
	static constexpr uint64_t FUTURE_BUSY = UINT64_MAX;
	std::atomic<uint64_t> futureSetting{0};
	dsp::SpscRing<Converter*, 8> retired;  // driver thread -> main thread
	std::atomic<uint64_t> wanted{0};  // the last setting the driver missed
	std::atomic<uint64_t> inUse{0};  // using_'s setting
	bool retire() noexcept;
};

/** Counters, written on the driver and engine threads, read by Python. */
struct AudioStats {
	std::atomic<uint64_t> blocks{0};  // processInput calls
	std::atomic<uint64_t> steppedFrames{0};  // frames this port stepped Rack's engine by, as master
	std::atomic<uint64_t> droppedFrames{0};  // frames a full ring refused
	std::atomic<uint64_t> emptyFrames{0};  // module frames with no device frame, while the device had inputs
	std::atomic<uint64_t> clears{0};  // Core's "too full" clears, keeping latency low
	std::atomic<uint64_t> resamplerMisses{0};  // blocks silent for want of a prepared converter
};

/** One module's audio port, as Core's AudioPort buffers it. `inputs` and `outputs` (0 to 16) are the device
channels the module gets and sends, the port's maxInputs and maxOutputs. Made and deleted on the main thread. */
class AudioBridge {
public:
	AudioBridge(int inputs, int outputs);
	const int inputs, outputs;
	AudioStats stats;

	// --- the driver thread: Core's AudioPort around Rack's engine (Rack's context set, as Device does) ---
	/** Claim master if free, suggest the device rate when master, then run input(). */
	void portInput(rack::audio::Port& port, rack::engine::Module* module, bool engineOwned, const float* input,
		int inputStride, int frames) noexcept;
	/** Step Rack's engine for the frames requested by input() when this module is master. */
	void portBuffer(rack::engine::Module* module) noexcept;
	/** Core's processOutput: output() at the engine's rate. */
	void portOutput(float* output, int outputStride, int frames) noexcept;
	/** Core's onStartStream; any thread. */
	void portStart() noexcept;
	/** Stop the port and unset its module as master with Rack's no-lock variant, as Core does. */
	void portStop(rack::engine::Module* module) noexcept;

	/** The device's block, resampled into the module's ring. `deviceInputs` and `deviceOutputs` are the port's
	getNumInputs and getNumOutputs, at most `inputs` and `outputs`. Returns the frames a master steps. */
	int input(const float* input, int inputStride, int frames, bool master, float engineRate, float deviceRate,
		int deviceInputs, int deviceOutputs) noexcept;
	/** The module's frames, resampled into the device's block, clamped to ±1, the rest zeroed. */
	void output(float* output, int outputStride, int frames, float engineRate) noexcept;
	/** Rack's clears at a stream's start, and its stop. */
	void startStream() noexcept;
	void stopStream() noexcept;

	// --- the engine thread, once per module frame ---
	/** The oldest device frame into `frame` (`inputs` floats), or zeros. */
	void shift(float* frame) noexcept;
	/** `frame` (`outputs` floats) to the device, the channels it lacks zeroed, if it has outputs and there is room. */
	void push(const float* frame) noexcept;
	/** Core's onSampleRateChange: both rings start over. */
	void sampleRateChanged() noexcept;

	// --- the main thread ---
	/** Converters for the rates and channels the main thread observes now. */
	void prepare(float deviceRate, float engineRate, int deviceInputs, int deviceOutputs);
	/** Prepare the current or missed setting and, separately, a future rate; free retired converters. UI thread. */
	void maintain(float deviceRate, float engineRate, int deviceInputs, int deviceOutputs, float futureEngineRate = 0.f);
	/** The device's input and output channels, as the driver last saw them. */
	int deviceInputs() const noexcept { return deviceIn.load(std::memory_order_relaxed); }
	int deviceOutputs() const noexcept { return deviceOut.load(std::memory_order_relaxed); }
	/** Frames waiting in each ring (tests). */
	size_t toEngineFrames() const noexcept { return toEngine.size(); }
	size_t toDeviceFrames() const noexcept { return toDevice.size(); }
	const ConverterSlot& toEngineConverter() const noexcept { return toEngineSrc; }
	const ConverterSlot& toDeviceConverter() const noexcept { return toDeviceSrc; }

private:
	FrameRing toEngine;  // the device's input for the module (Core's engineOutputBuffer)
	FrameRing toDevice;  // the module's frames for the device (Core's engineInputBuffer)
	ConverterSlot toEngineSrc, toDeviceSrc;  // Core's outputSrc and inputSrc
	std::atomic<int> deviceIn{0}, deviceOut{0};  // Core's deviceNumInputs and deviceNumOutputs
	// Driver thread only.
	float deviceRate = 0.f;  // Core's deviceSampleRate
	int requested = 0;  // Core's requestedEngineFrames
};

/** Converts `frames` frames for `setting`'s channels with `converter`, or copies them when the rates are equal (Rack's
converter without state). Updates both counts as SampleRateConverter::process does. */
void convert(Converter* converter, const Setting& setting, const float* in, int inStride, int* inFrames, float* out,
	int outStride, int* outFrames) noexcept;

}  // namespace io
}  // namespace rackpy
