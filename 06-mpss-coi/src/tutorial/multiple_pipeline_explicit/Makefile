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

all: $(DEBUG_DIR)/multiple_pipeline_explicit_source_host $(DEBUG_DIR)/multiple_pipeline_explicit_sink_mic \
     $(RELEASE_DIR)/multiple_pipeline_explicit_source_host $(RELEASE_DIR)/multiple_pipeline_explicit_sink_mic

$(DEBUG_DIR)/multiple_pipeline_explicit_source_host: multiple_pipeline_explicit_source.cpp
	mkdir -p $(DEBUG_DIR)
	$(CXX) $(HOST_FLAGS) $(DEBUG_FLAGS) -o $@ $<

$(RELEASE_DIR)/multiple_pipeline_explicit_source_host: multiple_pipeline_explicit_source.cpp
	mkdir -p $(RELEASE_DIR)
	$(CXX) $(HOST_FLAGS) $(RELEASE_FLAGS) -o $@ $<

$(DEBUG_DIR)/multiple_pipeline_explicit_sink_mic: multiple_pipeline_explicit_sink.cpp
	mkdir -p $(DEBUG_DIR)
	source $(MPSS_SETUP_SCRIPT) ; \
	$$CXX $(DEV_FLAGS) $(RELEASE_FLAGS) -o $@ $<

$(RELEASE_DIR)/multiple_pipeline_explicit_sink_mic: multiple_pipeline_explicit_sink.cpp
	mkdir -p $(RELEASE_DIR)
	source $(MPSS_SETUP_SCRIPT) ; \
	$$CXX $(DEV_FLAGS) $(RELEASE_FLAGS) -o $@ $<

clean:
	rm -rf debug
	rm -rf release
