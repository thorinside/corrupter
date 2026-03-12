# Corrupter VCV Rack Plugin Makefile

RACK_DIR ?= ./Rack-SDK

# DSP library headers
FLAGS += -I../include -I../src

# DSP library sources (exclude test_wrap.cpp)
SOURCES += ../src/engine.cpp
SOURCES += ../src/c_api.cpp
SOURCES += ../src/pitch_quantizer.cpp
SOURCES += ../src/internal/clock_engine.cpp
SOURCES += ../src/internal/corrupt_engine.cpp

# Plugin sources
SOURCES += $(wildcard src/*.cpp)

# Resources
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
