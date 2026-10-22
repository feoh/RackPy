// SPDX-License-Identifier: GPL-3.0-or-later
// RP-07 laboratory benchmark: what per-frame Python costs, and what embedding
// costs at start-up. This is the RP-22 callback laboratory shape (a Python
// subclass of a native kernel, called once per frame through a pybind11
// trampoline). It is measured to show it is not a production path.
// Spike code, not RackPy.
//
//   bench_lab <python home>   prints JSON
#include <pybind11/embed.h>

#include "dsp.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

struct FrameKernel {
	virtual ~FrameKernel() = default;
	virtual float process(float pitch, float sampleTime) = 0;
};

struct PyFrameKernel : FrameKernel, py::trampoline_self_life_support {
	float process(float pitch, float sampleTime) override {
		PYBIND11_OVERRIDE_PURE(float, FrameKernel, process, pitch, sampleTime);
	}
};

/** The same oscillator in C++, reached through the same binding, for reference. */
struct NativeFrameKernel : FrameKernel {
	bench::Sine osc;
	float process(float pitch, float sampleTime) override { return osc.step(pitch, sampleTime); }
};

long rssKiB() {
	std::ifstream f("/proc/self/status");
	std::string line;
	while (std::getline(f, line))
		if (line.rfind("VmRSS:", 0) == 0)
			return std::stol(line.substr(6));
	return -1;
}

double nowNs() {
	return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double percentile(std::vector<double> v, double p) {
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t) (p * (v.size() - 1) + 0.5))];
}

}  // namespace

