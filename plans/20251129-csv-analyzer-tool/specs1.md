# CSV File Analyzer Tool - Requirements Specification

## 1. System Overview

### 1.1 Purpose
This document specifies the requirements for a command-line tool that processes individual EKG signal files in CSV format and outputs human-readable analysis results. This tool enables testing and validation of the EKG analyzer implementation using real recorded EKG signals from the dataset.

### 1.2 Scope
- **Target Platform**: Linux/Unix systems (development/testing environment)
- **Programming Language**: C (C99 standard)
- **Application Type**: Command-line utility
- **Processing Mode**: Single-file processing (one CSV file per invocation)
- **Output Format**: Human-readable text to standard output
- **Regulatory Status**: Educational/prototype (NOT a medical device)

### 1.3 Safety Notice
This implementation is intended for educational and prototype purposes only. It is NOT certified for medical use and does NOT comply with IEC 60601 or other medical device standards. Do not use this system for actual patient care.

---

## 2. Input Specifications

### 2.1 CSV File Format

The tool SHALL support CSV files with the following characteristics:

- **Format Type**: Single-column text file with line number mapping
- **Line Format**: `line_number→value` (Unicode arrow symbol U+2192 as separator)
- **Alternative Format**: `line_number,value` (comma-separated, for compatibility)
- **No Headers**: Files do not contain header rows
- **No Blank Lines**: Files should not contain blank lines (ignored if present)
- **Text Encoding**: ASCII or UTF-8

**Example (Integer Format)**:
```
1→1004
2→975
3→962
4→951
5→922
```

**Example (Float Format)**:
```
1→1900.5
2→1915.2
3→1925.8
4→1960.1
5→1963.7
```

### 2.2 Data Value Specifications

| Property | Requirement |
|----------|-------------|
| **Sample Rate** | 100 Hz (assumed, 10 ms between samples) |
| **Value Type** | Integer or floating-point numbers |
| **Value Range** | Must fit within int16_t range (-32768 to +32767) |
| **Typical Amplitude** | ±500 to ±2000 units (representing ~0.5-2.0 mV) |
| **Minimum Samples** | 200 samples (2 seconds minimum) |
| **Maximum Samples** | 100,000 samples (16.7 minutes maximum) |
| **Recommended Window** | 300-800 samples (3-8 seconds) |

### 2.3 File Validation Requirements

The tool SHALL validate the following before processing:

1. **File Accessibility**: File exists and is readable
2. **Format Validation**: Each line matches expected format
3. **Line Number Sequence**: Line numbers are sequential (1, 2, 3, ...)
4. **Value Range**: All values can be represented as int16_t
5. **Sample Count**: Total samples within acceptable range (200-100,000)
6. **Non-Empty**: File contains at least minimum required samples

---

## 3. CSV Parsing Requirements

### 3.1 Parsing Algorithm

The CSV parser SHALL implement the following logic:

**Step 1: File Reading**
- Open file in read mode
- Read file line-by-line
- Handle large files efficiently (streaming, not loading entire file to memory initially)

**Step 2: Line Parsing**
- Detect separator type (→ or ,)
- Split line into line_number and value
- Trim whitespace from both fields
- Validate line_number is sequential integer
- Parse value as floating-point number

**Step 3: Data Conversion**
- Convert parsed float value to int16_t
- Check for overflow/underflow
- Accumulate samples into dynamic array

**Step 4: Array Allocation**
- Initially count lines to determine size
- Allocate int16_t array of required size
- Fill array with parsed values

### 3.2 Error Detection

The parser SHALL detect and report the following errors:

| Error Type | Detection Criteria | Error Message |
|------------|-------------------|---------------|
| File Not Found | fopen() returns NULL | "Error: Cannot open file '<filename>'" |
| Invalid Format | Line doesn't match pattern | "Error: Invalid format at line <N>: '<line>'" |
| Missing Separator | No → or , found in line | "Error: Missing separator at line <N>" |
| Non-Numeric Value | Value is not a valid number | "Error: Invalid numeric value at line <N>: '<value>'" |
| Sequence Gap | Line numbers skip | "Warning: Non-sequential line numbers (expected <N>, got <M>)" |
| Value Overflow | Value > 32767 or < -32768 | "Error: Value out of range at line <N>: <value>" |
| Too Few Samples | Total samples < 200 | "Error: Insufficient samples (<N> samples, minimum 200 required)" |
| Too Many Samples | Total samples > 100,000 | "Error: Excessive samples (<N> samples, maximum 100,000)" |

