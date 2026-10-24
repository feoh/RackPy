// SPDX-License-Identifier: GPL-3.0-or-later
// json_t <-> Python values: the boundary policy for every Rack API that takes
// or returns JSON (docs/coverage.md policy 13; docs/reference/values.md).
//
// - null, true/false, integer, real, string, array and object map to None,
//   bool, int, float, str, list and dict, and back (tuples become arrays).
// - Integers outside json_int_t (long long) raise OverflowError.
// - NaN and infinities raise ValueError: JSON cannot represent them.
// - Object keys must be str (TypeError otherwise); other types raise TypeError.
// - Strings and keys cannot contain NUL (ValueError): Rack loads JSON without
//   JSON_ALLOW_NUL, so a saved "\u0000" would make the whole patch unloadable.
// - From json_t, strings that are not valid UTF-8 (only json_string_nocheck can
//   make them) raise UnicodeDecodeError.
// - Nesting deeper than JSON_MAX_DEPTH raises ValueError, which also stops
//   reference cycles.
// Needs the GIL. Converts, never shares: Python never holds a json_t.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

namespace rackpy {
namespace runtime {

constexpr int JSON_MAX_DEPTH = 64;

/** A new Python value for `json` (borrowed; may be null, which is None). */
pybind11::object fromJson(const json_t* json);

/** A new json_t reference for `value`; the caller owns it. Throws a Python error (as
pybind11 exceptions) for anything the policy above refuses. */
json_t* toJson(pybind11::handle value);

}  // namespace runtime
}  // namespace rackpy
