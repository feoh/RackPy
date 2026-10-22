# RP-06 lifecycle probe (Linux x64). Spike code, not the production build.
# Run through tests/integration/test_lifecycle_probe.py, which supplies every path.
#
#   make RACK_DIR=<sdk> PYTHON_HOME=<pbs python dir> PYBIND11_DIR=<pybind11 pkg> BUILD=<out dir>

ifndef RACK_DIR
$(error RACK_DIR must point at an extracted Rack SDK)
endif
ifndef PYTHON_HOME
$(error PYTHON_HOME must point at a python-build-standalone install)
endif
PYTHON_VERSION ?= 3.14
BUILD ?= build
CXX ?= g++
EMBEDDING := ../embedding/src

SDK_FLAGS := -fPIC -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include -fno-gnu-unique -g -O2 \
	-funsafe-math-optimizations -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -march=nehalem
SDK_LDFLAGS := -shared -L$(RACK_DIR) -lRack -Wl,-rpath=/tmp/Rack2 -static-libstdc++ -static-libgcc
HOST_FLAGS := $(SDK_FLAGS) -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden \
	-I$(PYTHON_HOME)/include/python$(PYTHON_VERSION) -I$(EMBEDDING) -I$(PYBIND11_DIR)/include

all: $(BUILD)/loader/plugin.so $(BUILD)/host_lifecycle.so

# The loader is the RP-04 loader, unchanged.
$(BUILD)/loader/plugin.so: $(EMBEDDING)/loader.cpp $(EMBEDDING)/host_api.h
	@mkdir -p $(@D)
	$(CXX) -std=c++11 -Wsuggest-override $(SDK_FLAGS) -I$(EMBEDDING) -o $@ $< $(SDK_LDFLAGS)

$(BUILD)/host_lifecycle.so: src/lifecycle.cpp $(EMBEDDING)/host_api.h
	@mkdir -p $(@D)
	$(CXX) $(HOST_FLAGS) -o $@ $< $(SDK_LDFLAGS) -L$(PYTHON_HOME)/lib -lpython$(PYTHON_VERSION) \
		-Wl,-rpath,'$$ORIGIN/python/lib'

clean:
	rm -rf $(BUILD)

.PHONY: all clean