### 3.3 Flexible Parsing

The tool SHALL support the following variations:

- **Mixed Separators**: Detect separator from first valid line
- **Float Values**: Accept floating-point values, convert to int16_t by rounding
- **Leading/Trailing Whitespace**: Trim whitespace before parsing
- **Comment Lines** (optional enhancement): Ignore lines starting with '#'
- **Missing Line Numbers** (optional): If no separator found, treat entire line as value

---

## 4. Processing Pipeline

### 4.1 Overall Workflow

```
┌─────────────────┐
│  Load CSV File  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Parse & Valid  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Convert to int16│
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Window Selection│ (if file > max window)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  ekg_analyze()  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Format Output   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Display Results │
└─────────────────┘
```

### 4.2 Window Selection

For files exceeding the maximum analysis window (800 samples):

**Option 1: First Window** (default)
- Use first 800 samples
- Ignore remaining samples
- Display warning: "File contains <N> samples, analyzing first 800 samples"

**Option 2: Custom Offset** (with -o flag)
- Start analysis at specified offset
- Take next 800 samples (or user-specified window size)
- Example: `-o 5000 -w 500` analyzes samples 5000-5499

**Option 3: Multiple Windows** (future enhancement)
- Analyze multiple overlapping windows
- Report results for each window
- Aggregate results

### 4.3 Preprocessing (Optional)

The tool MAY optionally perform preprocessing:

- **DC Offset Removal**: Calculate and subtract mean value
- **Signal Quality Assessment**: Check for saturation, flat line
- **Amplitude Scaling** (if needed): Ensure signal amplitude in expected range

These preprocessing steps are optional and controlled by command-line flags.

### 4.4 Analysis Invocation

```c
int16_t *samples = /* parsed array */;
uint16_t num_samples = /* actual count */;
ekg_analysis_result_t result;

int status = ekg_analyze(samples, num_samples, &result);

if (status != EKG_SUCCESS) {
    /* Handle error */
}
```

The tool SHALL check the return code and handle all possible error codes:
- `EKG_SUCCESS` (0): Continue to output
- `EKG_ERROR_NULL_PTR` (-1): Internal error (should not occur)
- `EKG_ERROR_INVALID_LEN` (-2): Sample count validation failed
- `EKG_ERROR_LOW_QUALITY` (-3): Signal quality insufficient
- `EKG_ERROR_SATURATED` (-4): Signal is saturated

---

## 5. Output Format Specification

### 5.1 Standard Output Format

**Human-Readable Console Output:**

```
========================================
EKG Analysis Results
========================================
File: data/20251129-ECGrecording/PROCESSEDINTEGER/SHOCK/VFibCoarse.integer.csv
Samples: 49997 (499.97 seconds @ 100 Hz)

SHOCK DECISION: SHOCK ADVISED

Rhythm Type: Ventricular Fibrillation (VF)
Confidence: 92%
Heart Rate: 0 bpm (not measurable in VF)
Dominant Frequency: 6.8 Hz
QRS Detected: 0

========================================
```

### 5.2 Field Mapping

| Result Field | Display Format | Notes |
|--------------|----------------|-------|
| `shock_advised` | "SHOCK ADVISED" or "NO SHOCK" | Bold/highlighted if terminal supports |
| `rhythm_type` | Descriptive string | Map numeric value to readable name |
| `confidence` | Percentage (0-100%) | Show as integer percentage |
| `heart_rate` | "X bpm" | Show "not measurable" if 0 in VF case |
| `dominant_freq` | "X.X Hz" | Show only if non-zero (relevant for VF) |
| `num_qrs_detected` | Integer count | Number of QRS complexes found |

**Rhythm Type Mapping:**
- 0 → "Unknown"
- 1 → "Normal Sinus Rhythm"
- 2 → "Ventricular Tachycardia (VT)"
- 3 → "Ventricular Fibrillation (VF)"
- 4 → "Other Arrhythmia"

