// SPDX-License-Identifier: GPL-3.0-or-later
// Native tests for rackpy::rttiCast (native/include/rackpy/rtti.hpp), outside Rack, linked against the SDK's
// libRack. The ABI walk is checked on every platform (libstdc++ and libc++abi lay the records out the same
// way); the last test is the case RP-40 found on macOS: a widget whose type_info comes from libRack, cast to
// a Rack class without a key function, whose type_info this program has its own copy of.
#include <rack.hpp>

#include <cstdio>

#include "rackpy/rtti.hpp"

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

struct Root {
	virtual ~Root() = default;
	int root = 0;
};
struct Middle : Root {
	int middle = 0;
};
struct Leaf : Middle {
	int leaf = 0;
};
struct Other {
	virtual ~Other() = default;
	int other = 0;
};
struct Both : Middle, Other {
	int both = 0;
};
struct Left : Root {};
struct Right : Root {};
struct Diamond : Left, Right {};
struct Hidden : private Other, Root {};
struct ViaVirtual : virtual Other, Root {};

template<class T, class B>
std::ptrdiff_t offsetOf(T& object) {
	return reinterpret_cast<char*>(static_cast<B*>(&object)) - reinterpret_cast<char*>(&object);
}

void testTheAbiWalk() {
	using rackpy::rtti::baseOffset;
	CHECK(baseOffset(typeid(Leaf), typeid(Leaf)) == 0);
	CHECK(baseOffset(typeid(Leaf), typeid(Root)) == 0);
	CHECK(baseOffset(typeid(Leaf), typeid(Other)) == -1);
	Both both;
	CHECK(baseOffset(typeid(Both), typeid(Other)) == (offsetOf<Both, Other>(both)));
	CHECK(baseOffset(typeid(Both), typeid(Other)) > 0);
	CHECK(baseOffset(typeid(Both), typeid(Root)) == 0);
	CHECK(baseOffset(typeid(Diamond), typeid(Root)) == -1);  // ambiguous, as dynamic_cast says
	CHECK(baseOffset(typeid(Diamond), typeid(Right)) == (offsetOf<Diamond, Right>(*new Diamond)));
	CHECK(baseOffset(typeid(Hidden), typeid(Other)) == -1);  // private
	CHECK(baseOffset(typeid(ViaVirtual), typeid(Other)) == -1);  // virtual: offset known only at run time
	CHECK(baseOffset(typeid(ViaVirtual), typeid(Root)) >= 0);
}

void testTheCastAgreesWithDynamicCast() {
	Both both;
	Root* asRoot = &both;
	CHECK(rackpy::rttiCast<Other*>(asRoot) == dynamic_cast<Other*>(asRoot));
	CHECK(rackpy::rttiCast<Other*>(asRoot) == static_cast<Other*>(&both));
	CHECK(rackpy::rttiCast<Both*>(static_cast<Other*>(&both)) == &both);
	const Root* constRoot = &both;
	CHECK(rackpy::rttiCast<const Other*>(constRoot) == static_cast<const Other*>(&both));
	Leaf leaf;
	CHECK(rackpy::rttiCast<Other*>(static_cast<Root*>(&leaf)) == nullptr);
	CHECK(rackpy::rttiCast<Leaf*>(static_cast<Root*>(nullptr)) == nullptr);
	Diamond diamond;
	CHECK(rackpy::rttiCast<Right*>(static_cast<Left*>(&diamond)) == static_cast<Right*>(&diamond));
	CHECK(&rackpy::rttiCast<Other&>(static_cast<Root&>(both)) == static_cast<Other*>(&both));
	bool threw = false;
	try {
		rackpy::rttiCast<Other&>(static_cast<Root&>(leaf));
	}
	catch (const std::bad_cast&) {
		threw = true;
	}
	CHECK(threw);
}

void testAWidgetWhoseTypeInfoComesFromLibRack() {
	// ModuleLightWidget has a key function, so its type_info, and the base type_info it names, are libRack's;
	// MultiLightWidget has none, so typeid(MultiLightWidget) here is this program's own copy.
	auto* light = new rack::app::ModuleLightWidget;
	rack::widget::Widget* widget = light;
	bool plain = dynamic_cast<rack::app::MultiLightWidget*>(widget) != nullptr;
	std::printf("plain dynamic_cast to MultiLightWidget across libRack: %s\n", plain ? "works" : "fails");
	CHECK(rackpy::rttiCast<rack::app::MultiLightWidget*>(widget) == static_cast<rack::app::MultiLightWidget*>(light));
	CHECK(rackpy::rttiCast<rack::app::ModuleLightWidget*>(widget) == light);
	CHECK(rackpy::rttiCast<rack::app::SvgPort*>(widget) == nullptr);
	delete light;
}

}  // namespace

int main() {
	testTheAbiWalk();
	testTheCastAgreesWithDynamicCast();
	testAWidgetWhoseTypeInfoComesFromLibRack();
	if (failures) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("rtti: all checks passed\n");
	return 0;
}
