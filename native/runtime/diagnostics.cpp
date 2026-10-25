// SPDX-License-Identifier: GPL-3.0-or-later
#include "rackpy/runtime/diagnostics.hpp"

#include <atomic>
#include <cstring>

namespace rackpy {
namespace runtime {

namespace {

constexpr size_t WORDS = DIAGNOSTIC_MESSAGE_BYTES / sizeof(uint64_t);
constexpr size_t CODES = 16;

/** One record, guarded by a sequence lock. Every field is atomic, so a torn read is detected, never undefined. */
struct Slot {
	// 0: never written. Odd: being written. Even 2n: holds record n.
	std::atomic<uint64_t> seq{0};
	std::atomic<double> time{0};
	std::atomic<uint16_t> code{0};
	std::atomic<uint8_t> role{0};
	std::atomic<uint64_t> words[WORDS] = {};
};

Slot slots[DIAGNOSTIC_CAPACITY];
std::atomic<uint64_t> next{0};
std::atomic<uint64_t> counts[CODES] = {};

}  // namespace

const char* codeName(DiagnosticCode code) {
	switch (code) {
		case DiagnosticCode::RefusedRackThread: return "refused_rack_thread";
		case DiagnosticCode::CallbackRaised: return "callback_raised";
		case DiagnosticCode::ForeignContext: return "foreign_context";
		case DiagnosticCode::ReleaseQueueFull: return "release_queue_full";
		case DiagnosticCode::ShutdownLeak: return "shutdown_leak";
		case DiagnosticCode::SlowExclusiveCallback: return "slow_exclusive_callback";
		case DiagnosticCode::KernelFailed: return "kernel_failed";
		case DiagnosticCode::Test: return "test";
	}
	return "unknown";
}

void diagnose(DiagnosticCode code, const char* message) {
	uint64_t n = next.fetch_add(1, std::memory_order_relaxed) + 1;
	counts[(size_t) code % CODES].fetch_add(1, std::memory_order_relaxed);
	Slot& s = slots[(n - 1) % DIAGNOSTIC_CAPACITY];
	s.seq.store(2 * n - 1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);
	s.time.store(rack::system::getTime(), std::memory_order_relaxed);
	s.code.store((uint16_t) code, std::memory_order_relaxed);
	s.role.store((uint8_t) threadRole(), std::memory_order_relaxed);
	char buffer[DIAGNOSTIC_MESSAGE_BYTES] = {};
	if (message)
		std::strncpy(buffer, message, DIAGNOSTIC_MESSAGE_BYTES - 1);
	for (size_t i = 0; i < WORDS; i++) {
		uint64_t word;
		std::memcpy(&word, buffer + i * sizeof(uint64_t), sizeof(word));
		s.words[i].store(word, std::memory_order_relaxed);
	}
	s.seq.store(2 * n, std::memory_order_release);
}

std::vector<Diagnostic> readDiagnostics(uint64_t after) {
	std::vector<Diagnostic> out;
	uint64_t last = next.load(std::memory_order_acquire);
	uint64_t first = last > DIAGNOSTIC_CAPACITY ? last - DIAGNOSTIC_CAPACITY + 1 : 1;
	if (after + 1 > first)
		first = after + 1;
	for (uint64_t n = first; n <= last; n++) {
		const Slot& s = slots[(n - 1) % DIAGNOSTIC_CAPACITY];
		uint64_t before = s.seq.load(std::memory_order_acquire);
		if (before != 2 * n)
			continue;  // being written, or already overwritten by a newer record
		Diagnostic d;
		d.sequence = n;
		d.time = s.time.load(std::memory_order_relaxed);
		d.code = (DiagnosticCode) s.code.load(std::memory_order_relaxed);
		d.role = (ThreadRole) s.role.load(std::memory_order_relaxed);
		char buffer[DIAGNOSTIC_MESSAGE_BYTES];
		for (size_t i = 0; i < WORDS; i++) {
			uint64_t word = s.words[i].load(std::memory_order_relaxed);
			std::memcpy(buffer + i * sizeof(uint64_t), &word, sizeof(word));
		}
		std::atomic_thread_fence(std::memory_order_acquire);
		if (s.seq.load(std::memory_order_relaxed) != before)
			continue;  // overwritten while we read it
		buffer[DIAGNOSTIC_MESSAGE_BYTES - 1] = '\0';
		d.message = buffer;
		out.push_back(std::move(d));
	}
	return out;
}

uint64_t diagnosticsRecorded() {
	return next.load(std::memory_order_relaxed);
}

uint64_t diagnosticCount(DiagnosticCode code) {
	return counts[(size_t) code % CODES].load(std::memory_order_relaxed);
}

}  // namespace runtime
}  // namespace rackpy
