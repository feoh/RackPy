// SPDX-License-Identifier: GPL-3.0-or-later
// Python handles for native kernels (RP-17A): _rackpy_host.dsp, wrapped by rackpy.dsp.kernel.
#pragma once
#include <pybind11/pybind11.h>

#include "../engine/module.hpp"

namespace rackpy {
namespace bindings {

void bindKernels(pybind11::module_ host);
/** Module.set_kernel: check the kernel's wiring against `module` and hand it over. Main thread. */
void attachKernel(const ModuleRef& module, pybind11::handle kernel, bool bypass = false);
/** Module.kernel: the module's latest kernel as a handle, or None. */
pybind11::object moduleKernel(const ModuleRef& module, bool bypass = false);

}  // namespace bindings
}  // namespace rackpy
