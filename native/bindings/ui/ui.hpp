// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <memory>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace widgets {

/** rackpy.raw.ui (RP-26A). Needs rackpy.raw.widget bound first. */
void bindUi(pybind11::module_ raw);
/** rackpy.raw.widget.EventState (RP-26B). */
void bindEventState(pybind11::module_ widget);
/** The EventState handle of Rack's context, or None. Main thread. */
pybind11::object eventStateObject(const std::shared_ptr<runtime::ContextToken>& token);

}  // namespace widgets
}  // namespace rackpy
