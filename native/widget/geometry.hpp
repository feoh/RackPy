// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tree.hpp"

namespace rackpy {
namespace widget {

// Copy the checked locator, NOT the cached shared Handle. Field views must
// survive attached-wrapper collection but must not keep a detached root alive.
// The Node's weak handle still refers only to its owning/identity wrapper.
struct BoxView {
	Handle owner;
	rack::math::Rect& get() const { return owner.get().box; }
};

struct VecView {
	Handle owner;
	bool size;
	rack::math::Vec& get() const {
		auto& box = owner.get().box;
		return size ? box.size : box.pos;
	}
};

}  // namespace widget
}  // namespace rackpy
