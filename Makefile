TARGET := home-assistant-bridge_tests
BUILD_DIR := ./build

INC_DIRS := \
  components/geappliances_bridge \
  lib/tiny/include \
  lib/tiny/test/include \
  lib/tiny-gea-api/include \
  lib/tiny-gea-api/test/include \
  test/include \
  test/simulation \

SRC_DIRS := \
  lib/tiny/src \
  lib/tiny/test/src \
  lib/tiny-gea-api/src \
  lib/tiny-gea-api/test/src \
  test \
  test/src \
  test/tests \
  test/simulation \

SRC_FILES := \
  components/geappliances_bridge/mqtt_bridge.cpp \
  components/geappliances_bridge/mqtt_bridge_polling.cpp \
  components/geappliances_bridge/gea2_erd_client_adapter.cpp \
  components/geappliances_bridge/device_identity_manager.cpp \
  components/geappliances_bridge/feature_bit_manager.cpp \
  components/geappliances_bridge/autodiscovery_manager.cpp \
  components/geappliances_bridge/esphome_mqtt_client_adapter.cpp \
  components/geappliances_bridge/esphome_time_source.cpp \
  components/geappliances_bridge/esphome_uart_adapter.cpp \
  components/geappliances_bridge/ha_discovery_manager.cpp \
  components/geappliances_bridge/geappliances_bridge.cpp \
  components/geappliances_bridge/geappliances_bridge_bridge_init.cpp \
  components/geappliances_bridge/geappliances_bridge_feature_bits.cpp \
  components/geappliances_bridge/geappliances_bridge_ha_discovery.cpp \
  components/geappliances_bridge/geappliances_bridge_startup_hsm.cpp \
  components/geappliances_bridge/geappliances_bridge_autodiscovery.cpp

SRCS := $(SRC_FILES) $(shell find $(SRC_DIRS) -maxdepth 1 -name *.cpp -or -name *.c -or -name *.s)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

INC_DIRS += $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

SANITIZE_FLAGS := -fsanitize=address -fsanitize=undefined

# CppUTest installation paths — auto-detect common locations
# Override with: make CPPUTEST_PREFIX=/path/to/cpputest
#   macOS (Apple Silicon):  /opt/homebrew  (default)
#   macOS (Intel):          /usr/local
#   Linux (system pkg):     /usr
#   Linux (local build):    /path/to/CppUTest
UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)
ifeq ($(UNAME_S),Darwin)
  # Apple Silicon → homebrew at /opt/homebrew; Intel → /usr/local
  ifeq ($(UNAME_P),arm)
    CPPUTEST_PREFIX ?= /opt/homebrew
  else
    CPPUTEST_PREFIX ?= /usr/local
  endif
else
  # Linux: default to system-wide install
  CPPUTEST_PREFIX ?= /usr
endif
CPPUTEST_INC := -I$(CPPUTEST_PREFIX)/include
CPPUTEST_LIB := -L$(CPPUTEST_PREFIX)/lib

CFLAGS += -std=c11 -pedantic
CPPFLAGS += $(SANITIZE_FLAGS) -fno-omit-frame-pointer
CPPFLAGS += $(INC_FLAGS) $(CPPUTEST_INC) -MMD -MP -g -Wall -Wextra -Wcast-qual -Werror
CXXFLAGS += -std=c++17
LDFLAGS := $(SANITIZE_FLAGS) $(CPPUTEST_LIB)
LDLIBS := -lstdc++ -lCppUTest -lCppUTestExt -lm

BUILD_DEPS += $(MAKEFILE_LIST)

# Generate erd_lists.h from JSON before building
ERD_LISTS_HEADER := components/geappliances_bridge/erd_lists.h
ERD_DEFINITIONS_JSON := lib/public-appliance-api-documentation/appliance_api_erd_definitions.json

# Generate appliance_api_feature_lists.h from appliance_api.json before building
APPLIANCE_API_FEATURE_LISTS_HEADER := components/geappliances_bridge/appliance_api_feature_lists.h
APPLIANCE_API_JSON := lib/public-appliance-api-documentation/appliance_api.json

# Generate ha_discovery_config.h from appliance_api_erd_definitions.json before building
HA_DISCOVERY_CONFIG_HEADER := components/geappliances_bridge/ha_discovery_config.h

$(ERD_LISTS_HEADER) $(APPLIANCE_API_FEATURE_LISTS_HEADER) $(HA_DISCOVERY_CONFIG_HEADER): $(ERD_DEFINITIONS_JSON) $(APPLIANCE_API_JSON) scripts/generate_erd_lists.py
	@echo Generating ERD lists, feature API lists, and HA discovery config...
	@python3 scripts/generate_erd_lists.py

BUILD_DEPS += $(ERD_LISTS_HEADER) $(APPLIANCE_API_FEATURE_LISTS_HEADER) $(HA_DISCOVERY_CONFIG_HEADER)

.PHONY: test
test: $(BUILD_DIR)/$(TARGET)
	@echo Running tests...
	@$(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@echo Linking $@...
	@mkdir -p $(dir $@)
	@$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.s.o: %.s $(BUILD_DEPS)
	@echo Assembling $<...
	@mkdir -p $(dir $@)
	@$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c $(BUILD_DEPS)
	@echo Compiling $<...
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp $(BUILD_DEPS)
	@echo Compiling $<...
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo Cleaning...
	@rm -rf $(BUILD_DIR)

.PHONY: pytest
pytest:
	@echo Running Python tests...
	@python3 -m pytest scripts/test_generate_erd_lists.py -v

-include $(DEPS)
