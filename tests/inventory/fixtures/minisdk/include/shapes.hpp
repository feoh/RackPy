// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mini {
namespace shapes {

struct Hidden;

/** A record with overloads, a pimpl and an anonymous union. */
struct Box {
	struct Internal;
	Internal* internal;
	float width = 0.f;
	union {
		/** Unstable API. Use scale(). */
		float data[2];
		/** DEPRECATED. Use data. */
		float legacy;
	};

	Box();
	explicit Box(float w);
	void scale(float s);
	void scale(float sx, float sy);
	virtual float area() const;
	PRIVATE void reset();
	DEPRECATED void resize(float w);
	static int count();
	Hidden* hidden = nullptr;
};

template <typename T = float, int N = 4>
struct Buffer {
	T values[N];
	T get(int i) const;
};

template <>
struct Buffer<int, 2> {
	int values[2];
};

template <typename T>
T convert(T x);

template <>
inline float convert(float x) {
	return x;
}

enum Color {
	RED,
	GREEN = 5,
};

typedef Box BoxAlias;

#define MINI_LIMIT 16
#define MINI_TWICE(x) ((x) * 2)

}  // namespace shapes
}  // namespace mini