### 5.3 Verbose Output Mode

When `-v` or `--verbose` flag is provided, display additional information:

```
========================================
EKG Analysis Results (VERBOSE)
========================================
File: VFibCoarse.integer.csv
Samples: 49997 (499.97 seconds @ 100 Hz)
Analysis Window: 500 samples (5.00 seconds)
Offset: 0 samples

Signal Statistics:
  Min Value: -2298
  Max Value: 1745
  Mean Value: -127
  Peak-to-Peak: 4043
  DC Offset: -127 (removed: no)

SHOCK DECISION: SHOCK ADVISED

Rhythm Type: Ventricular Fibrillation (VF)
Confidence: 92%
Heart Rate: 0 bpm (not measurable in VF)
Dominant Frequency: 6.8 Hz
QRS Detected: 0

Detection Details:
  VF Detector: POSITIVE (confidence: 92%)
    - Dominant frequency: 6.8 Hz (range: 4-10 Hz) ✓
    - Zero-crossing rate: 0.19 (threshold: 0.17) ✓
    - Amplitude: 4043 (minimum: 100) ✓
    - QRS absent: true ✓

  VT Detector: NEGATIVE
    - Heart rate: N/A (no QRS detected)

========================================
```

### 5.4 Error Output

For all errors, output to stderr with clear messages:

**Example 1: File Not Found**
```
Error: Cannot open file 'missing.csv'
Please check the file path and try again.
```

**Example 2: Invalid Format**
```
Error: Invalid CSV format at line 42: '42→abc'
Expected format: line_number→numeric_value

Please verify the file is in correct CSV format.
```

**Example 3: Analysis Error**
```
Error: Analysis failed (code: -3)
Signal quality insufficient for reliable analysis.

Possible causes:
  - Signal amplitude too low (possible asystole)
  - Signal saturated or clipped
  - Excessive noise or artifacts

Please verify signal quality and try again.
```

### 5.5 Exit Codes

| Exit Code | Meaning | Usage |
|-----------|---------|-------|
| 0 | Success | Analysis completed successfully |
| 1 | File error | File not found, not readable, or permission denied |
| 2 | Format error | Invalid CSV format or malformed data |
| 3 | Data error | Sample count, value range, or sequence error |
| 4 | Analysis error | ekg_analyze() returned error code |
| 5 | Memory error | malloc() failed or out of memory |

---

## 6. Command-Line Interface

### 6.1 Usage Syntax

```
ekg_test_csv [OPTIONS] <filename.csv>
```

### 6.2 Required Arguments

| Argument | Description |
|----------|-------------|
| `<filename.csv>` | Path to CSV file containing EKG samples |

### 6.3 Optional Flags

| Flag | Long Form | Argument | Default | Description |
|------|-----------|----------|---------|-------------|
| `-v` | `--verbose` | None | Off | Enable verbose output mode |
| `-w` | `--window` | `<samples>` | 500 | Analysis window size (300-800) |
| `-o` | `--offset` | `<samples>` | 0 | Start offset in samples |
| `-h` | `--help` | None | N/A | Display help message and exit |
| | `--version` | None | N/A | Display version information and exit |

### 6.4 Examples

**Example 1: Basic usage**
```bash
./ekg_test_csv data/SHOCK/VFibCoarse.integer.csv
```

**Example 2: Verbose mode**
```bash
./ekg_test_csv -v data/NOSHOCK/Normal100.integer.csv
```

**Example 3: Custom window size**
```bash
./ekg_test_csv -w 800 data/SHOCK/PolyVTachType1.integer.csv
```

**Example 4: Analyze specific segment**
```bash
./ekg_test_csv -o 10000 -w 500 data/long_recording.csv
```

**Example 5: Batch processing via shell script**
```bash
for file in data/SHOCK/*.csv; do
    echo "Processing: $file"
    ./ekg_test_csv "$file"
    echo ""
done
```

### 6.5 Help Message

When invoked with `-h` or `--help`, display:

