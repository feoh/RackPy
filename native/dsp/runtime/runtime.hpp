// SPDX-License-Identifier: GPL-3.0-or-later
// Per-module kernel runtime (RP-17A). Python-free: the engine-thread half is
// the production audio path, and the main-thread half only moves ownership.
#pragma once
#include <rack.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rackpy/kernel.hpp"
#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace dsp {

/** Fixed-capacity single-producer, single-consumer ring: never blocks or allocates. */
template <typename T, size_t N>
struct SpscRing {
	static_assert((N & (N - 1)) == 0, "N must be a power of two");
	std::array<T, N> items{};
	std::atomic<size_t> head{0};  // advanced by the consumer
	std::atomic<size_t> tail{0};  // advanced by the producer

	bool push(const T& value) {
		size_t t = tail.load(std::memory_order_relaxed);
		if (t - head.load(std::memory_order_acquire) == N)
			return false;
		items[t & (N - 1)] = value;
		tail.store(t + 1, std::memory_order_release);
		return true;
	}
	bool pop(T& value) {
		size_t h = head.load(std::memory_order_relaxed);
		if (h == tail.load(std::memory_order_acquire))
			return false;
		value = items[h & (N - 1)];
		head.store(h + 1, std::memory_order_release);
		return true;
	}
	size_t size() const { return tail.load(std::memory_order_acquire) - head.load(std::memory_order_acquire); }
	/** Consumer side: whether anything may be waiting (a cheap check before pop()). */
	bool maybeNonEmpty() const { return tail.load(std::memory_order_relaxed) != head.load(std::memory_order_relaxed); }
};

/** A kernel, its type, and the token Python handles check. Created and
deleted on the main thread; the engine thread only calls `kernel`. */
struct Hosted {
	std::unique_ptr<Kernel> kernel;
	const KernelType* type = nullptr;
	/** For a type that is not registered (a graph plan's, RP-21B): keeps `type` alive as long as the kernel. */
	std::shared_ptr<const KernelType> ownedType;
	std::shared_ptr<runtime::ObjectToken> token = std::make_shared<runtime::ObjectToken>(nullptr);
	bool started = false;  // engine thread: sampleRateChange sent before the first frame
	std::vector<double> values;  // main thread: the fields as created, controls as last set
	~Hosted() { token->invalidate(); }
};

constexpr size_t CONTROL_CAPACITY = 256;
constexpr size_t RETIRED_CAPACITY = 4;

/** A module's kernel. Main thread: attach(), set(), reclaim(). Engine thread:
process(). Rack's reset and sample-rate events run with the engine stopped. */
struct KernelSlot {
	struct Control {
		Hosted* target;
		int slot;
		double value;
	};

	~KernelSlot();

	// --- engine thread (the production audio path) ---------------------------
	void process(rack::engine::Module& module, const rack::engine::Module::ProcessArgs& args) noexcept;
	void reset() noexcept;
	void sampleRateChange(float sampleRate, float sampleTime) noexcept;

	// --- main thread -------------------------------------------------------------
	/** Hand `next` to the module. While Rack's engine runs the module, it
	is picked up at the next frame boundary and the previous kernel is
	retired for reclaim(); otherwise it replaces the previous one at once. */
	void attach(std::unique_ptr<Hosted> next, bool engineRunsModule);
	/** Send a control value to `target`, which must be this module's current
	or pending kernel. Directly when the engine does not run the module;
	otherwise through the bounded queue. False when the queue is full. */
	bool set(Hosted* target, int slot, double value, bool engineRunsModule);
	/** Delete kernels the engine has retired. */
	void reclaim();
	/** The kernel Python last attached, applied or not; null for none. Main thread. */
	Hosted* latest() const { return latestHosted; }
	bool failed() const { return failure.load(std::memory_order_acquire); }
	size_t queued() const { return controls.size(); }

private:
	enum class Run : uint8_t { IDLE, START, RUNNING, FAILED };
	// Engine thread only (or with the engine stopped): the hot path checks these first.
	Hosted* current = nullptr;
	Kernel* currentKernel = nullptr;
	Run run = Run::IDLE;
	std::atomic<Hosted*> pending{nullptr};  // handed over by the main thread
	SpscRing<Control, CONTROL_CAPACITY> controls;  // main -> engine
	SpscRing<Hosted*, RETIRED_CAPACITY> retired;  // engine -> main
	std::atomic<bool> failure{false};
	Hosted* latestHosted = nullptr;  // main thread
	void silence(rack::engine::Module& module) noexcept;
	void take(Hosted* next) noexcept;
	void slow(rack::engine::Module& module, const rack::engine::Module::ProcessArgs& args) noexcept;
	void fail(rack::engine::Module& module) noexcept;
};

/** The registered kernel types (main thread). */
void registerType(KernelType type);
const KernelType* findType(const std::string& name);
std::vector<const KernelType*> types();
/** RackPy's own kernel types, "rackpy.Gain" and "rackpy.Sine". */
void registerBuiltins();
/** The built-ins, then each library (relative to `pluginDir`) in order; a
library that fails is logged and skipped, with none of its types added. Main thread. */
void loadKernelLibraries(const std::string& pluginDir, const std::vector<std::string>& libraries);

/** Check `values` (one per field) against `type` and the module's
configuration: whole-number indices in range, required fields present.
Throws std::invalid_argument naming the field. Main thread. */
void checkWiring(const KernelType& type, const std::vector<double>& values, const rack::engine::Module& module);

}  // namespace dsp
}  // namespace rackpy
