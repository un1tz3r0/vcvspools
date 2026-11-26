ifneq ($(filter test bench,$(MAKECMDGOALS)),)
include tests.mk
else
RACK_DIR ?= ../Rack-SDK

FLAGS += -I. -Ivendor
SOURCES += $(wildcard src/*.cpp) $(wildcard core/*.cpp)
DISTRIBUTABLES += LICENSE $(wildcard res)

include $(RACK_DIR)/plugin.mk
CXXFLAGS += -std=c++17
endif
