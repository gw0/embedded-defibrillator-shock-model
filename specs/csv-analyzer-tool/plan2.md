# EKG CSV Analyzer Tool - Implementation Plan (Rolling Window Edition)

## Overview
Implement a command-line tool (`tools/ekg_csv.c`) to parse EKG signal CSV files and output human-readable analysis results using the existing `ekg_analyze()` API. This tool supports both single-window and rolling-window analysis modes using a unified code path.

## Key Requirements
- **Tool name**: `tools/ekg_csv.c` → compiled to `bin/ekg_csv`
- **Input**: CSV files with format `line_number→value` (UTF-8 arrow U+2192) or comma-separated
- **Output**: Condensed spec format with === headers
- **Modes**: Single-window (default) or rolling-window (with `-s` flag)
- **Verbose mode**: Signal statistics, detection details, window info
- **Sample constraints**: 300-800 samples per window (per ekg_analyze() API)
- **Exit codes**: 0=success, 1=file, 2=format, 3=data, 4=analysis, 5=memory

## Unified Window Approach (Key Design)

**Critical Design Decision**: ALL analysis uses the same code path. Single-window mode is just rolling-window with `num_windows = 1`.

- **No separate code paths**: One loop handles both single and multi-window
- **One print function**: Adapts output based on `num_windows`
- **Simpler code**: ~120 lines added vs. ~150 for separate implementations

## Command-Line Interface

```
ekg_csv [OPTIONS] <filename.csv>

Options:
  -v, --verbose       Enable verbose output
  -w, --window <N>    Analysis window size (300-800, default: 500)
  -o, --offset <N>    Start offset in samples (default: 0)
  -s, --step <N>      Step size for rolling window (enables rolling mode)
  -h, --help          Display help
  --version           Display version
```

**Options structure**:
```c
typedef struct {
    char filename[512];
    int verbose;
    int window_size;  // default: 500
    int offset;       // default: 0
    int step_size;    // default: 0 (single window), set value (rolling window)
} csv_options_t;
```

**Mode detection**:
- `step_size == 0`: Single-window mode (analyze 1 window at offset)
- `step_size > 0`: Rolling-window mode (default step = window/2 = 50% overlap)

## Data Structures

### Window Result Storage
```c
typedef struct {
    uint32_t offset;           // Starting sample index
    uint32_t window_size;      // Number of samples in this window
    ekg_analysis_result_t result;  // Analysis result from ekg_analyze()
} window_result_t;
```

**Display format**: Windows identified by sample ranges (e.g., "500-999" not "Window #3")
- More informative - shows exactly which samples were analyzed
- Includes time range in seconds for clarity

## Unified Algorithm

```c
// Calculate number of windows
int num_windows;
int step;

if (options.step_size == 0) {
    // Single window mode
    num_windows = 1;
    step = 0;  // Doesn't matter, loop runs once
} else {
    // Rolling window mode
    num_windows = calculate_num_windows(total_samples, options.window_size,
                                       options.offset, options.step_size);
    step = options.step_size;
}

// Allocate results array (works for 1 or many windows)
window_result_t *results = malloc(num_windows * sizeof(window_result_t));
if (results == NULL) {
    exit_code = EXIT_MEMORY_ERROR;
    goto cleanup;
}

// Analyze each window (loop runs once for single-window mode)
for (int i = 0; i < num_windows; i++) {
    uint32_t window_offset = options.offset + (i * step);

    // Validate window doesn't exceed file bounds
    if (window_offset + options.window_size > total_samples) {
        break;  // Stop if we've run out of samples
    }

    // Analyze window
    int ret = ekg_analyze(&samples[window_offset], options.window_size,
                         &results[i].result);

    if (ret != EKG_SUCCESS) {
        // Stop immediately on error (fail-fast)
        handle_analysis_error(ret);
        free(results);
        goto cleanup;
    }

    results[i].offset = window_offset;
    results[i].window_size = options.window_size;
}

// Print results - format adapts based on num_windows
print_results(results, num_windows, total_samples, &options);

free(results);
```

**Key simplification**: No `if (rolling_mode) {...} else {...}` branching!

## Output Formats

### Single Window Mode (num_windows == 1)
```
========================================
EKG Analysis Results
========================================
File: <filename>
Samples: <count> (<seconds> seconds @ 100 Hz)
Window: <analyzed> samples (offset: <N>)

SHOCK ADVISED: YES/NO
Confidence: <N>%
Rhythm type: <type_string>
Heart rate: <N> bpm [(not measurable in VF)]
QRS detected: <N>
Dominant freq: <N.N> Hz
========================================
```

