// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded runtime diagnostics. Any thread may record one, including the audio
// path: recording takes no lock, allocates nothing, does no IO and never calls
// Python. The UI thread reads them later (rackpy.runtime.diagnostics()).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace runtime {

enum class DiagnosticCode : uint16_t {
	/** Rack tried to run Python on one of its own threads; the call was refused. */
	RefusedRackThread = 1,
	/** A Python callback raised; the exception was caught at the Rack boundary. */
	CallbackRaised,
	/** A Rack context other than the bound one asked for Python work and was refused. */
	ForeignContext,
	/** The deferred-release queue was full, so a Python reference was leaked rather than dropped unsafely. */
	ReleaseQueueFull,
	/** Shutdown found Python references still held by native objects. */
	ShutdownLeak,
	/** A Python callback that Rack runs with audio stopped exceeded its time budget. */
	SlowExclusiveCallback,
	/** A test hook; never recorded by production code. */
	Test,
};

const char* codeName(DiagnosticCode code);

struct Diagnostic {
	uint64_t sequence;  // 1, 2, 3, ... in recording order
	double time;  // rack::system::getTime() when recorded
	DiagnosticCode code;
	ThreadRole role;
	std::string message;
};

/** Number of records kept; older ones are overwritten. */
constexpr size_t DIAGNOSTIC_CAPACITY = 256;
/** Longest message kept, in bytes; longer messages are truncated. */
constexpr size_t DIAGNOSTIC_MESSAGE_BYTES = 160;

/** Record a diagnostic. Safe on any thread, including the audio path. */
void diagnose(DiagnosticCode code, const char* message);

/** Records with a sequence number greater than `after`, oldest first. UI thread; allocates. */
std::vector<Diagnostic> readDiagnostics(uint64_t after = 0);

/** How many diagnostics have ever been recorded (including overwritten ones). */
uint64_t diagnosticsRecorded();

/** How many times each code has been recorded. */
uint64_t diagnosticCount(DiagnosticCode code);

}  // namespace runtime
}  // namespace rackpy
