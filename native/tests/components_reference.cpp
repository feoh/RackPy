// SPDX-License-Identifier: GPL-3.0-or-later
// Differential reference for the engine component bindings (RP-13): Rack's own
// Port, Param and Light code over channel counts 0, 1, 2 and 16, normalling,
// polyphonic sums and set_channels transitions, compiled with the SDK's flags
// and run in Rack's floating-point mode. tests/engine replays every case
// through rackpy.raw.engine inside Rack. Floats are 32-bit patterns.
#include <rack.hpp>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rack;
using engine::Light;
using engine::Port;
using engine::PORT_MAX_CHANNELS;

namespace {

std::string f(float x) {
	uint32_t bits;
	std::memcpy(&bits, &x, sizeof bits);
	char buf[16];
	std::snprintf(buf, sizeof buf, "0x%08" PRIx32, bits);
	return std::string("{\"f\": \"") + buf + "\"}";
}

std::string list(const std::vector<std::string>& items) {
	std::string out = "[";
	for (size_t k = 0; k < items.size(); k++)
		out += (k ? ", " : "") + items[k];
	return out + "]";
}

std::string floats(const float* v, int n) {
	std::vector<std::string> items;
	for (int k = 0; k < n; k++)
		items.push_back(f(v[k]));
	return list(items);
}

std::vector<std::string> cases;

void add(const char* fn, const std::vector<std::string>& args, const std::string& result) {
	cases.push_back("{\"fn\": \"" + std::string(fn) + "\", \"args\": " + list(args) + ", \"result\": " + result + "}");
}

/** A port as Rack leaves it after a cable with `channels` channels connects: raw channel count, then voltages. */
Port connected(int channels, const std::vector<float>& volts) {
	Port p;
	p.channels = (uint8_t) channels;
	for (int c = 0; c < PORT_MAX_CHANNELS; c++)
		p.voltages[c] = volts[c % volts.size()] * (c + 1);
	return p;
}

void ports() {
	const std::vector<std::vector<float>> patterns = {{0.f}, {1.f}, {-2.5f, 0.5f}, {10.f, -10.f, 3.f}, {1e-3f, 7.f}};
	for (int channels : {0, 1, 2, 5, 16}) {
		for (size_t pi = 0; pi < patterns.size(); pi++) {
			std::vector<std::string> args = {std::to_string(channels), std::to_string(pi)};
			Port p = connected(channels, patterns[pi]);
			std::vector<std::string> volts, poly, normal, normalPoly;
			for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
				volts.push_back(f(p.getVoltage(c)));
				poly.push_back(f(p.getPolyVoltage(c)));
				normal.push_back(f(p.getNormalVoltage(5.f, c)));
				normalPoly.push_back(f(p.getNormalPolyVoltage(5.f, c)));
			}
			add("port.get_voltage", args, list(volts));
			add("port.get_poly_voltage", args, list(poly));
			add("port.get_normal_voltage", args, list(normal));
			add("port.get_normal_poly_voltage", args, list(normalPoly));
			add("port.state", args, list({std::to_string(p.getChannels()), p.isConnected() ? "true" : "false",
				p.isMonophonic() ? "true" : "false", p.isPolyphonic() ? "true" : "false"}));
			add("port.get_voltage_sum", args, f(p.getVoltageSum()));
			add("port.get_voltage_rms", args, f(p.getVoltageRMS()));
			std::vector<float> read(PORT_MAX_CHANNELS, -99.f);
			p.readVoltages(read.data());
			add("port.read_voltages", args, floats(read.data(), p.getChannels()));
			for (int next : {0, 1, 3, 16}) {
				Port q = p;
				q.setChannels((uint8_t) next);
				std::vector<std::string> nargs = args;
				nargs.push_back(std::to_string(next));
				add("port.set_channels", nargs, list({std::to_string(q.getChannels()), floats(q.voltages, PORT_MAX_CHANNELS)}));
			}
			Port cleared = p;
			cleared.clearVoltages();
			add("port.clear_voltages", args, floats(cleared.voltages, PORT_MAX_CHANNELS));
			Port written = p;
			std::vector<float> incoming = {9.f, 8.f, 7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f, 0.f, -1.f, -2.f, -3.f, -4.f, -5.f, -6.f};
			written.writeVoltages(incoming.data());
			add("port.write_voltages", args, floats(written.voltages, PORT_MAX_CHANNELS));
		}
	}
}

void lights() {
	for (float start : {0.f, 0.25f, 1.f}) {
		for (float target : {0.f, 0.5f, 1.f}) {
			for (float dt : {1.f / 44100.f, 1.f / 60.f, 0.5f}) {
				for (float lambda : {30.f, 1.f, 100.f}) {
					Light l;
					l.setBrightness(start);
					l.setBrightnessSmooth(target, dt, lambda);
					add("light.set_brightness_smooth", {f(start), f(target), f(dt), f(lambda)}, f(l.getBrightness()));
				}
			}
			for (int frames : {1, 64, 44100}) {
				Light l;
				l.setBrightness(start);
				// Rack's deprecated per-frame overload, which RackPy still maps.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
				l.setBrightnessSmooth(target, frames);
#pragma GCC diagnostic pop
				add("light.set_brightness_smooth_frames", {f(start), f(target), std::to_string(frames)}, f(l.getBrightness()));
			}
		}
	}
}

}  // namespace

int main() {
	system::resetFpuFlags();  // Rack's floating-point mode
	ports();
	lights();
	std::printf("{\"port_max_channels\": %d, \"cases\": [\n", PORT_MAX_CHANNELS);
	for (size_t k = 0; k < cases.size(); k++)
		std::printf("%s%s\n", cases[k].c_str(), k + 1 < cases.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