### Rolling Window Mode (num_windows > 1)

**Phase 1: Summary Table**
```
========================================
EKG Rolling Window Analysis
========================================
File: <filename>
Total samples: <count> (<seconds> seconds @ 100 Hz)
Window size: <N> samples (<N.N> seconds)
Step size: <N> samples (overlap: <N> samples)
Windows analyzed: <N>

Window       | Shock | Conf | Rhythm    | HR
-------------|-------|------|-----------|----
0-499        | NO    | 95%  | Normal    |  72
250-749      | NO    | 93%  | Normal    |  74
500-999      | YES   | 89%  | VF        |   0
750-1249     | YES   | 92%  | VF        |   0
...
```

**Phase 2: Detailed Results for Shock-Advised Windows Only**
```
========================================
Detailed Analysis: Shock-Advised Windows
========================================

Window 500-999 (5.00-9.99 seconds)
----------------------------------------
SHOCK ADVISED: YES
Confidence: 89%
Rhythm type: Ventricular Fibrillation (VF)
Heart rate: 0 bpm (not measurable in VF)
QRS detected: 0
Dominant freq: 6.5 Hz

[If -v flag: Signal Statistics here]
----------------------------------------

Window 750-1249 (7.50-12.49 seconds)
----------------------------------------
SHOCK ADVISED: YES
...
========================================
```

**Phase 3: Final Summary**
```
========================================
OVERALL RECOMMENDATION: SHOCK ADVISED
Shock advised in 2 of 40 windows (5.0%)
Shock-advised ranges: 500-999, 750-1249
========================================
```

### Verbose Mode Enhancement
In both modes, `-v` flag adds signal statistics:
```
Signal Statistics:
  Min value: <N>
  Max value: <N>
  Mean value: <N.N>
  Peak-to-peak: <N>
  DC offset: <N.N>
```

## CSV Parsing (Existing - Keep Simple)

**Already implemented** in current `ekg_csv.c`:
- Single-pass parsing
- Fixed 100,000 sample buffer
- Handles arrow (→), comma, or plain integer formats
- Validates range [-32768, 32767]
- Error reporting with line numbers

**Do not modify** - parsing logic works and is simple.

## Functions to Implement/Modify

### New Functions

1. **`calculate_num_windows()`** (10 lines)
```c
int calculate_num_windows(uint32_t total_samples, int window_size,
                         int offset, int step_size)
{
    if (offset >= total_samples) return 0;
    uint32_t remaining = total_samples - offset;
    if (remaining < window_size) return 0;

    return ((remaining - window_size) / step_size) + 1;
}
```

2. **`rhythm_type_to_short_string()`** (5 lines)
```c
const char* rhythm_type_to_short_string(uint8_t type) {
    switch (type) {
        case 1: return "Normal";
        case 2: return "VT";
        case 3: return "VF";
        case 4: return "Other";
        default: return "Unknown";
    }
}
```

### Modified Functions

3. **`parse_arguments()`** - Add `-s, --step` option (10 lines added)
```c
// Add to getopt_long options array
{"step", required_argument, 0, 's'},

// Add to switch statement
case 's':
    options->step_size = atoi(optarg);
    if (options->step_size < 0) {
        fprintf(stderr, "Error: Step size must be non-negative\n");
        return -1;
    }
    break;
```

4. **`print_results()`** - Completely rewrite as unified adaptive function (80 lines total)

**Structure**:
```c
void print_results(const window_result_t *results, int num_windows,
                  uint32_t total_samples, const csv_options_t *options)
{
    if (num_windows == 1) {
        // Print single-window format
        print_single_window_result(&results[0], total_samples, options);
    } else {
        // Print rolling-window format (3 phases)
        print_rolling_summary_table(results, num_windows, total_samples, options);
        print_shock_advised_details(results, num_windows, options);
        print_overall_summary(results, num_windows);
    }
}
```

**Helper sections within print_results()**:
- `print_single_window_result()` - 20 lines
- `print_rolling_summary_table()` - 20 lines
- `print_shock_advised_details()` - 20 lines
- `print_overall_summary()` - 20 lines

5. **`main()`** - Refactor to use unified window loop (20 lines modified)

