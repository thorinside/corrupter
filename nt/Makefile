# Makefile for Corrupter distingNT plugin
#
# Dual build targets:
#   make hardware - Build ARM .o for disting NT hardware
#   make test     - Build native .dylib/.so for desktop testing in VCV Rack nt_emu
#   make clean    - Clean build artifacts

PROJECT = corrupter

# Paths
CORRUPTER_ROOT = ..
DISTINGNT_API ?= ../../nt_emu/external/distingNT_API

# Source files
NT_SOURCES = corrupter_nt.cpp

DSP_SOURCES = \
	$(CORRUPTER_ROOT)/src/engine.cpp \
	$(CORRUPTER_ROOT)/src/c_api.cpp \
	$(CORRUPTER_ROOT)/src/pitch_quantizer.cpp \
	$(CORRUPTER_ROOT)/src/internal/clock_engine.cpp \
	$(CORRUPTER_ROOT)/src/internal/corrupt_engine.cpp

ALL_SOURCES = $(NT_SOURCES) $(DSP_SOURCES)

# Include paths
INCLUDES = \
	-I$(DISTINGNT_API)/include \
	-I$(CORRUPTER_ROOT)/include \
	-I$(CORRUPTER_ROOT)/src

# Common flags
CXXFLAGS_COMMON = -std=c++17 -Wall -Wextra -Werror -fno-rtti -fno-exceptions

# Hardware target (ARM Cortex-M7)
CXX_ARM = arm-none-eabi-g++
CXXFLAGS_ARM = $(CXXFLAGS_COMMON) \
	-mcpu=cortex-m7 \
	-mfpu=fpv5-d16 \
	-mfloat-abi=hard \
	-mthumb \
	-Os \
	-fPIC \
	-fdata-sections \
	-ffunction-sections

# Desktop test target
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CXX_TEST = clang++
    DYLIB_EXT = dylib
else
    CXX_TEST = g++
    DYLIB_EXT = so
endif

CXXFLAGS_TEST = $(CXXFLAGS_COMMON) -O2 -fPIC

# Output directories
PLUGINS_DIR = plugins
BUILD_DIR = build

# Targets
.PHONY: all hardware test clean push

all: hardware test

# Hardware build - compile each source to .o, then link with -r
hardware: $(PLUGINS_DIR)/$(PROJECT).o

BUILD_OBJS = \
	$(BUILD_DIR)/corrupter_nt.o \
	$(BUILD_DIR)/engine.o \
	$(BUILD_DIR)/c_api.o \
	$(BUILD_DIR)/pitch_quantizer.o \
	$(BUILD_DIR)/clock_engine.o \
	$(BUILD_DIR)/corrupt_engine.o

$(BUILD_DIR)/corrupter_nt.o: corrupter_nt.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/engine.o: $(CORRUPTER_ROOT)/src/engine.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/c_api.o: $(CORRUPTER_ROOT)/src/c_api.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/pitch_quantizer.o: $(CORRUPTER_ROOT)/src/pitch_quantizer.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/clock_engine.o: $(CORRUPTER_ROOT)/src/internal/clock_engine.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/corrupt_engine.o: $(CORRUPTER_ROOT)/src/internal/corrupt_engine.cpp | $(BUILD_DIR)
	$(CXX_ARM) $(CXXFLAGS_ARM) $(INCLUDES) -c $< -o $@

$(PLUGINS_DIR)/$(PROJECT).o: $(BUILD_OBJS) | $(PLUGINS_DIR)
	$(CXX_ARM) -r $(BUILD_OBJS) -o $@
	@echo "Hardware build complete: $@"
	@ls -lh $@

# Test build - single-shot compile to .dylib/.so
test: $(PLUGINS_DIR)/$(PROJECT).$(DYLIB_EXT)

$(PLUGINS_DIR)/$(PROJECT).$(DYLIB_EXT): $(ALL_SOURCES) | $(PLUGINS_DIR)
ifeq ($(UNAME_S),Darwin)
	$(CXX_TEST) $(CXXFLAGS_TEST) $(INCLUDES) -dynamiclib -undefined dynamic_lookup $(ALL_SOURCES) -o $@
else
	$(CXX_TEST) $(CXXFLAGS_TEST) $(INCLUDES) -shared $(ALL_SOURCES) -o $@
endif
	@echo "Desktop test build complete: $@"
	@ls -lh $@

$(PLUGINS_DIR):
	mkdir -p $(PLUGINS_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

push: hardware
	ntpush $(PLUGINS_DIR)/$(PROJECT).o

clean:
	rm -rf $(PLUGINS_DIR) $(BUILD_DIR)
	@echo "Build artifacts cleaned"