```
EKG CSV File Analyzer Tool v1.0

USAGE:
    ekg_test_csv [OPTIONS] <filename.csv>

DESCRIPTION:
    Analyzes EKG signals from CSV files and provides shock recommendation
    using the EKG Signal Analyzer library.

ARGUMENTS:
    <filename.csv>      Path to CSV file containing EKG samples
                        Format: line_number→value (one sample per line)
                        Sample rate: 100 Hz

OPTIONS:
    -v, --verbose       Enable verbose output mode
    -w, --window <N>    Analysis window size in samples (300-800, default: 500)
    -o, --offset <N>    Start offset in samples (default: 0)
    -h, --help          Display this help message
    --version           Display version information

CSV FILE FORMAT:
    Single-column format with line numbers:
      1→1004
      2→975
      3→962
      ...

EXIT CODES:
    0    Success
    1    File error
    2    Format error
    3    Data error
    4    Analysis error
    5    Memory error

EXAMPLES:
    # Basic usage
    ./ekg_test_csv data/VFibCoarse.integer.csv

    # Verbose output
    ./ekg_test_csv -v data/Normal100.integer.csv

    # Custom window at specific offset
    ./ekg_test_csv -o 5000 -w 800 data/long_recording.csv

SAFETY NOTICE:
    This is an educational/prototype tool. NOT for medical use.

For more information, see docs/csv-analyzer-tool.md
```

---

## 7. Error Handling

### 7.1 File Access Errors

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| File not found | `fopen()` returns NULL, errno==ENOENT | Print error, exit code 1 |
| Permission denied | `fopen()` returns NULL, errno==EACCES | Print error, exit code 1 |
| Is a directory | `fopen()` returns NULL, errno==EISDIR | Print error, exit code 1 |
| File too large | File size > 10 MB | Print warning or error |

### 7.2 Format Errors

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| Invalid line format | Regex/parsing fails | Print line number, exit code 2 |
| Missing separator | No → or , in line | Print line number, exit code 2 |
| Non-numeric value | `strtod()` fails | Print line number, exit code 2 |
| Empty file | EOF on first read | Print error, exit code 3 |

### 7.3 Data Validation Errors

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| Too few samples | Sample count < 200 | Print error, exit code 3 |
| Too many samples | Sample count > 100,000 | Print error, exit code 3 |
| Value overflow | Value > 32767 | Print error, exit code 3 |
| Value underflow | Value < -32768 | Print error, exit code 3 |
| Non-sequential lines | Line N+1 != Line N + 1 | Print warning, continue |

### 7.4 Analysis Errors

Map `ekg_analyze()` error codes to user-friendly messages:

| Error Code | Internal Meaning | User Message |
|------------|-----------------|--------------|
| -1 | NULL pointer | "Internal error: Invalid pointer" |
| -2 | Invalid length | "Sample count out of valid range" |
| -3 | Low quality | "Signal quality insufficient for analysis" |
| -4 | Saturated | "Signal is saturated or clipped" |

---

## 8. Memory Management

### 8.1 Dynamic Allocation

The tool SHALL allocate memory dynamically based on file size:

```c
/* Determine sample count by reading file */
int sample_count = count_lines_in_file(filename);

/* Allocate array */
int16_t *samples = (int16_t *)malloc(sample_count * sizeof(int16_t));
if (samples == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return 5;  /* Memory error exit code */
}

/* Use samples... */

/* Always free before exit */
free(samples);
```

### 8.2 Memory Limits

| Limit | Value | Rationale |
|-------|-------|-----------|
| Max samples | 100,000 | Prevents excessive allocation (200 KB) |
| Max line length | 256 bytes | Sufficient for format: "NNNNNN→±NNNNN.NNN" |
| Buffer size | 4096 bytes | File read buffer size |

### 8.3 Cleanup Requirements

The tool SHALL ensure proper cleanup in all exit paths:

- **Normal Exit**: Free all allocated memory
- **Error Exit**: Free all allocated memory before exit
- **Signal Handler** (optional): Register SIGINT handler to cleanup on Ctrl+C

**Cleanup Pattern:**
```c
int main(int argc, char *argv[]) {
    int16_t *samples = NULL;
    int exit_code = 0;

    /* ... processing ... */

cleanup:
    if (samples != NULL) {
        free(samples);
    }
    return exit_code;
}
```

