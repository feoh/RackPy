// SPDX-License-Identifier: GPL-3.0-or-later
#include <rack.hpp>
#include <cassert>
#include <cstdlib>
#include <new>
#include <thread>

#include "../widget/tree.hpp"

// Fail the actual SDK child-list allocation, including its early parent
// assignment. Replacement allocators are confined to this test executable.
static std::atomic<bool> failAllocation{false};
void* operator new(std::size_t size) {
	if (failAllocation.exchange(false))
		throw std::bad_alloc();
	if (auto* ptr = std::malloc(size ? size : 1))
		return ptr;
	throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

int main() {
	using namespace rackpy;
	runtime::markMainThread();
	rack::Context context;
	context.event = new rack::widget::EventState;
	runtime::ContextScope scope(&context);
	auto token = std::make_shared<runtime::ContextToken>(&context);
	auto manager = std::make_shared<widget::Manager>(token);
	auto root = manager->create();
	auto a = manager->create();
	auto b = manager->create();
	auto grandchild = manager->create();
	manager->add(*root, *a);
	manager->add(*root, *b, true);
	manager->add(*a, *grandchild);
	assert(root->get().children.front() == b->ptr);
	assert(root->get().children.back() == a->ptr);
	assert(a->get().parent == root->ptr);
	for (auto pair : {std::make_pair(root, a), std::make_pair(grandchild, root), std::make_pair(root, root)}) {
		bool refused = false;
		try { manager->add(*pair.first, *pair.second); }
		catch (const runtime::Ownership&) { refused = true; }
		assert(refused);
	}
	manager->remove(*root, *a);
	assert(!a->get().parent && manager->roots.size() == 2);
	manager->add(*b, *a);
	root->get().clearChildren();
	assert(!a->token->alive() && !b->token->alive() && !grandchild->token->alive());
	assert(manager->roots.size() == 1);
	manager->close(*root);
	manager->close(*root);
	assert(manager->roots.empty());

	auto allocationParent = manager->create();
	auto allocationChild = manager->create();
	bool failed = false;
	failAllocation.store(true);
	try { manager->add(*allocationParent, *allocationChild); }
	catch (const std::bad_alloc&) { failed = true; }
	assert(failed && !failAllocation.load());
	assert(allocationChild->get().parent == nullptr);
	assert(allocationParent->get().children.empty() && manager->roots.size() == 2);
	manager->add(*allocationParent, *allocationChild);
	manager->close(*allocationParent);
	assert(!allocationChild->token->alive());

	// Native SDK layout and order reference, without a fake Python implementation.
	rack::widget::Widget native;
	auto* first = new rack::widget::Widget;
	auto* second = new rack::widget::Widget;
	first->setBox(rack::math::Rect(2, 3, 4, 5));
	second->setBox(rack::math::Rect(-1, -2, 8, 9));
	native.addChild(first);
	native.addChildBottom(second);
	auto r = manager->create();
	auto c = manager->create();
	auto d = manager->create();
	c->get().setBox(first->getBox());
	d->get().setBox(second->getBox());
	manager->add(*r, *c);
	manager->add(*r, *d, true);
	assert(r->get().getChildrenBoundingBox() == native.getChildrenBoundingBox());
	d->get().setVisible(false);
	second->setVisible(false);
	assert(r->get().getVisibleChildrenBoundingBox() == native.getVisibleChildrenBoundingBox());

	// A worker dropping the last owning handle only flags collection. Native
	// children remain alive until a main-thread drain, then become stale.
	std::thread worker([held = std::move(r)]() mutable { held.reset(); });
	worker.join();
	assert(c->token->alive());
	manager->drain();
	assert(!c->token->alive() && !d->token->alive());
	for (int i = 0; i < 1000; ++i) {
		auto owner = manager->create();
		auto child = manager->create();
		manager->add(*owner, *child);
		owner.reset();
		manager->drain();
		assert(!child->token->alive());
	}
	assert(manager->roots.empty());
	auto survivor = manager->create();
	auto child = manager->create();
	manager->add(*survivor, *child);
	manager->shutdown();
	assert(!survivor->token->alive() && !child->token->alive());
	assert(manager->roots.empty());
	// Final-release races cannot race native fields: Handle destruction only
	// flips an atomic flag, while shutdown owns all native deletion.
	for (int i = 0; i < 100; ++i) {
		auto own = manager->create();
		auto borrowed = manager->create();
		manager->add(*own, *borrowed);
		std::thread drop([held = std::move(own)]() mutable { held.reset(); });
		manager->shutdown();
		drop.join();
		assert(!borrowed->token->alive());
	}
	rack::Context foreign;
	{
		runtime::ContextScope other(&foreign);
		bool refused = false;
		try { manager->create(); }
		catch (const runtime::WrongContext&) { refused = true; }
		assert(refused);
	}
	return 0;
}
