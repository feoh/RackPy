// SPDX-License-Identifier: GPL-3.0-or-later
// RP-30: Rack's services. string, random and logger belong to family 1 (bind_common), the rest to family 8.
#pragma once
#include <pybind11/pybind11.h>

#include <string>

namespace rackpy {
namespace bindings {
namespace services {

/** rackpy.raw.string (RP-30A). */
void bindString(pybind11::module_ string);
/** rackpy.raw.random (RP-30A). */
void bindRandom(pybind11::module_ random);
/** rackpy.raw.logger (RP-30A). */
void bindLogger(pybind11::module_ logger);
/** rackpy.raw.system (RP-30B). */
void bindSystem(pybind11::module_ system);
/** rackpy.raw.network (RP-30B). */
void bindNetwork(pybind11::module_ network);
/** A path argument: str or os.PathLike, as a UTF-8 string. Needs the GIL. */
std::string pathArg(pybind11::handle value);
/** printf-style formatting through Rack's string::f, one checked conversion at a time (RP-30A). */
std::string formatPrintf(const char* format, pybind11::sequence args);

}  // namespace services
}  // namespace bindings
}  // namespace rackpy