Replace existing single-window analysis code with the unified algorithm shown above.

## Implementation Steps

1. ✅ **Skeleton, help, version** - Already done
2. ✅ **CSV parsing** - Already done
3. ✅ **Basic argument parsing** - Already done
4. **Add `-s` step option** - Modify parse_arguments()
5. **Add window_result_t structure** - At top of file
6. **Add calculate_num_windows()** - Helper function
7. **Add rhythm_type_to_short_string()** - Helper function
8. **Refactor main()** - Replace single-window code with unified loop
9. **Rewrite print_results()** - Unified adaptive function
10. **Update print_help()** - Document `-s` flag with examples
11. **Test single-window** - Verify backward compatibility
12. **Test rolling-window** - Verify with real data files
13. **Memory testing** - valgrind leak check

## Testing

### Single Window Mode Tests
```bash
# Basic
./ekg_csv data/SHOCK/VFibCoarse.integer.csv

# Custom window
./ekg_csv -w 800 data/NOSHOCK/Normal100.integer.csv

# Offset
./ekg_csv -o 10000 -w 500 data/NOSHOCK/Normal100.integer.csv

# Verbose
./ekg_csv -v data/SHOCK/PolyVTachType1.integer.csv
```

### Rolling Window Mode Tests
```bash
# Basic rolling (50% overlap)
./ekg_csv -s 250 data/SHOCK/VFibCoarse.integer.csv

# Custom step
./ekg_csv -w 800 -s 200 data/NOSHOCK/Normal100.integer.csv

# No overlap
./ekg_csv -w 500 -s 500 data/SHOCK/VFibCoarse.integer.csv

# High overlap with verbose
./ekg_csv -w 400 -s 100 -v data/SHOCK/VFibFine.integer.csv
```

### Expected Behaviors
- Single-window: Same output as before (backward compatible)
- Rolling-window: Table + details for shock windows + summary
- Fail-fast: Analysis error in any window stops processing
- Memory: No leaks with ~200 windows

## Complexity Summary

### Lines of Code
- **Modified**: ~20 lines (main loop)
- **Added**: ~120 lines total
  - Data structures: 5 lines
  - calculate_num_windows(): 10 lines
  - rhythm_type_to_short_string(): 5 lines
  - Argument parsing (-s flag): 10 lines
  - print_results() rewrite: 80 lines
  - Help message update: 10 lines

### Total File Size
- Current: ~540 lines
- After rolling window: ~600-650 lines

### Benefits of Unified Approach
- ✅ Simpler: ONE code path for all modes
- ✅ Less code: ~30 lines saved vs. separate implementations
- ✅ Maintainable: No duplicated logic
- ✅ Testable: Easy to verify both modes
- ✅ Memory efficient: ~4KB for 200 windows

## Critical Implementation Notes

1. **Window range display**: Use `offset-(offset+window_size-1)` format
   - Example: "500-999" instead of "Window #3"
   - Include time: "500-999 (5.00-9.99 seconds)"

2. **Fail-fast error handling**: Stop processing on first analysis error
   - Don't continue analyzing remaining windows
   - Free allocated memory before exit

3. **Sample rate**: 100 Hz (10ms per sample)
   - Used for time calculations: `samples / 100.0` = seconds

4. **Shock decision logic**: For rolling windows
   - Overall: SHOCK if ANY window advises shock
   - List all shock-advised ranges in final summary

5. **Existing code to preserve**:
   - CSV parsing functions (load_csv_file, parse_line, etc.)
   - Error handling pattern (goto cleanup)
   - Rhythm type mapping (keep existing function)

## Files to Reference

1. **`src/ekg_analyzer.h`** - ekg_analyze() signature, ekg_analysis_result_t
2. **`src/ekg_config.h`** - EKG_MIN_SAMPLES (300), EKG_MAX_SAMPLES (800)
3. **`tests/test_ekg_analyzer.c`** - Output format style reference
4. **`tools/ekg_csv.c`** - Current implementation (base to modify)

## Success Criteria

- ✅ Compiles without warnings
- ✅ Single-window mode works identically to before
- ✅ Rolling-window mode produces correct 3-phase output
- ✅ Verbose mode shows signal statistics
- ✅ All exit codes correct
- ✅ No memory leaks (valgrind clean)
- ✅ Help message documents all options
- ✅ Tested with VF, VT, and Normal files