---

## 9. Build Integration

### 9.1 Makefile Target

Add the following target to the project Makefile:

```makefile
# CSV Test Tool
ekg_test_csv: tools/ekg_test_csv.c $(OBJ_FILES)
	$(CC) $(CFLAGS) -o $@ $< $(OBJ_FILES) $(LDFLAGS)

test_csv: ekg_test_csv
	./ekg_test_csv data/20251129-ECGrecording/PROCESSEDINTEGER/SHOCK/VFibCoarse.integer.csv
```

### 9.2 Dependencies

The tool depends on:
- **EKG Analyzer Library**: All compiled object files (*.o)
- **Standard Libraries**: stdio, stdlib, string, stdint, math
- **Math Library**: Link with `-lm`

**Object Files Required:**
- `ekg_analyzer.o`
- `qrs_detector.o`
- `vf_detector.o`
- `vt_detector.o`
- `signal_processing.o`
- `heart_rate.o`

### 9.3 Directory Structure

```
ekg-analyzer/
├── src/
│   ├── ekg_analyzer.c/.h
│   ├── qrs_detector.c/.h
│   ├── vf_detector.c/.h
│   ├── vt_detector.c/.h
│   ├── signal_processing.c/.h
│   ├── heart_rate.c/.h
│   └── ekg_config.h
├── tools/
│   ├── ekg_test_csv.c         # This tool
│   └── csv_parser.c/.h        # Shared CSV parsing (optional)
├── specs/
│   └── csv-analyzer-tool.md   # This document
└── Makefile
```

### 9.4 Compilation

```bash
# Compile all source files
make

# Compile CSV test tool
make ekg_test_csv

# Run example test
make test_csv
```

---

## 10. Example Usage and Output

### 10.1 Example 1: Normal Sinus Rhythm

**Input File:** `data/NOSHOCK/Normal100.integer.csv` (49,997 samples)

**Command:**
```bash
./ekg_test_csv data/NOSHOCK/Normal100.integer.csv
```

**Expected Output:**
```
========================================
EKG Analysis Results
========================================
File: data/NOSHOCK/Normal100.integer.csv
Samples: 49997 (499.97 seconds @ 100 Hz)

SHOCK DECISION: NO SHOCK

Rhythm Type: Normal Sinus Rhythm
Confidence: 95%
Heart Rate: 98 bpm
Dominant Frequency: 0.0 Hz
QRS Detected: 82

========================================
```

**Exit Code:** 0

### 10.2 Example 2: Ventricular Fibrillation

**Input File:** `data/SHOCK/VFibCoarse.integer.csv` (49,997 samples)

**Command:**
```bash
./ekg_test_csv data/SHOCK/VFibCoarse.integer.csv
```

**Expected Output:**
```
========================================
EKG Analysis Results
========================================
File: data/SHOCK/VFibCoarse.integer.csv
Samples: 49997 (499.97 seconds @ 100 Hz)

SHOCK DECISION: SHOCK ADVISED

Rhythm Type: Ventricular Fibrillation (VF)
Confidence: 92%
Heart Rate: 0 bpm (not measurable in VF)
Dominant Frequency: 6.8 Hz
QRS Detected: 0

========================================
```

**Exit Code:** 0

### 10.3 Example 3: Polymorphic Ventricular Tachycardia

**Input File:** `data/SHOCK/PolyVTachType1.integer.csv` (49,997 samples)

**Command:**
```bash
./ekg_test_csv -v data/SHOCK/PolyVTachType1.integer.csv
```

