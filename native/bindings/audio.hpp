// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <rack.hpp>

namespace rackpy {
namespace bindings {

/** rackpy.raw.audio (RP-29B). */
void bindAudio(pybind11::module_ audio);
/** A live audio port Python made (rackpy.raw.audio.Port), for Rack's audio widgets and menus. Main thread. */
rack::audio::Port& pythonAudioPort(pybind11::handle value);

/** A device whose values Python configures (RP-29C): native code answers Rack's calls on any thread. Rack's
setSampleRate and setBlockSize apply the value and record a request for Python to take. */
struct ConfiguredDevice : rack::audio::Device {
	std::atomic<int> numInputs{0}, numOutputs{0}, blockSize{256};
	std::atomic<float> sampleRate{44100.f};
	std::mutex fieldsMutex;
	std::string name;
	std::set<float> sampleRates;
	std::set<int> blockSizes;
	std::vector<std::pair<std::string, double>> requests;
	bool removed = false;
	int driving = 0;  // Python calls running Rack's Device code (under the driver's devicesMutex)

	std::string getName() override {
		std::lock_guard<std::mutex> lock(fieldsMutex);
		return name;
	}
	int getNumInputs() override { return numInputs.load(); }
	int getNumOutputs() override { return numOutputs.load(); }
	std::set<float> getSampleRates() override {
		std::lock_guard<std::mutex> lock(fieldsMutex);
		return sampleRates;
	}
	float getSampleRate() override { return sampleRate.load(); }
	void setSampleRate(float rate) override {
		sampleRate.store(rate);
		std::lock_guard<std::mutex> lock(fieldsMutex);
		if (requests.size() < 1024)
			requests.emplace_back("sample_rate", rate);
	}
	std::set<int> getBlockSizes() override {
		std::lock_guard<std::mutex> lock(fieldsMutex);
		return blockSizes;
	}
	int getBlockSize() override { return blockSize.load(); }
	void setBlockSize(int size) override {
		blockSize.store(size);
		std::lock_guard<std::mutex> lock(fieldsMutex);
		if (requests.size() < 1024)
			requests.emplace_back("block_size", size);
	}
};

/** A configured device Rack never lists: the callback laboratory's (RP-22F). Only Python's ports reach it, through
an UnlistedAudioDriver, on the main thread. A Python port leaves it natively when the port is released (deleted, or
Rack's context closed). disconnect() and the destructor make every Python port leave it and point nowhere, natively. */
struct UnlistedAudioDevice : ConfiguredDevice {
	UnlistedAudioDevice();
	~UnlistedAudioDevice() override;
	void disconnect() noexcept;
};

/** An audio driver Rack never lists (the callback laboratory's, RP-22F): when it goes, every Python port using it
leaves its device and points nowhere, natively. Main thread. */
struct UnlistedAudioDriver : rack::audio::Driver {
	~UnlistedAudioDriver() override;
};

/** Whether `port` is a port Python made that is not being released: only such a port may be handed to Python. Main thread. */
bool livePythonAudioPort(const rack::audio::Port* port);
/** The Python object of a live Python-made port (see livePythonAudioPort). Needs the GIL. */
pybind11::object audioPortObject(rack::audio::Port* port);
/** Point a Python-made port at `driver`, as Rack's Port::setDriverId does once it has found a driver
(src/audio.cpp:93-123, getDefaultDeviceId at 116): leave the old device, take the driver, then its default
device. Rack's setDriverId finds only drivers Rack lists, so this repeats its calls. Main thread; the driver's
Python overrides run. */
void connectAudioDriver(pybind11::handle port, UnlistedAudioDriver& driver);
/** A device's subscribers, as PythonDevice.subscribed lists them; `owner` keeps the device alive. Needs the GIL. */
pybind11::list audioSubscribers(pybind11::object owner, rack::audio::Device& device);

}  // namespace bindings
}  // namespace rackpy
