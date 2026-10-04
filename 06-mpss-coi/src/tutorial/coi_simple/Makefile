# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.

MPSS_SETUP_SCRIPT = /opt/mpss/?.*/environment-setup-*-linux
DEV_FLAGS ?= -lcoi_device -rdynamic -Wl,--enable-new-dtags
HOST_FLAGS ?= -lcoi_host -Wl,--enable-new-dtags
DEBUG_FLAGS = -g -O0 -D_DEBUG
RELEASE_FLAGS = -DNDEBUG -O3
DEBUG_DIR = debug
RELEASE_DIR = release

all: $(DEBUG_DIR)/coi_simple_source_host $(DEBUG_DIR)/coi_simple_sink_mic \
     $(RELEASE_DIR)/coi_simple_source_host $(RELEASE_DIR)/coi_simple_sink_mic

$(DEBUG_DIR)/coi_simple_source_host: coi_simple_source.cpp
	mkdir -p $(DEBUG_DIR)
	$(CXX) $(HOST_FLAGS) $(DEBUG_FLAGS) -o $@ $<

$(RELEASE_DIR)/coi_simple_source_host: coi_simple_source.cpp
	mkdir -p $(RELEASE_DIR)
	$(CXX) $(HOST_FLAGS) $(RELEASE_FLAGS) -o $@ $<

$(DEBUG_DIR)/coi_simple_sink_mic: coi_simple_sink.cpp
	mkdir -p $(DEBUG_DIR)
	source $(MPSS_SETUP_SCRIPT) ; \
	$$CXX $(DEV_FLAGS) $(RELEASE_FLAGS) -o $@ $<

$(RELEASE_DIR)/coi_simple_sink_mic: coi_simple_sink.cpp
	mkdir -p $(RELEASE_DIR)
	source $(MPSS_SETUP_SCRIPT) ; \
	$$CXX $(DEV_FLAGS) $(RELEASE_FLAGS) -o $@ $<

clean:
	rm -rf debug
	rm -rf release
