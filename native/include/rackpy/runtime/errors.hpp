// SPDX-License-Identifier: GPL-3.0-or-later
// C++ exceptions the runtime throws inside binding code. The host translates
// each one to the matching class in rackpy.errors (src/rackpy/errors.py); none
// may cross a Rack callback or the loader's C ABI.
#pragma once
#include <stdexcept>
#include <string>

namespace rackpy {
namespace runtime {

struct Error : std::runtime_error {
	using std::runtime_error::runtime_error;
};

/** A handle whose Rack object, or whose Rack context, no longer exists. -> rackpy.errors.StaleHandleError */
struct StaleHandle : Error {
	using Error::Error;
};

/** A handle used with, or from, a Rack context it does not belong to. -> rackpy.errors.ContextError */
struct WrongContext : Error {
	using Error::Error;
};

/** An operation called on a thread that may not perform it. -> rackpy.errors.ThreadError */
struct WrongThread : Error {
	using Error::Error;
};

/** An ownership transfer that cannot happen, e.g. handing one object to Rack twice. -> rackpy.errors.OwnershipError */
struct Ownership : Error {
	using Error::Error;
};

/** A locking engine call inside a callback that Rack runs under the exclusive engine lock. -> rackpy.errors.EngineLockError */
struct EngineLocked : Error {
	using Error::Error;
};

}  // namespace runtime
}  // namespace rackpy
