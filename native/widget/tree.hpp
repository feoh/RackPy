// SPDX-License-Identifier: GPL-3.0-or-later
// Detached native-only widgets. No Python virtuals, no scene registration.
#pragma once
#include <rack.hpp>

#include <atomic>
#include <map>
#include <memory>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace widget {

struct Manager;
struct Node;
struct Handle {
	std::shared_ptr<Manager> manager;
	Node* ptr;
	std::shared_ptr<runtime::ObjectToken> token;
	~Handle();
	Node& get() const;
};

struct Node final : rack::widget::Widget {
	std::weak_ptr<Handle> handle;
	std::shared_ptr<runtime::ObjectToken> token;
	explicit Node(std::shared_ptr<runtime::ContextToken> context)
		: token(std::make_shared<runtime::ObjectToken>(std::move(context))) {}
	~Node() override {
		token->invalidate();
		if (auto h = handle.lock())
			h->ptr = nullptr;
	}
};

/** Owns native roots, not Python objects. A handle's last release only sets
an atomic collection flag, even on a Python worker; only the UI scans/deletes
unreferenced roots. Context shutdown closes all roots before Rack deletes
EventState. Not an audio API. */
struct Manager : std::enable_shared_from_this<Manager> {
	std::shared_ptr<runtime::ContextToken> context;
	std::map<uint64_t, Node*> roots;
	std::atomic<bool> collect{false};

	explicit Manager(std::shared_ptr<runtime::ContextToken> context) : context(std::move(context)) {}
	void check() const {
		runtime::requireMainThread("using detached widget trees");
		runtime::checkContext(*context);
		if (!context->context->event)
			throw runtime::WrongContext("widget trees require a live Rack EventState");
	}
	void retire() noexcept { collect.store(true, std::memory_order_release); }
	void drain() {
		check();
		if (!collect.exchange(false, std::memory_order_acquire))
			return;
		for (auto it = roots.begin(); it != roots.end();) {
			if (it->second->handle.expired()) {
				auto* node = it->second;
				it = roots.erase(it);
				delete node;
			}
			else
				++it;
		}
	}
	void shutdown() {
		check();
		while (!roots.empty()) {
			auto* node = roots.begin()->second;
			roots.erase(roots.begin());
			delete node;
		}
	}
	std::shared_ptr<Handle> view(Node& node) {
		if (auto held = node.handle.lock())
			return held;
		auto held = std::shared_ptr<Handle>(new Handle{shared_from_this(), &node, node.token});
		node.handle = held;
		return held;
	}
	std::shared_ptr<Handle> create() {
		drain();
		auto node = std::make_unique<Node>(context);
		auto held = view(*node);
		roots.emplace(node->token->generation, node.get());
		node.release();
		return held;
	}
	void add(const Handle& parent, const Handle& child, bool bottom = false) {
		auto& p = parent.get();
		auto& c = child.get();
		if (parent.manager.get() != this || child.manager.get() != this)
			throw runtime::WrongContext("widgets belong to different contexts");
		if (c.parent || &c == &p || p.isDescendantOf(&c))
			throw runtime::Ownership("child must be detached and cannot be its own ancestor");
		try {
			if (bottom)
				p.addChildBottom(&c);
			else
				p.addChild(&c);
		}
		catch (...) {
			// SDK sets parent before allocating its list node. Base Node callbacks
			// cannot throw; allocation failure must leave the orphan owned here.
			c.parent = nullptr;
			throw;
		}
		roots.erase(c.token->generation);
	}
	void addRelative(const Handle& parent, const Handle& child, const Handle& sibling, bool above) {
		auto& p = parent.get();
		auto& c = child.get();
		auto& s = sibling.get();
		if (parent.manager.get() != this || child.manager.get() != this || sibling.manager.get() != this)
			throw runtime::WrongContext("widgets belong to different contexts");
		if (c.parent || &c == &p || p.isDescendantOf(&c))
			throw runtime::Ownership("child must be detached and cannot be its own ancestor");
		if (s.parent != &p)
			throw runtime::Ownership("sibling must be a direct child of the target parent");
		try {
			if (above)
				p.addChildAbove(&c, &s);
			else
				p.addChildBelow(&c, &s);
		}
		catch (...) {
			// Rack can publish parent before allocating its child-list entry.
			c.parent = nullptr;
			throw;
		}
		roots.erase(c.token->generation);
	}
	void remove(const Handle& parent, const Handle& child) {
		auto& p = parent.get();
		auto& c = child.get();
		if (c.parent != &p)
			throw runtime::Ownership("widget is not a direct child of this parent");
		// Allocate ownership bookkeeping before changing Rack's tree.
		roots.emplace(c.token->generation, &c);
		p.removeChild(&c);
	}
	void close(const Handle& h) {
		check();
		drain();
		if (!h.token->alive())
			return;
		auto& n = h.get();
		if (n.parent)
			throw runtime::Ownership("remove the child from its parent before closing it");
		roots.erase(n.token->generation);
		delete &n;
	}
};

inline Handle::~Handle() {
	// No native widget dereference or Python work here; context shutdown may
	// already have deleted it. Collecting an attached handle is harmless.
	manager->retire();
}
inline Node& Handle::get() const {
	manager->check();
	manager->drain();
	if (!token->alive() || !ptr)
		throw runtime::StaleHandle("this widget or its owning root has been deleted");
	return *ptr;
}

}  // namespace widget
}  // namespace rackpy
