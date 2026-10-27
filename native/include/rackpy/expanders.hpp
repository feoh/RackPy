// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy expander messages for native code (RP-23C): how kernels and graph
// nodes in a kernel library exchange messages with neighbouring RackPy
// modules, under schemas of their own.
//
// Rack's expanders carry a void* to a message the neighbour's module
// allocated; only that module knows its layout, and a module must check its
// neighbour before reinterpreting it. RackPy modules allocate their messages
// as RackPy ExpanderMessage buffers: a schema name and a count of float32
// values, assigned from Python (rackpy.engine.expanders) while Rack's engine
// does not run the module. This header lets native code use them safely:
//
//   - schema(name), on the main thread (a kernel's constructor), interns a
//     schema name once;
//   - send() gives the neighbour's producer buffer on one side and requests
//     the flip, and receive() this module's consumer buffer, each only if the
//     neighbour is a RackPy module and the buffer has the schema and room for
//     what the caller reads or writes. Both run on Rack's engine thread with
//     no lock, allocation or exception, and never read a pointer that is not
//     a RackPy buffer.
//
// A kernel library is a separate DSO with its own C++ runtime, so RTTI never
// crosses into it: RackPy does the checks in its own code and hands the
// library a Services table when it loads it, if the library defines
// RACKPY_EXPANDERS. A library that does not, or a RackPy without expander
// services, gets null from schema(), and send()/receive() exchange nothing.
//
// Nothing here stops native code from using Rack's own Expander API on
// other plugins' modules (check `module->model`, then reinterpret the
// pointer, as Rack's documentation describes); but a RackPy module's slots
// and buffers belong to RackPy: never assign them or free them.
//
// Include it after <rack.hpp>; it uses only Rack's public API.
#pragma once
#include <rack.hpp>

#include <cstddef>
#include <cstdint>

namespace rackpy {
namespace expander {

/** Bumped whenever Schema, Link, Services or RACKPY_EXPANDERS change incompatibly. */
constexpr int EXPANDER_ABI = 1;

/** An interned schema name: messages have the same schema exactly when their Schema pointers are equal. */
struct Schema;

/** What RackPy remembers about a neighbour between frames, so that it checks a neighbour's type only when
the neighbour changes. Keep one per send or receive site, in the kernel or node (two for a receive), and
leave its fields to RackPy. */
struct Link {
	const void* module = nullptr;
	int64_t id = -1;
	void* channels = nullptr;
};

/** RackPy's expander services, as a kernel library receives them. */
struct Services {
	/** Interns `name` (1 to 64 letters, digits, '_', '.' or '-'), or null for an invalid name. Main thread. */
	virtual const Schema* schema(const char* name) noexcept = 0;
	/** The neighbour's producer buffer on `side` of `self` (0 left, 1 right), for writing, with the flip
	requested: if the neighbour is a RackPy module whose facing expander points back to `self` and whose
	buffers there have `schema` and at least `size` values. Null otherwise. Engine thread. */
	virtual float* send(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t size, Link& link) noexcept = 0;
	/** This module's consumer buffer on `side`, for reading: if the neighbour there is a RackPy module and the
	buffer has `schema` and at least `size` values. Null otherwise. `frame` (ProcessArgs::frame) lets RackPy
	publish the message to Python as a snapshot once per frame, before anything marks it read. Engine thread. */
	virtual float* receive(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t size, int64_t frame,
		Link& neighbour, Link& own) noexcept = 0;

protected:
	~Services() = default;
};

/** The services RackPy gave this library when it loaded it, or null. */
inline Services*& services() {
	static Services* given = nullptr;
	return given;
}

inline const Schema* schema(const char* name) noexcept {
	Services* s = services();
	return s ? s->schema(name) : nullptr;
}

inline float* send(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t size, Link& link) noexcept {
	Services* s = services();
	return s && schema ? s->send(self, side, schema, size, link) : nullptr;
}

inline float* receive(rack::engine::Module& self, uint8_t side, const Schema* schema, size_t size, int64_t frame,
	Link& neighbour, Link& own) noexcept {
	Services* s = services();
	return s && schema ? s->receive(self, side, schema, size, frame, neighbour, own) : nullptr;
}

}  // namespace expander
}  // namespace rackpy

/** Defines a kernel library's expander entry points, once, next to its RACKPY_KERNEL_LIBRARY: RackPy then
gives the library its services before any of its kernels is made.

	RACKPY_EXPANDERS
*/
#define RACKPY_EXPANDERS \
	extern "C" __attribute__((visibility("default"))) int rackpy_expander_abi() { return rackpy::expander::EXPANDER_ABI; } \
	extern "C" __attribute__((visibility("default"))) void rackpy_set_expander_services(rackpy::expander::Services* given) { \
		rackpy::expander::services() = given; \
	}
