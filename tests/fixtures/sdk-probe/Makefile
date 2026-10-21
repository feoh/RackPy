# RP-02 probe: build with `bin/rack-sdk probe`, which copies this directory to a
# temporary location and passes RACK_DIR for the pinned, extracted SDK.
ifndef RACK_DIR
$(error RACK_DIR must point at an extracted Rack SDK)
endif

SOURCES += src/probe.cpp

include $(RACK_DIR)/plugin.mk
