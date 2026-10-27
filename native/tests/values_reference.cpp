// SPDX-License-Identifier: GPL-3.0-or-later
// Differential reference for the family 1 bindings (RP-12): evaluates Rack's
// own math, color and Quantity code over edge-case inputs, compiled with the
// SDK's flags, and prints every case as JSON. tests/values replays each case
// through rackpy.raw inside Rack and compares results bit for bit. It also
// covers random::get<T> for every T RackPy maps (RP-31K), from fixed seeds.
//
// Floats are written as their 32-bit patterns ("0x3f800000") and doubles as
// their 64-bit ones, so NaN payloads, signed zeros and denormals survive the trip.
#include <rack.hpp>

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace rack;
using math::Rect;
using math::Vec;

namespace {

std::string f(float x) {
	uint32_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	char buf[16];
	std::snprintf(buf, sizeof buf, "0x%08" PRIx32, bits);
	return std::string("{\"f\": \"") + buf + "\"}";
}

std::string d(double x) {
	uint64_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	char buf[24];
	std::snprintf(buf, sizeof buf, "0x%016" PRIx64, bits);
	return std::string("{\"d\": \"") + buf + "\"}";
}

std::string i(long long x) {
	return std::to_string(x);
}

std::string u(uint64_t x) {
	return std::to_string(x);
}

std::string b(bool x) {
	return x ? "true" : "false";
}

std::string s(const std::string& x) {
	std::string out = "\"";
	for (char c : x) {
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	return out + "\"";
}

std::string v(Vec x) {
	return "{\"vec\": [" + f(x.x) + ", " + f(x.y) + "]}";
}

std::string r(Rect x) {
	return "{\"rect\": [" + f(x.pos.x) + ", " + f(x.pos.y) + ", " + f(x.size.x) + ", " + f(x.size.y) + "]}";
}

std::string c(NVGcolor x) {
	return "{\"color\": [" + f(x.r) + ", " + f(x.g) + ", " + f(x.b) + ", " + f(x.a) + "]}";
}

std::string list(const std::vector<std::string>& items) {
	std::string out = "[";
	for (size_t k = 0; k < items.size(); k++)
		out += (k ? ", " : "") + items[k];
	return out + "]";
}

std::vector<std::string> cases;

void add(const char* fn, const std::vector<std::string>& args, const std::string& result) {
	cases.push_back("{\"fn\": \"" + std::string(fn) + "\", \"args\": " + list(args) + ", \"result\": " + result + "}");
}

const float INF = std::numeric_limits<float>::infinity();
const float NAN_ = std::numeric_limits<float>::quiet_NaN();
const std::vector<float> FLOATS = {0.f, -0.f, 1.f, -1.f, 0.5f, -0.5f, 2.5f, -2.5f, 7.25f, -7.25f, 1.f / 3.f, 1e-7f, -1e-7f,
	1e-45f, 3.4e38f, -3.4e38f, INF, -INF, NAN_};
const std::vector<int> INTS = {0, 1, -1, 2, -2, 3, 7, -7, 10, -10, 255, 1024, -1024, INT32_MAX, INT32_MIN};
const std::vector<long long> INT64S = {0, 1, -1, 2, 3, 4, 6, 8, 1023, 1024, -1024, INT64_MAX, INT64_MIN, 1LL << 62};
// Above INT64_MAX, where Python's int takes the uint64_t instantiation.
const std::vector<uint64_t> UINT64S = {1ull << 63, (1ull << 63) + 1, (1ull << 63) + 2, 3ull << 62, UINT64_MAX - 1,
	UINT64_MAX};
// 1e-50 and 1e-300 are normal doubles that a float cannot hold; 1e-310 is a denormal double.
const std::vector<double> DOUBLES = {0., -0., 1., -1., 2.5, -2.5, 1e-50, -1e-50, 1e-300, -1e-300, 1e-310, 1e300, -1e300,
	std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
	std::numeric_limits<double>::quiet_NaN()};
const std::vector<std::pair<float, float>> BOUNDS = {{0.f, 1.f}, {1.f, 0.f}, {-2.f, 2.f}, {NAN_, 1.f}, {0.f, INF}, {-INF, INF}};
const std::vector<std::pair<int, int>> IBOUNDS = {{0, 1}, {1, 0}, {-2, 2}, {-100, 100}, {INT32_MIN, INT32_MAX}};

std::vector<Vec> vecs() {
	std::vector<Vec> out;
	const std::vector<float> coords = {0.f, -0.f, 1.f, -1.5f, 3.f, 0.25f, 1e-7f, INF, -INF, NAN_};
	for (float x : coords)
		for (float y : {0.f, 2.f, -0.75f, INF, NAN_})
			out.push_back(Vec(x, y));
	return out;
}

std::vector<Rect> rects() {
	return {Rect(0, 0, 10, 10), Rect(-5, -5, 2, 3), Rect(1, 2, 0, 0), Rect(3, 3, -2, -2), Rect::inf(), Rect(0, 0, INF, 5),
		Rect(2.5f, -1.5f, 4.25f, 0.5f), Rect(NAN_, 0, 1, 1)};
}

void scalars() {
	for (float x : FLOATS) {
		for (auto ab : BOUNDS) {
			add("clamp_f", {f(x), f(ab.first), f(ab.second)}, f(math::clamp(x, ab.first, ab.second)));
			add("clamp_safe_f", {f(x), f(ab.first), f(ab.second)}, f(math::clampSafe(x, ab.first, ab.second)));
		}
		add("clamp_f_default", {f(x)}, f(math::clamp(x)));
		add("normalize_zero", {f(x)}, f(math::normalizeZero(x)));
		add("sgn_f", {f(x)}, f(math::sgn<float>(x)));
		add("chop", {f(x), f(1e-6f)}, f(math::chop(x)));
		add("chop", {f(x), f(1.f)}, f(math::chop(x, 1.f)));
		for (float m : {1.f, 2.5f, -1.f, 0.f, INF})
			add("euc_mod_f", {f(x), f(m)}, f(math::eucMod(x, m)));
		for (float y : {0.f, 1.f, -2.5f, INF, NAN_}) {
			add("is_near", {f(x), f(y), f(1e-6f)}, b(math::isNear(x, y)));
			add("crossfade_f", {f(x), f(y), f(0.25f)}, f(math::crossfade(x, y, 0.25f)));
			add("complex_mult", {f(x), f(y), f(0.5f), f(-2.f)}, [&] {
				float cr, ci;
				math::complexMult(x, y, 0.5f, -2.f, &cr, &ci);
				return list({f(cr), f(ci)});
			}());
		}
		add("rescale", {f(x), f(0.f), f(1.f), f(-5.f), f(5.f)}, f(math::rescale(x, 0.f, 1.f, -5.f, 5.f)));
		add("rescale", {f(x), f(2.f), f(2.f), f(0.f), f(1.f)}, f(math::rescale(x, 2.f, 2.f, 0.f, 1.f)));
	}
	const std::vector<float> table = {0.f, 1.f, 4.f, 9.f, -2.f};
	for (float x : {0.f, 0.5f, 1.25f, 2.f, 3.75f, 3.999f})
		add("interpolate_linear", {list({f(0.f), f(1.f), f(4.f), f(9.f), f(-2.f)}), f(x)}, f(math::interpolateLinear(table.data(), x)));

	for (int x : INTS) {
		for (auto ab : IBOUNDS) {
			add("clamp_i", {i(x), i(ab.first), i(ab.second)}, i(math::clamp(x, ab.first, ab.second)));
			add("clamp_safe_i", {i(x), i(ab.first), i(ab.second)}, i(math::clampSafe(x, ab.first, ab.second)));
		}
		for (int m : {1, 2, 3, 7, 1024}) {
			add("euc_mod_i", {i(x), i(m)}, i(math::eucMod(x, m)));
			add("euc_div", {i(x), i(m)}, i(math::eucDiv(x, m)));
			int div, mod;
			math::eucDivMod(x, m, &div, &mod);
			add("euc_div_mod", {i(x), i(m)}, list({i(div), i(mod)}));
		}
		if (x >= 0)
			add("log2", {i(x)}, i(math::log2(x)));
	}
	for (long long x : INT64S) {
		add("is_even", {i(x)}, b(math::isEven<int64_t>(x)));
		add("is_odd", {i(x)}, b(math::isOdd<int64_t>(x)));
		add("is_pow2", {i(x)}, b(math::isPow2<int64_t>(x)));
		add("sgn_i", {i(x)}, i(math::sgn<int64_t>(x)));
	}
	for (uint64_t x : UINT64S) {
		add("is_even", {u(x)}, b(math::isEven<uint64_t>(x)));
		add("is_odd", {u(x)}, b(math::isOdd<uint64_t>(x)));
		add("is_pow2", {u(x)}, b(math::isPow2<uint64_t>(x)));
	}
	for (double x : DOUBLES) {
		add("sgn_d", {d(x)}, d(math::sgn<double>(x)));
		add("sgn_f_of_d", {d(x)}, f(math::sgn<float>((float) x)));  // a Python float through the float overload
	}
}

/** Eight draws of get<T> from Rack's generator, seeded; T by the name rackpy.raw.random.get takes. */
std::vector<std::string> draws(const std::string& type, uint64_t s0, uint64_t s1) {
	random::local().seed(s0, s1);
	std::vector<std::string> out;
	for (int k = 0; k < 8; k++) {
		if (type == "uint64") out.push_back(u(random::get<uint64_t>()));
		else if (type == "uint32") out.push_back(i(random::get<uint32_t>()));
		else if (type == "uint16") out.push_back(i(random::get<uint16_t>()));
		else if (type == "uint8") out.push_back(i(random::get<uint8_t>()));
		else if (type == "int64") out.push_back(i(random::get<int64_t>()));
		else if (type == "int32") out.push_back(i(random::get<int32_t>()));
		else if (type == "int16") out.push_back(i(random::get<int16_t>()));
		else if (type == "int8") out.push_back(i(random::get<int8_t>()));
		else if (type == "bool") out.push_back(b(random::get<bool>()));
		else if (type == "float") out.push_back(f(random::get<float>()));
		else if (type == "double") out.push_back(d(random::get<double>()));
	}
	return out;
}

void randoms() {
	const std::vector<std::pair<uint64_t, uint64_t>> seeds = {{1, 2}, {0x9e3779b97f4a7c15ull, 42}, {UINT64_MAX, 7}};
	for (auto seed : seeds)
		for (const char* type : {"uint64", "uint32", "uint16", "uint8", "int64", "int32", "int16", "int8", "bool", "float",
				 "double"})
			add("random.get", {u(seed.first), u(seed.second), s(type)}, list(draws(type, seed.first, seed.second)));
}

void vectors() {
	std::vector<Vec> vs = vecs();
	std::vector<Rect> rs = rects();
	for (Vec a : vs) {
		add("vec.neg", {v(a)}, v(a.neg()));
		add("vec.arg", {v(a)}, f(a.arg()));
		add("vec.norm", {v(a)}, f(a.norm()));
		add("vec.normalize", {v(a)}, v(a.normalize()));
		add("vec.square", {v(a)}, f(a.square()));
		add("vec.area", {v(a)}, f(a.area()));
		add("vec.flip", {v(a)}, v(a.flip()));
		add("vec.abs", {v(a)}, v(a.abs()));
		add("vec.round", {v(a)}, v(a.round()));
		add("vec.floor", {v(a)}, v(a.floor()));
		add("vec.ceil", {v(a)}, v(a.ceil()));
		add("vec.is_zero", {v(a)}, b(a.isZero()));
		add("vec.is_finite", {v(a)}, b(a.isFinite()));
		for (float angle : {0.f, 1.5707964f, -0.5f, 3.f})
			add("vec.rotate", {v(a), f(angle)}, v(a.rotate(angle)));
		for (float sc : {0.f, 2.f, -0.5f, INF})
			add("vec.mult_s", {v(a), f(sc)}, v(a.mult(sc))), add("vec.div_s", {v(a), f(sc)}, v(a.div(sc))),
				add("vec.rmul", {f(sc), v(a)}, v(sc * a));
		for (Rect bound : rs) {
			add("vec.clamp", {v(a), r(bound)}, v(a.clamp(bound)));
			add("vec.clamp_safe", {v(a), r(bound)}, v(a.clampSafe(bound)));
		}
	}
	for (size_t k = 0; k < vs.size(); k += 3) {
		for (size_t l = 0; l < vs.size(); l += 4) {
			Vec a = vs[k], bb = vs[l];
			add("vec.plus", {v(a), v(bb)}, v(a.plus(bb)));
			add("vec.minus", {v(a), v(bb)}, v(a.minus(bb)));
			add("vec.mult_v", {v(a), v(bb)}, v(a.mult(bb)));
			add("vec.div_v", {v(a), v(bb)}, v(a.div(bb)));
			add("vec.dot", {v(a), v(bb)}, f(a.dot(bb)));
			add("vec.min", {v(a), v(bb)}, v(a.min(bb)));
			add("vec.max", {v(a), v(bb)}, v(a.max(bb)));
			add("vec.equals", {v(a), v(bb)}, b(a.equals(bb)));
			add("vec.crossfade", {v(a), v(bb), f(0.3f)}, v(a.crossfade(bb, 0.3f)));
		}
	}
	for (Rect a : rs) {
		add("rect.left", {r(a)}, f(a.getLeft()));
		add("rect.right", {r(a)}, f(a.getRight()));
		add("rect.top", {r(a)}, f(a.getTop()));
		add("rect.bottom", {r(a)}, f(a.getBottom()));
		add("rect.width", {r(a)}, f(a.getWidth()));
		add("rect.height", {r(a)}, f(a.getHeight()));
		add("rect.center", {r(a)}, v(a.getCenter()));
		add("rect.top_left", {r(a)}, v(a.getTopLeft()));
		add("rect.top_right", {r(a)}, v(a.getTopRight()));
		add("rect.bottom_left", {r(a)}, v(a.getBottomLeft()));
		add("rect.bottom_right", {r(a)}, v(a.getBottomRight()));
		add("rect.zero_pos", {r(a)}, r(a.zeroPos()));
		for (Vec d : {Vec(1, 2), Vec(-0.5f, 0), Vec(INF, 1)}) {
			add("rect.grow", {r(a), v(d)}, r(a.grow(d)));
			add("rect.shrink", {r(a), v(d)}, r(a.shrink(d)));
			add("rect.interpolate", {r(a), v(d)}, v(a.interpolate(d)));
		}
		for (Vec p : vs)
			add("rect.contains_v", {r(a), v(p)}, b(a.contains(p)));
		for (Rect o : rs) {
			add("rect.contains_r", {r(a), r(o)}, b(a.contains(o)));
			add("rect.intersects", {r(a), r(o)}, b(a.intersects(o)));
			add("rect.equals", {r(a), r(o)}, b(a.equals(o)));
			add("rect.clamp", {r(a), r(o)}, r(a.clamp(o)));
			add("rect.nudge", {r(a), r(o)}, r(a.nudge(o)));
			add("rect.expand", {r(a), r(o)}, r(a.expand(o)));
			add("rect.intersect", {r(a), r(o)}, r(a.intersect(o)));
		}
	}
	for (Vec a : {Vec(0, 0), Vec(1, 2), Vec(-3, 4.5f), Vec(INF, NAN_)})
		for (Vec bb : {Vec(5, -1), Vec(0.5f, 0.25f)})
			add("rect.from_min_max", {v(a), v(bb)}, r(Rect::fromMinMax(a, bb))),
				add("rect.from_corners", {v(a), v(bb)}, r(Rect::fromCorners(a, bb)));
	add("rect.inf", {}, r(Rect::inf()));
}

void colors() {
	const std::vector<NVGcolor> cs = {nvgRGBAf(0, 0, 0, 0), nvgRGBAf(1, 1, 1, 1), nvgRGBAf(0.25f, 0.5f, 0.75f, 0.5f),
		nvgRGBAf(2, -1, 0.5f, 1.5f), nvgRGBAf(0.1f, 0.2f, 0.3f, 1), color::RED, color::CYAN};
	for (NVGcolor a : cs) {
		add("color.clamp", {c(a)}, c(color::clamp(a)));
		add("color.to_hex_string", {c(a)}, s(color::toHexString(a)));
		for (float x : {0.f, 0.5f, 2.f, -1.f}) {
			add("color.mult_s", {c(a), f(x)}, c(color::mult(a, x)));
			add("color.alpha", {c(a), f(x)}, c(color::alpha(a, x)));
		}
		for (NVGcolor bb : cs) {
			add("color.is_equal", {c(a), c(bb)}, b(color::isEqual(a, bb)));
			add("color.minus", {c(a), c(bb)}, c(color::minus(a, bb)));
			add("color.plus", {c(a), c(bb)}, c(color::plus(a, bb)));
			add("color.mult_c", {c(a), c(bb)}, c(color::mult(a, bb)));
			add("color.lerp", {c(a), c(bb), f(0.25f)}, c(color::lerp(a, bb, 0.25f)));
			add("color.screen", {c(a), c(bb)}, c(color::screen(a, bb)));
		}
	}
	for (std::string hex : {"#000000", "#ffffff", "#FF8800", "#12345678", "#abc", "ff0000", "#zzzzzz", "", "#1234567890"})
		add("color.from_hex_string", {s(hex)}, c(color::fromHexString(hex)));
	const std::pair<const char*, NVGcolor> constants[] = {{"BLACK_TRANSPARENT", color::BLACK_TRANSPARENT},
		{"WHITE_TRANSPARENT", color::WHITE_TRANSPARENT}, {"BLACK", color::BLACK}, {"RED", color::RED}, {"GREEN", color::GREEN},
		{"BLUE", color::BLUE}, {"CYAN", color::CYAN}, {"MAGENTA", color::MAGENTA}, {"YELLOW", color::YELLOW},
		{"WHITE", color::WHITE}};
	for (auto& k : constants)
		add("color.constant", {s(k.first)}, c(k.second));
}

/** The same storage rules as RackPy's ValueQuantity, in plain C++: the reference for its behaviour. */
struct RefQuantity : Quantity {
	float value = 0.f, minValue = 0.f, maxValue = 1.f, defaultValue = 0.f;
	std::string label, unit;
	void setValue(float x) override { value = math::clampSafe(x, getMinValue(), getMaxValue()); }
	float getValue() override { return value; }
	float getMinValue() override { return minValue; }
	float getMaxValue() override { return maxValue; }
	float getDefaultValue() override { return defaultValue; }
	std::string getLabel() override { return label; }
	std::string getUnit() override { return unit; }
};

void quantities() {
	struct Config {
		float lo, hi, def;
		const char* label;
		const char* unit;
	};
	const Config configs[] = {{0, 1, 0.5f, "Gain", ""}, {-10, 10, 0, "Pitch", " V"}, {20, 20000, 440, "Frequency", " Hz"},
		{0, INF, 1, "Unbounded", ""}, {5, 5, 5, "Fixed", "%"}, {1, 0, 0.25f, "Inverted", ""}};
	for (const Config& k : configs) {
		std::vector<std::string> cfg = {f(k.lo), f(k.hi), f(k.def), s(k.label), s(k.unit)};
		for (float x : {0.f, 0.25f, -3.f, 7.5f, 1e6f, NAN_, INF}) {
			RefQuantity q;
			q.minValue = k.lo, q.maxValue = k.hi, q.defaultValue = k.def, q.label = k.label, q.unit = k.unit;
			q.setValue(x);
			std::vector<std::string> args = cfg;
			args.push_back(f(x));
			add("quantity.set_value", args, list({f(q.getValue()), s(q.getDisplayValueString()), s(q.getString()),
				b(q.isMin()), b(q.isMax()), f(q.getScaledValue()), b(q.isBounded()), f(q.getRange())}));
			q.setScaledValue(0.75f);
			add("quantity.set_scaled_value", args, f(q.getValue()));
			q.setValue(x);
			q.moveScaledValue(0.1f);
			add("quantity.move_scaled_value", args, f(q.getValue()));
			q.setValue(x);
			q.moveValue(1.5f);
			add("quantity.move_value", args, f(q.getValue()));
			q.setValue(x);
			q.toggle();
			add("quantity.toggle", args, f(q.getValue()));
			add("quantity.to_scaled", args, f(q.toScaled(x)));
			add("quantity.from_scaled", args, f(q.fromScaled(x)));
		}
		for (const char* text : {"0.5", "1+1", "c4", "a4", "inf", "-inf", "garbage", "", "1e3", "dbtogain(-6)"}) {
			RefQuantity q;
			q.minValue = k.lo, q.maxValue = k.hi, q.defaultValue = k.def;
			q.setValue(k.def);
			q.setDisplayValueString(text);
			std::vector<std::string> args = cfg;
			args.push_back(s(text));
			add("quantity.set_display_value_string", args, f(q.getValue()));
		}
		RefQuantity q;
		q.minValue = k.lo, q.maxValue = k.hi, q.defaultValue = k.def;
		q.setValue(k.hi);
		q.reset();
		add("quantity.reset", cfg, f(q.getValue()));
		q.setMin();
		add("quantity.set_min", cfg, f(q.getValue()));
		q.setMax();
		add("quantity.set_max", cfg, f(q.getValue()));
	}
}

}  // namespace

int main() {
	// Rack's standalone main() sets flush-to-zero and denormals-are-zero; compare in the same mode.
	system::resetFpuFlags();
	scalars();
	vectors();
	colors();
	quantities();
	randoms();
	std::printf("{\"rack_version\": %s, \"cases\": [\n", s(APP_VERSION).c_str());
	for (size_t k = 0; k < cases.size(); k++)
		std::printf("%s%s\n", cases[k].c_str(), k + 1 < cases.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
