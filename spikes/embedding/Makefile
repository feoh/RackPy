# RP-04 embedding spike (Linux x64). Spike code, not the production build.
# Run through tests/integration/test_embedding_smoke.py, which supplies every path.
#
#   make RACK_DIR=<sdk> PYTHON_HOME=<pbs python dir> PYTHON_VERSION=3.14 \
#        PYBIND11_DIR=<pybind11 pkg> NANOBIND_DIR=<nanobind pkg> BUILD=<out dir>

ifndef RACK_DIR
$(error RACK_DIR must point at an extracted Rack SDK)
endif
ifndef PYTHON_HOME
$(error PYTHON_HOME must point at a python-build-standalone install)
endif
PYTHON_VERSION ?= 3.14
BUILD ?= build
CXX ?= g++

# Rack SDK compile.mk/plugin.mk flags for Linux x64.
SDK_FLAGS := -fPIC -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -fno-gnu-unique -g -O3 \
	-funsafe-math-optimizations -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -march=nehalem
SDK_LDFLAGS := -shared -L$(RACK_DIR) -lRack -Wl,-rpath=/tmp/Rack2 -static-libstdc++ -static-libgcc

HOST_FLAGS := $(SDK_FLAGS) -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden \
	-I$(PYTHON_HOME)/include/python$(PYTHON_VERSION) -Isrc
PY_LDFLAGS := -L$(PYTHON_HOME)/lib -lpython$(PYTHON_VERSION)
# Hosts sit next to plugin.so; the runtime is in python/ beside them.
HOST_RPATH := -Wl,-rpath,'$$ORIGIN/python/lib'

PYBIND11_INC := $(PYBIND11_DIR)/include
NANOBIND_INC := -I$(NANOBIND_DIR)/include -I$(NANOBIND_DIR)/ext/robin_map/include

HEADERS := src/host_api.h src/host_core.hpp

all: $(BUILD)/loader/plugin.so $(BUILD)/host_pybind11.so $(BUILD)/host_nanobind.so $(BUILD)/direct/plugin.so

$(BUILD)/loader/plugin.so: src/loader.cpp src/host_api.h
	@mkdir -p $(@D)
	$(CXX) -std=c++11 -Wsuggest-override $(SDK_FLAGS) -Isrc -o $@ $< $(SDK_LDFLAGS)

$(BUILD)/host_pybind11.so: src/host_core.cpp src/host_pybind11.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(HOST_FLAGS) -I$(PYBIND11_INC) -o $@ src/host_core.cpp src/host_pybind11.cpp \
		$(SDK_LDFLAGS) $(PY_LDFLAGS) $(HOST_RPATH)

$(BUILD)/nb_combined.o: $(NANOBIND_DIR)/src/nb_combined.cpp
	@mkdir -p $(@D)
	$(CXX) $(HOST_FLAGS) $(NANOBIND_INC) -Wno-missing-field-initializers -c -o $@ $<

$(BUILD)/host_nanobind.so: src/host_core.cpp src/host_nanobind.cpp $(BUILD)/nb_combined.o $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(HOST_FLAGS) $(NANOBIND_INC) -o $@ src/host_core.cpp src/host_nanobind.cpp $(BUILD)/nb_combined.o \
		$(SDK_LDFLAGS) $(PY_LDFLAGS) $(HOST_RPATH)

$(BUILD)/direct/plugin.so: src/direct.cpp src/host_core.cpp src/host_pybind11.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(HOST_FLAGS) -I$(PYBIND11_INC) -o $@ src/direct.cpp src/host_core.cpp src/host_pybind11.cpp \
		$(SDK_LDFLAGS) $(PY_LDFLAGS) $(HOST_RPATH)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
