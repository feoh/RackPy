// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tree.hpp"
#include <functional>

namespace rackpy {
namespace widget {

// Copy the checked locator, NOT the cached shared Handle. Field views must
// survive attached-wrapper collection but must not keep a detached root alive.
// The Node's weak handle still refers only to its owning/identity wrapper.
struct GeometryOwner {
	std::function<rack::widget::Widget&()> resolve;
	GeometryOwner(Handle handle) : resolve([handle] () -> rack::widget::Widget& { return handle.get(); }) {}
	GeometryOwner(std::function<rack::widget::Widget&()> checked) : resolve(std::move(checked)) {}
	rack::widget::Widget& get() const { return resolve(); }
};
struct BoxView {
	GeometryOwner owner;
	rack::math::Rect& get() const { return owner.get().box; }
};

struct VecView {
	GeometryOwner owner;
	bool size;
	std::function<rack::math::Vec&()> field = {};
	rack::math::Vec& get() const {
		if (field) return field();
		auto& box = owner.get().box;
		return size ? box.size : box.pos;
	}
};

}  // namespace widget
}  // namespace rackpy