PYBIND11_EMBEDDED_MODULE(_lab, m) {
	py::classh<FrameKernel, PyFrameKernel>(m, "FrameKernel").def(py::init<>()).def("process", &FrameKernel::process);
	py::classh<NativeFrameKernel, FrameKernel>(m, "NativeFrameKernel").def(py::init<>());
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: bench_lab <python home>\n");
		return 2;
	}
	long rssBefore = rssKiB();
	double t0 = nowNs();
	PyConfig config;
	PyConfig_InitIsolatedConfig(&config);
	config.install_signal_handlers = 0;
	config.site_import = 0;
	config.write_bytecode = 0;
	PyConfig_SetBytesString(&config, &config.home, argv[1]);
	PyStatus status = Py_InitializeFromConfig(&config);
	PyConfig_Clear(&config);
	if (PyStatus_Exception(status)) {
		std::fprintf(stderr, "Python init failed\n");
		return 1;
	}
	double tInit = nowNs();
	long rssInit = rssKiB();

	std::vector<double> callNs, blockNs, nativeNs, contendedNs;
	{
		py::module_ lab = py::module_::import("_lab");
		py::exec(R"(
import math
import _lab

class PySine(_lab.FrameKernel):
    """The tutorial oscillator, written in Python."""
    def __init__(self):
        super().__init__()
        self.phase = 0.0
    def process(self, pitch, sample_time):
        self.phase += 261.6256 * 2.0 ** pitch * sample_time
        if self.phase >= 1.0:
            self.phase -= 1.0
        return 5.0 * math.sin(2.0 * math.pi * self.phase)
)");
		py::object py_kernel = py::eval("PySine()");
		py::object native_kernel = lab.attr("NativeFrameKernel")();
		double tImport = nowNs();
		long rssImport = rssKiB();
		auto* pk = py_kernel.cast<FrameKernel*>();
		auto* nk = native_kernel.cast<FrameKernel*>();
		const float dt = 1.f / 48000.f;
		const int BLOCK = 256, BLOCKS = 400;
		float sink = 0.f;

		// If audio_guard.so is preloaded, mark the first loop as an audio path: the
		// guard must count every PyGILState_Ensure there.
		using GuardFn = void (*)();
		using ReportFn = void (*)(char*, size_t);
		auto enter = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_enter");
		auto leave = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_leave");
		auto reset = (GuardFn) dlsym(RTLD_DEFAULT, "audio_guard_reset");
		auto report = (ReportFn) dlsym(RTLD_DEFAULT, "audio_guard_report");
		char guardJson[512] = "null";
		{
			// Release the GIL as an audio thread would hold none, then measure.
			py::gil_scoped_release released;
			if (reset)
				reset();
			if (enter)
				enter();
			for (int b = 0; b < BLOCKS; b++) {
				// GIL taken and released for every frame.
				double a = nowNs();
				for (int f = 0; f < BLOCK; f++) {
					PyGILState_STATE g = PyGILState_Ensure();
					sink += pk->process(0.1f, dt);
					PyGILState_Release(g);
				}
				callNs.push_back((nowNs() - a) / BLOCK);
			}
			if (leave)
				leave();
			if (report)
				report(guardJson, sizeof(guardJson));
			for (int b = 0; b < BLOCKS; b++) {
				// GIL held across a block of 256 frames; Python still runs once per frame.
				double a = nowNs();
				PyGILState_STATE g = PyGILState_Ensure();
				for (int f = 0; f < BLOCK; f++)
					sink += pk->process(0.1f, dt);
				PyGILState_Release(g);
				blockNs.push_back((nowNs() - a) / BLOCK);
			}
			{
				// Another Python thread is busy (as a UI script or worker might be).
				PyGILState_STATE g = PyGILState_Ensure();
				py::exec(R"(
import threading
_stop = threading.Event()
def _busy():
    x = 0
    while not _stop.is_set():
        x += 1
_t = threading.Thread(target=_busy)
_t.start()
)");
				PyGILState_Release(g);
				for (int b = 0; b < BLOCKS; b++) {
					double a = nowNs();
					for (int f = 0; f < BLOCK; f++) {
						PyGILState_STATE h = PyGILState_Ensure();
						sink += pk->process(0.1f, dt);
						PyGILState_Release(h);
					}
					contendedNs.push_back((nowNs() - a) / BLOCK);
				}
				g = PyGILState_Ensure();
				py::exec("_stop.set(); _t.join()");
				PyGILState_Release(g);
			}
			for (int b = 0; b < BLOCKS; b++) {
				// C++ kernel through the same binding: no Python, no GIL.
				double a = nowNs();
				for (int f = 0; f < BLOCK; f++)
					sink += nk->process(0.1f, dt);
				nativeNs.push_back((nowNs() - a) / BLOCK);
			}
		}
		std::printf("{\"python\": \"%s\", \"init_ms\": %.3f, \"import_and_define_ms\": %.3f, \"rss_before_kib\": %ld, "
			"\"rss_after_init_kib\": %ld, \"rss_after_import_kib\": %ld, \"frame_budget_ns\": {\"44100\": %.1f, \"48000\": %.1f, "
			"\"96000\": %.1f}, \"ns_per_frame\": {",
			Py_GetVersion(), (tInit - t0) / 1e6, (tImport - tInit) / 1e6, rssBefore, rssInit, rssImport, 1e9 / 44100,
			1e9 / 48000, 1e9 / 96000);
		auto stats = [](const char* name, const std::vector<double>& v, bool last) {
			std::printf("\"%s\": {\"p50\": %.1f, \"p99\": %.1f}%s", name, percentile(v, 0.5), percentile(v, 0.99),
				last ? "" : ", ");
		};
		stats("python_gil_per_frame", callNs, false);
		stats("python_gil_per_block", blockNs, false);
		stats("python_gil_per_frame_with_busy_python_thread", contendedNs, false);
		std::printf("\"python_contended_worst_block_ns_per_frame\": %.1f, ",
			*std::max_element(contendedNs.begin(), contendedNs.end()));
		stats("native_through_binding", nativeNs, true);
		std::printf("}, \"guard_on_python_frame_loop\": %s, \"sink\": %.3g}\n", guardJson, sink);
	}
	Py_FinalizeEx();
	return 0;
}
