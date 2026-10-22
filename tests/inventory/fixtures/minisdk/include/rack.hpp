// SPDX-License-Identifier: GPL-3.0-or-later
// Inventory fixture: an original miniature SDK that imitates rack.hpp's patterns.
#pragma once

#ifdef __clang__
#define PRIVATE __attribute__((deprecated("Using internal Rack function or symbol")))
#else
#define PRIVATE __attribute__((error("Using internal Rack function or symbol")))
#endif
#define DEPRECATED __attribute__((deprecated))

#include <shapes.hpp>
#include <platform.hpp>

namespace mini {
using namespace shapes;
}
