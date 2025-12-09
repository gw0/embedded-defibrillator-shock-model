# Implementation Plan: ekg_eval Tool

## Overview

Create a standalone `ekg_eval` tool for cross-dataset validation. The tool evaluates the current default configuration (from `src/ekg_config.h`) against any labeled dataset, providing performance metrics using the same output style as `ekg_train`.

**User Requirements:**
- Primary use case: Cross-dataset validation (train on one dataset, evaluate on another)
- Configuration: Use default config from EKG_DEFAULT_CONFIG
- Tool design: Separate standalone tool
- Output: Same style as ekg_train (no CSV output needed)
- Code: Simple and short

## Architecture

### Code Reuse Strategy

Extract shared evaluation logic into a reusable module to avoid duplication between `ekg_train` and `ekg_eval`:

```
tools/
├── csv_utils.c/h          [existing - CSV loading]
├── eval_utils.c/h         [NEW - shared evaluation & reporting logic]
├── ekg_train.c            [REFACTOR - use eval_utils]
├── ekg_eval.c             [NEW - uses eval_utils]
```

### eval_utils Module Design

**tools/eval_utils.h** - Public API:
```c
// Data structures
typedef struct {
    char filepath[512];
    uint8_t ground_truth_shock;      // 0=NOSHOCK, 1=SHOCK
    uint8_t ground_truth_rhythm;     // 0=Unknown, 1=Normal, 2=VT, 3=VF, 4=Other
} dataset_file_t;

typedef struct {
    dataset_file_t *files;
    uint32_t count;
    uint32_t shock_count;
    uint32_t noshock_count;
} dataset_t;

typedef struct {
    uint32_t tp, tn, fp, fn;
    float accuracy;
    float sensitivity;
    float specificity;
    float precision;
    float f1_score;
} metrics_t;

// Dataset functions
int load_dataset(const char *base_path, dataset_t *dataset);
void free_dataset(dataset_t *dataset);

// Evaluation functions
void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
                    const uint32_t *file_indices, uint32_t num_files,
                    metrics_t *metrics, uint32_t source_sample_rate);
void calculate_metrics(metrics_t *metrics);

// Reporting functions (shared with ekg_train)
void print_metrics(const metrics_t *metrics);
void print_config_params(const ekg_config_t *config);
```

**tools/eval_utils.c** - Extract from ekg_train.c:
- Dataset loading: `parse_rhythm_type()`, `scan_directory()`, `load_dataset()`, `free_dataset()`
- Evaluation: `calculate_num_windows()`, `calculate_metrics()`, `evaluate_config()`
- **Reporting: `print_metrics()`, `print_config_params()` (so both tools use identical output)**

**Total extracted:** ~300 LOC

## CLI Interface

```bash
ekg_eval [OPTIONS] <dataset_path>

ARGUMENTS:
  <dataset_path>      Path to dataset directory (must contain SHOCK/ and NOSHOCK/)

OPTIONS:
  -r, --sample-rate <Hz>     Source sample rate (default: 100)
                             Supported: 100, 200, 500, 1000, 2000, 5000, 10000
  -h, --help                 Show help message

EXAMPLES:
  # Basic evaluation
  ekg_eval data/20251129-ECGrecording/PROCESSEDINTEGER/

  # Cross-validation on external dataset
  ekg_eval /path/to/test-dataset/

  # With sample rate conversion
  ekg_eval -r 500 data/high-rate-dataset/
```

## Output Format

Uses the same reporting functions as `ekg_train` (via shared `print_metrics()` and `print_config_params()` in eval_utils.c):

```
========================================
Evaluating Default Configuration
========================================
Dataset: data/20251129-ECGrecording/PROCESSEDINTEGER/
Files: 273 (67 SHOCK, 206 NOSHOCK)

Configuration:
  VT_HR_THRESHOLD: 140.0 bpm
  VT_QRS_WIDTH: 110.0 ms
  VT_REGULARITY_CV: 15.0%
  VT_CONFIDENCE: 0.80
  VF_FREQ_MIN: 5.0 Hz
  VF_FREQ_MAX: 9.0 Hz
  VF_ZCR_THRESHOLD: 0.10
  VF_CONFIDENCE: 0.90

Evaluating... 273/273 (100.0%)

Results:
  Accuracy: 51.3%, Sensitivity: 100.0%, Specificity: 31.6%, Precision: 32.2%, F1: 48.6%
  TP=67, TN=65, FP=141, FN=0
========================================
```

**Note:** Exact format matches `ekg_train` output to maintain consistency.

## Implementation Steps

### Phase 1: Extract eval_utils Module