**Expected Output:**
```
========================================
EKG Analysis Results (VERBOSE)
========================================
File: data/SHOCK/PolyVTachType1.integer.csv
Samples: 49997 (499.97 seconds @ 100 Hz)
Analysis Window: 500 samples (5.00 seconds)
Offset: 0 samples

Signal Statistics:
  Min Value: -1520
  Max Value: 2100
  Mean Value: 340
  Peak-to-Peak: 3620
  DC Offset: 340 (removed: no)

SHOCK DECISION: SHOCK ADVISED

Rhythm Type: Ventricular Tachycardia (VT)
Confidence: 88%
Heart Rate: 185 bpm
Dominant Frequency: 3.1 Hz
QRS Detected: 15

Detection Details:
  VT Detector: POSITIVE (confidence: 88%)
    - Heart rate: 185 bpm (threshold: >120 bpm) ✓
    - QRS width: 14 samples = 140 ms (threshold: >120 ms) ✓
    - Rhythm regularity: CV = 18% (threshold: <20%) ✓
    - Consecutive beats: 15 (minimum: 3) ✓

  VF Detector: NEGATIVE
    - Dominant frequency: 3.1 Hz (out of range 4-10 Hz)

========================================
```

**Exit Code:** 0

### 10.4 Example 4: Invalid File

**Input File:** `data/corrupted.csv` (malformed)

**Command:**
```bash
./ekg_test_csv data/corrupted.csv
```

**Expected Output (to stderr):**
```
Error: Invalid CSV format at line 42: '42 invalid data'
Expected format: line_number→numeric_value

Please verify the file is in correct CSV format.
```

**Exit Code:** 2

---

## 11. Testing and Validation

### 11.1 Functional Tests

| Test Case | Input | Expected Output | Pass Criteria |
|-----------|-------|----------------|---------------|
| Normal Rhythm | Normal100.integer.csv | NO SHOCK, rhythm=Normal | Exit code 0, correct classification |
| VFib Coarse | VFibCoarse.integer.csv | SHOCK ADVISED, rhythm=VF | Exit code 0, shock=1 |
| VFib Fine | VFibFine.integer.csv | SHOCK ADVISED, rhythm=VF | Exit code 0, shock=1 |
| Poly VT | PolyVTachType1.integer.csv | SHOCK ADVISED, rhythm=VT | Exit code 0, shock=1 |
| Asystole | asystole.csv | NO SHOCK | Exit code 0, shock=0 |

### 11.2 Error Handling Tests

| Test Case | Input | Expected Behavior |
|-----------|-------|-------------------|
| File not found | nonexistent.csv | Error message, exit code 1 |
| Invalid format | malformed.csv | Error message with line number, exit code 2 |
| Too few samples | short.csv (100 samples) | Error message, exit code 3 |
| Value overflow | overflow.csv (value=50000) | Error message, exit code 3 |
| Empty file | empty.csv | Error message, exit code 3 |

### 11.3 Memory Tests

Use valgrind to verify no memory leaks:

```bash
valgrind --leak-check=full ./ekg_test_csv data/SHOCK/VFibCoarse.integer.csv
```

**Expected Result:**
```
All heap blocks were freed -- no leaks are possible
```

### 11.4 Performance Tests

| Test | File Size | Expected Time |
|------|-----------|---------------|
| Small file | 500 samples | <10 ms |
| Medium file | 5,000 samples | <50 ms |
| Large file | 50,000 samples | <200 ms |

---

## 12. Future Enhancements

### 12.1 Potential Features

1. **Batch Processing Mode**: Process multiple files from directory
2. **JSON Output**: Optional JSON format for programmatic processing
3. **Signal Visualization**: ASCII art plot of EKG signal
4. **Real-time Streaming**: Process data from stdin for real-time testing
5. **Confidence Threshold**: Override default confidence thresholds
6. **Multi-Window Analysis**: Analyze multiple segments of long recordings

### 12.2 Integration Options

- **Python Wrapper**: Python script to call tool and parse output
- **Web Interface**: CGI script to upload CSV and display results
- **Automated Testing**: Integration with test framework for regression testing

---

## 13. References

### 13.1 Related Documents

- `specs/initial-implementation/specs.md` - EKG Analyzer Requirements Specification
- `specs/initial-implementation/plan.md` - EKG Analyzer Implementation Plan
- `data/README.md` - Dataset documentation

### 13.2 Code References

- `src/ekg_analyzer.h` - Main analyzer API
- `src/ekg_config.h` - Configuration constants
- `tools/ekg_test_csv.c` - This tool implementation (to be created)

---

**Document Version**: 1.0
**Date**: 2025-11-29
**Status**: Draft for Implementation
