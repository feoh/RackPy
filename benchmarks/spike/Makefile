# RP-07 benchmark spike (Linux x64). Spike code, not the production build.
# bin/perf-baseline supplies every path:
#
#   make RACK_DIR=<sdk> PYTHON_HOME=<pbs python dir> PYBIND11_DIR=<pybind11 pkg> BUILD=<out dir>

ifndef RACK_DIR
$(error RACK_DIR must point at an extracted Rack SDK)
endif
PYTHON_VERSION ?= 3.14
BUILD ?= build
CXX ?= g++
CC ?= gcc

# Exactly the SDK's Linux x64 optimization flags, for every variant.
SDK_FLAGS := -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -g -O3 -funsafe-math-optimizations \
	-fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -march=nehalem -std=c++17
LINK_RACK := -L$(RACK_DIR) -lRack -Wl,-rpath,$(RACK_DIR) -static-libstdc++ -static-libgcc -ldl

all: $(BUILD)/bench $(BUILD)/audio_guard.so $(BUILD)/bench_lab $(BUILD)/host/plugin.so

$(BUILD)/bench: src/bench.cpp src/dsp.hpp
	@mkdir -p $(@D)
	$(CXX) $(SDK_FLAGS) -o $@ src/bench.cpp $(LINK_RACK)

$(BUILD)/audio_guard.so: ../../native/testing/audio_guard.c
	@mkdir -p $(@D)
	$(CC) -O2 -fPIC -shared -Wall -o $@ $< -ldl

$(BUILD)/bench_lab: src/bench_lab.cpp src/dsp.hpp
	@mkdir -p $(@D)
	$(CXX) $(SDK_FLAGS) -I$(PYTHON_HOME)/include/python$(PYTHON_VERSION) -I$(PYBIND11_DIR)/include \
		-o $@ src/bench_lab.cpp $(LINK_RACK) -L$(PYTHON_HOME)/lib -lpython$(PYTHON_VERSION) \
		-Wl,-rpath,$(PYTHON_HOME)/lib

# Real-host plugin: the SDK's plugin.mk flags for Linux x64.
$(BUILD)/host/plugin.so: src/host_bench.cpp src/dsp.hpp
	@mkdir -p $(@D)
	$(CXX) $(SDK_FLAGS) -fPIC -fno-gnu-unique -Wsuggest-override -shared -o $@ src/host_bench.cpp \
		-L$(RACK_DIR) -lRack -Wl,-rpath=/tmp/Rack2 -static-libstdc++ -static-libgcc

clean:
	rm -rf $(BUILD)

.PHONY: all clean
