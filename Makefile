# Makefile for EKG Signal Analyzer
#
# Supports both ARM Cortex-M33 cross-compilation and host GCC compilation
# for development and testing.

# Compiler selection
ARM_CC = arm-none-eabi-gcc
HOST_CC = gcc

# ARM Cortex-M33 compilation flags
ARM_CFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
ARM_CFLAGS += -O2 -std=c99 -Wall -Wextra -Werror
ARM_CFLAGS += -Isrc

# Host compilation flags
HOST_CFLAGS = -std=c99 -Wall -Wextra -Werror
HOST_CFLAGS += -Isrc -O2 -g

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
ARM_OBJ_DIR = $(OBJ_DIR)/arm
HOST_OBJ_DIR = $(OBJ_DIR)/host

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
ARM_OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(ARM_OBJ_DIR)/%.o)
HOST_OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(HOST_OBJ_DIR)/%.o)

# Targets
ARM_TARGET = $(BIN_DIR)/libekg_analyzer_arm.a
HOST_TARGET = $(BIN_DIR)/libekg_analyzer_host.a

# Default target: build for host
.PHONY: all
all: host

# Build for host (development/testing)
.PHONY: host
host: $(HOST_TARGET)

$(HOST_TARGET): $(HOST_OBJECTS) | $(BIN_DIR)
	ar rcs $@ $^

$(HOST_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(HOST_OBJ_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

# Build for ARM Cortex-M33
.PHONY: arm
arm: $(ARM_TARGET)

$(ARM_TARGET): $(ARM_OBJECTS) | $(BIN_DIR)
	arm-none-eabi-ar rcs $@ $^

$(ARM_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(ARM_OBJ_DIR)
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

# Create directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(ARM_OBJ_DIR):
	mkdir -p $(ARM_OBJ_DIR)

$(HOST_OBJ_DIR):
	mkdir -p $(HOST_OBJ_DIR)

# Clean
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Test programs
TEST_SOURCES = test_signal_processing.c
TEST_TARGET = $(BIN_DIR)/test_signal_processing
INTEGRATION_TEST_SOURCES = test_ekg_analyzer.c
INTEGRATION_TEST_TARGET = $(BIN_DIR)/test_ekg_analyzer

.PHONY: test
test: $(TEST_TARGET) $(INTEGRATION_TEST_TARGET)
	@echo "Running signal processing tests..."
	@$(TEST_TARGET)
	@echo ""
	@echo "Running integration tests..."
	@$(INTEGRATION_TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES) $(HOST_OBJECTS) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(TEST_SOURCES) $(HOST_OBJECTS) -o $@ -lm

$(INTEGRATION_TEST_TARGET): $(INTEGRATION_TEST_SOURCES) $(HOST_OBJECTS) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(INTEGRATION_TEST_SOURCES) $(HOST_OBJECTS) -o $@ -lm

# Show help
.PHONY: help
help:
	@echo "EKG Signal Analyzer - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all (default) - Build for host system (testing/development)"
	@echo "  host          - Build for host system"
	@echo "  arm           - Build for ARM Cortex-M33"
	@echo "  test          - Build and run verification tests"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Output:"
	@echo "  Host: $(HOST_TARGET) (static library)"
	@echo "  ARM:  $(ARM_TARGET) (static library)"
	@echo "  Test: $(TEST_TARGET)"