1. **Create tools/eval_utils.h**
   - Define data structures (dataset_file_t, dataset_t, metrics_t)
   - Declare function prototypes for dataset, evaluation, and reporting

2. **Create tools/eval_utils.c**
   - Extract from ekg_train.c:
     - Dataset loading functions
     - Evaluation functions (`calculate_metrics()`, `evaluate_config()`)
     - **Reporting functions (`print_metrics()`, `print_config_params()`)**

3. **Update Makefile**
   - Add eval_utils.o build target
   - Update ekg_train dependencies to include eval_utils.o

### Phase 2: Refactor ekg_train.c

1. **Modify tools/ekg_train.c**
   - Add `#include "eval_utils.h"`
   - Remove extracted code
   - Use shared reporting functions from eval_utils

2. **Test refactored ekg_train**
   ```bash
   make ekg_train
   ./bin/ekg_train -s random -n 10 data/20251129-ECGrecording/PROCESSEDINTEGER/
   ```

### Phase 3: Implement ekg_eval.c

**Simple structure (~150 LOC):**

```c
#include "../src/ekg_analyzer.h"
#include "../src/ekg_config.h"
#include "csv_utils.h"
#include "eval_utils.h"

int main(int argc, char *argv[]) {
    // Parse CLI arguments (-r for sample rate, -h for help)
    // Load dataset using load_dataset()
    // Print header using print_config_params(&EKG_DEFAULT_CONFIG)
    // Evaluate using evaluate_config(&EKG_DEFAULT_CONFIG, ...)
    // Print results using print_metrics()
    // Cleanup and exit
}
```

**Keep it minimal:**
- Only `print_help()` helper needed
- Reuse all reporting from eval_utils
- Simple argument parsing (just -r and -h)

### Phase 4: Makefile Integration

Add eval_utils and ekg_eval targets:

```makefile
# Evaluation Utilities (shared)
EVAL_UTILS_SOURCE = $(TOOLS_DIR)/eval_utils.c
EVAL_UTILS_OBJECT = $(HOST_OBJ_DIR)/eval_utils.o

$(EVAL_UTILS_OBJECT): $(EVAL_UTILS_SOURCE) | $(HOST_OBJ_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

# Update ekg_train dependencies
$(TRAIN_TOOL_TARGET): ... $(EVAL_UTILS_OBJECT) ...

# Evaluation Tool
EVAL_TOOL_SOURCE = $(TOOLS_DIR)/ekg_eval.c
EVAL_TOOL_TARGET = $(BIN_DIR)/ekg_eval

.PHONY: ekg_eval
ekg_eval: $(EVAL_TOOL_TARGET)

$(EVAL_TOOL_TARGET): $(EVAL_TOOL_SOURCE) $(HOST_OBJECTS) $(CSV_UTILS_OBJECT) $(EVAL_UTILS_OBJECT) | $(BIN_DIR)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@ -lm
```

Update help target:
```makefile
  @echo "  ekg_eval      - Build evaluation tool"
```

### Phase 5: Testing

1. **Build and basic test**
   ```bash
   make ekg_eval
   ./bin/ekg_eval data/20251129-ECGrecording/PROCESSEDINTEGER/
   ```

2. **Sample rate conversion**
   ```bash
   ./bin/ekg_eval -r 500 data/high-rate-dataset/
   ```

3. **Verify ekg_train still works**
   ```bash
   make ekg_train
   ./bin/ekg_train -s random -n 10 data/.../PROCESSEDINTEGER/
   ```

## Key Technical Details

- **Window analysis:** 500 samples, 250 step, any-positive aggregation (matches ekg_train)
- **Configuration:** Always uses `EKG_DEFAULT_CONFIG` from src/ekg_config.h
- **Reporting:** Shared functions from eval_utils ensure identical output format to ekg_train
- **Error handling:** Fail on invalid args/dataset errors, warn on individual file errors

## Critical Files

**New files:**
1. tools/eval_utils.h - Shared data structures and functions
2. tools/eval_utils.c - Extract evaluation and reporting from ekg_train.c
3. tools/ekg_eval.c - Simple tool (~150 LOC)

**Modified files:**
4. tools/ekg_train.c - Use eval_utils for shared code
5. Makefile - Add eval_utils.o and ekg_eval targets

**Reference:**
- tools/ekg_train.c - Source for extraction
- src/ekg_config.h - EKG_DEFAULT_CONFIG

## Success Criteria

- ✓ Simple, short code (~150 LOC for ekg_eval.c)
- ✓ Shared reporting with ekg_train (identical output style)
- ✓ Builds with `make ekg_eval`
- ✓ No regression in ekg_train
- ✓ Evaluates default config on any SHOCK/NOSHOCK dataset
