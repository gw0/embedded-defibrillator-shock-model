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
TEST_SOURCES = tests/test_signal_processing.c
TEST_TARGET = $(BIN_DIR)/test_signal_processing
INTEGRATION_TEST_SOURCES = tests/test_ekg_analyzer.c
INTEGRATION_TEST_TARGET = $(BIN_DIR)/test_ekg_analyzer
VF_TEST_SOURCES = tests/test_vf_detector.c
VF_TEST_TARGET = $(BIN_DIR)/test_vf_detector

.PHONY: test
test: $(TEST_TARGET) $(INTEGRATION_TEST_TARGET) $(VF_TEST_TARGET)
	@echo "Running signal processing tests..."
	@$(TEST_TARGET)
	@echo ""
	@echo "Running VF detector tests..."
	@$(VF_TEST_TARGET)
	@echo ""
	@echo "Running integration tests..."
	@$(INTEGRATION_TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES) $(HOST_OBJECTS) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(TEST_SOURCES) $(HOST_OBJECTS) -o $@ -lm

$(INTEGRATION_TEST_TARGET): $(INTEGRATION_TEST_SOURCES) $(HOST_OBJECTS) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(INTEGRATION_TEST_SOURCES) $(HOST_OBJECTS) -o $@ -lm

$(VF_TEST_TARGET): $(VF_TEST_SOURCES) $(HOST_OBJECTS) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(VF_TEST_SOURCES) $(HOST_OBJECTS) -o $@ -lm

# Tools
TOOLS_DIR = tools

# CSV Utilities (shared)
CSV_UTILS_SOURCE = $(TOOLS_DIR)/csv_utils.c
CSV_UTILS_OBJECT = $(HOST_OBJ_DIR)/csv_utils.o

$(CSV_UTILS_OBJECT): $(CSV_UTILS_SOURCE) | $(HOST_OBJ_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

# Evaluation Utilities (shared)
EVAL_UTILS_SOURCE = $(TOOLS_DIR)/eval_utils.c
EVAL_UTILS_OBJECT = $(HOST_OBJ_DIR)/eval_utils.o

$(EVAL_UTILS_OBJECT): $(EVAL_UTILS_SOURCE) | $(HOST_OBJ_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

# CSV Analyzer Tool
CSV_TOOL_SOURCE = $(TOOLS_DIR)/ekg_csv.c
CSV_TOOL_TARGET = $(BIN_DIR)/ekg_csv

.PHONY: ekg_csv
ekg_csv: $(CSV_TOOL_TARGET)

$(CSV_TOOL_TARGET): $(CSV_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(CSV_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) -o $@ -lm

# Training Tool
TRAIN_TOOL_SOURCE = $(TOOLS_DIR)/ekg_train.c
TRAIN_TOOL_TARGET = $(BIN_DIR)/ekg_train

# Check if OpenMP is available
OPENMP_FLAGS := $(shell echo | $(HOST_CC) -fopenmp -E - >/dev/null 2>&1 && echo "-fopenmp" || echo "")

.PHONY: ekg_train
ekg_train: $(TRAIN_TOOL_TARGET)

$(TRAIN_TOOL_TARGET): $(TRAIN_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) $(EVAL_UTILS_OBJECT) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(OPENMP_FLAGS) $(TRAIN_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) $(EVAL_UTILS_OBJECT) -o $@ -lm

# Evaluation Tool
EVAL_TOOL_SOURCE = $(TOOLS_DIR)/ekg_eval.c
EVAL_TOOL_TARGET = $(BIN_DIR)/ekg_eval

.PHONY: ekg_eval
ekg_eval: $(EVAL_TOOL_TARGET)

$(EVAL_TOOL_TARGET): $(EVAL_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) $(EVAL_UTILS_OBJECT) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $(EVAL_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) $(EVAL_UTILS_OBJECT) -o $@ -lm

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
	@echo "  ekg_csv       - Build CSV analyzer tool"
	@echo "  ekg_train     - Build training/optimization tool"
	@echo "  ekg_eval      - Build evaluation tool"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Output:"
	@echo "  Host:       $(HOST_TARGET) (static library)"
	@echo "  ARM:        $(ARM_TARGET) (static library)"
	@echo "  Test:       $(TEST_TARGET), $(INTEGRATION_TEST_TARGET)"
	@echo "  CSV Tool:   $(CSV_TOOL_TARGET)"
	@echo "  Train Tool: $(TRAIN_TOOL_TARGET)"
	@echo "  Eval Tool:  $(EVAL_TOOL_TARGET)"
