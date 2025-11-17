# EKG Signal Analyzer - Requirements Specification

## 1. System Overview

### 1.1 Purpose
This document specifies the requirements for an educational/prototype EKG (electrocardiogram) signal analyzer designed to provide shock recommendations for a defibrillator system. The analyzer will process EKG samples in real-time on a Cortex M33 microcontroller and determine whether a defibrillator shock should be administered.

### 1.2 Scope
- **Target Platform**: ARM Cortex M33 processor with FPU
- **Programming Language**: C (C99 standard)
- **Application Type**: Embedded real-time signal processing
- **Regulatory Status**: Educational/prototype (NOT a medical device)

### 1.3 Safety Notice
This implementation is intended for educational and prototype purposes only. It is NOT certified for medical use and does NOT comply with IEC 60601 or other medical device standards. Do not use this system for actual patient care.

---

## 2. Input Specifications

### 2.1 Input Signal
- **Data Type**: Array of `int16_t` (signed 16-bit integers)
- **Sample Rate**: 100 Hz (100 samples per second)
- **Maximum Duration**: 10 seconds (up to 1000 samples)
- **Minimum Duration**: 2 seconds (200 samples minimum for analysis)
- **Signal Conditioning**: Pre-filtered (baseline wander removed, noise filtered)
- **Amplitude Range**: -32768 to +32767 (full int16_t range)
- **Typical Amplitude**: ±500 to ±2000 (representing ~0.5-2.0 mV after scaling)

### 2.2 Signal Characteristics
The input signal is assumed to be:
- Already filtered for powerline noise (50/60 Hz)
- Baseline wander removed (DC offset may still be present)
- Muscle artifact filtered
- Lead configuration: Single lead (Lead II equivalent)

### 2.3 Input Validation Requirements
- Array pointer must not be NULL
- Sample count must be between 200 and 1000
- Samples should be checked for saturation (all values at min/max indicate invalid signal)

---

## 3. Output Specifications

### 3.1 Primary Output
- **Data Type**: `uint8_t` or `int` (boolean value)
- **Values**:
  - `0` = No shock recommended (normal rhythm or non-shockable arrhythmia)
  - `1` = Shock recommended (VF or VT detected)

### 3.2 Optional Diagnostic Output
For debugging and educational purposes, the analyzer may optionally provide:
- Detected rhythm type (normal, VT, VF, unknown)
- Confidence level (0-100%)
- Heart rate estimate (bpm)
- Number of QRS complexes detected
- Dominant frequency (Hz)

---

## 4. Functional Requirements

### 4.1 Shockable Rhythm Detection

The analyzer SHALL detect the following shockable rhythms:

#### 4.1.1 Ventricular Fibrillation (VF)
**Definition**: Rapid, chaotic, and disorganized ventricular electrical activity with no identifiable QRS complexes.

**Detection Criteria** (ALL must be met):
1. **Dominant Frequency**: 4-10 Hz (measured in power spectrum)
2. **No QRS Detection**: No regular QRS complexes detected using Pan-Tompkins algorithm
3. **High Irregularity**: R-R interval coefficient of variation >50% OR zero-crossing rate >0.17
4. **Adequate Amplitude**: Signal amplitude sufficient to distinguish from asystole (peak-to-peak >100 in int16_t units)
5. **Duration**: Criteria met continuously for at least 3 seconds

**Clinical Context**: VF is the most common shockable rhythm in sudden cardiac arrest. Immediate defibrillation is required.

#### 4.1.2 Ventricular Tachycardia (VT)
**Definition**: Rapid ventricular rhythm with wide QRS complexes, rate >100 bpm.

**Detection Criteria** (ALL must be met):
1. **Heart Rate**: >120 bpm (R-R interval <500 ms at 100 Hz sampling = <50 samples)
2. **Wide QRS**: QRS duration >120 ms (>12 samples at 100 Hz)
3. **Regular Rhythm**: R-R interval coefficient of variation <20%
4. **Sustained**: At least 3 consecutive wide QRS complexes meeting criteria
5. **Duration**: Criteria met continuously for at least 2 seconds

**Clinical Context**: Pulseless VT requires immediate defibrillation. This detector assumes VT is pulseless when detected.

### 4.2 Non-Shockable Rhythms

The analyzer SHALL NOT recommend shock for:
- **Normal Sinus Rhythm**: Regular rhythm, HR 60-100 bpm, normal QRS width
- **Sinus Tachycardia**: Regular rhythm, HR >100 bpm, normal QRS width (<120 ms)
- **Supraventricular Tachycardia (SVT)**: Rapid but narrow QRS complexes
- **Asystole**: Flat line, no electrical activity
- **Pulseless Electrical Activity (PEA)**: Organized electrical activity without VF/VT characteristics

### 4.3 Analysis Window Requirements

- **Minimum Analysis Window**: 3 seconds (300 samples)
- **Recommended Analysis Window**: 5 seconds (500 samples)
- **Maximum Analysis Window**: 8 seconds (800 samples)
- **Update Rate**: Analysis should be performed on overlapping windows, updated every 1-2 seconds

### 4.4 Decision Logic

The shock decision SHALL follow this priority:

```
IF (VF detected with high confidence)
    THEN recommend shock
ELSE IF (VT detected with high confidence)
    THEN recommend shock
ELSE IF (signal quality insufficient)
    THEN no shock (insufficient data)
ELSE
    THEN no shock (non-shockable rhythm)
```

---

## 5. Performance Requirements

### 5.1 Detection Performance

| Metric | Requirement | Target |
|--------|-------------|--------|
| VF Sensitivity | >90% | >95% |
| VF Specificity | >90% | >95% |
| VT Sensitivity | >85% | >90% |
| VT Specificity | >90% | >95% |
| False Positive Rate | <10% | <5% |

**Note**: These targets are for educational purposes. Medical devices require >97% sensitivity and >96% specificity for VF detection.

### 5.2 Timing Requirements

| Operation | Requirement |
|-----------|-------------|
| Detection Latency | <8 seconds from rhythm onset |
| Processing Time | <100 ms per analysis cycle |
| Real-time Constraint | Must process at ≥100 Hz sample rate |

### 5.3 Resource Requirements

| Resource | Maximum | Typical |
|----------|---------|---------|
| RAM Usage | 10 KB | 7 KB |
| Flash/Code Size | 20 KB | 15 KB |
| CPU Utilization | 10% @ 64 MHz | <5% @ 64 MHz |
| Stack Depth | 2 KB | 1 KB |

### 5.4 Numerical Precision

- **Floating-point**: Use `float` (32-bit) for calculations (Cortex M33 has FPU)
- **Integer**: Use `int16_t` for samples, `int32_t` for accumulations
- **Acceptable Precision**: ±2% error in frequency measurements, ±5% in amplitude measurements

---

## 6. Algorithm Requirements

### 6.1 QRS Detection Algorithm

**Algorithm**: Pan-Tompkins or equivalent

**Steps Required**:
1. **Bandpass Filter**: 5-18 Hz (emphasizes QRS, attenuates P and T waves)
2. **Derivative**: 5-point derivative to emphasize slope
3. **Squaring**: Emphasize high-frequency components
4. **Moving Window Integration**: 150 ms window (~15 samples at 100 Hz)
5. **Adaptive Thresholding**: Separate signal and noise thresholds with learning
6. **Peak Detection**: With 200 ms refractory period (20 samples)

**Output**:
- Array of QRS peak locations (sample indices)
- R-R intervals (time between consecutive QRS peaks)

### 6.2 VF Detection Algorithm

**Approach**: Frequency domain + time domain features

**Required Components**:
1. **Frequency Analysis**:
   - Method: Goertzel algorithm for bins at 4, 6, 8, 10 Hz
   - Find dominant frequency in 4-10 Hz range
   - Calculate power in VF band vs total power

2. **Time Domain Analysis**:
   - Zero-crossing rate calculation
   - Threshold crossing interval (TCI) analysis
   - Amplitude variability assessment

3. **QRS Absence Check**:
   - Confirm no regular QRS complexes detected
   - Check for chaotic, disorganized waveform

**Decision Threshold**:
- Dominant frequency in 4-10 Hz range AND
- (Zero-crossing rate >0.17 OR no QRS detected for >2 seconds) AND
- Peak-to-peak amplitude >100 (not asystole)

### 6.3 VT Detection Algorithm

**Approach**: QRS-based rhythm analysis

**Required Components**:
1. **Heart Rate Calculation**:
   - Calculate from R-R intervals
   - Average over last 8 beats or 5 seconds
   - Threshold: >120 bpm for VT

2. **QRS Width Measurement**:
   - Measure from QRS onset to end
   - Method: Detect where signal crosses threshold (10-20% of peak)
   - Threshold: >120 ms (>12 samples) for wide QRS

3. **Rhythm Regularity**:
   - Calculate coefficient of variation: CV = (std_dev / mean) × 100%
   - Regular if CV <20%

4. **Confirmation**:
   - Require ≥3 consecutive beats meeting all criteria
   - Sustained for ≥2 seconds

---

## 7. Data Structure Requirements

### 7.1 Input Structure
```c
typedef struct {
    int16_t *samples;      // Pointer to sample array
    uint16_t num_samples;  // Number of samples (200-1000)
    uint16_t sample_rate;  // Sampling rate in Hz (100)
} ekg_input_t;
```

### 7.2 Output Structure
```c
typedef struct {
    uint8_t shock_advised;     // 0 = no shock, 1 = shock
    uint8_t rhythm_type;       // 0=unknown, 1=normal, 2=VT, 3=VF, 4=other
    uint8_t confidence;        // 0-100%
    uint16_t heart_rate;       // Beats per minute (0 if not detectable)
    float dominant_freq;       // Dominant frequency in Hz (for VF)
    uint8_t num_qrs_detected;  // Number of QRS complexes found
} ekg_analysis_result_t;
```

### 7.3 Internal State (if needed for multi-call processing)
```c
typedef struct {
    float filter_state[8];     // IIR filter state variables
    int16_t qrs_locs[50];      // QRS peak locations (circular buffer)
    uint8_t qrs_count;         // Number of QRS peaks detected
    float noise_level;         // Adaptive noise threshold
    float signal_level;        // Adaptive signal threshold
} ekg_analyzer_state_t;
```

---

## 8. Interface Requirements

### 8.1 Main Analysis Function

```c
/**
 * Analyze EKG signal and determine if defibrillator shock is recommended
 *
 * @param samples     Pointer to array of EKG samples (int16_t)
 * @param num_samples Number of samples in array (200-1000)
 * @param result      Pointer to structure to store analysis results
 * @return            0 on success, negative error code on failure
 */
int ekg_analyze(const int16_t *samples,
                uint16_t num_samples,
                ekg_analysis_result_t *result);
```

### 8.2 Error Codes

```c
#define EKG_SUCCESS           0
#define EKG_ERROR_NULL_PTR   -1
#define EKG_ERROR_INVALID_LEN -2
#define EKG_ERROR_LOW_QUALITY -3
#define EKG_ERROR_SATURATED   -4
```

### 8.3 Configuration Constants

```c
#define EKG_SAMPLE_RATE       100    // Hz
#define EKG_MIN_SAMPLES       200    // 2 seconds minimum
#define EKG_MAX_SAMPLES       1000   // 10 seconds maximum
#define EKG_ANALYSIS_WINDOW   500    // 5 seconds recommended
#define EKG_VT_HR_THRESHOLD   120    // bpm
#define EKG_VT_QRS_THRESHOLD  12     // samples (120ms at 100Hz)
#define EKG_VF_FREQ_MIN       4.0f   // Hz
#define EKG_VF_FREQ_MAX       10.0f  // Hz
#define EKG_MIN_AMPLITUDE     100    // int16_t units (distinguish from asystole)
```

---

## 9. Quality Requirements

### 9.1 Code Quality

- **Standard**: C99 compliance
- **Style**: Consistent naming, clear comments for algorithms
- **Documentation**: Function headers with parameters and return values
- **Modularity**: Separate modules for QRS detection, VT detection, VF detection
- **Testability**: Functions should be unit-testable

### 9.2 Numerical Stability

- Avoid division by zero (check denominators)
- Handle edge cases (flat signal, saturated signal, very few samples)
- Use appropriate data types to prevent overflow in accumulations
- Initialize all variables

### 9.3 Safety Requirements

- **Input Validation**: Always validate pointers and array bounds
- **Fail-Safe**: In case of uncertainty or error, default to "no shock"
- **No False Positives on Noise**: Reject noisy/invalid signals rather than false shock
- **Deterministic**: Same input always produces same output (no undefined behavior)

---

## 10. Testing Requirements

### 10.1 Unit Tests

Each module (QRS detection, VT detection, VF detection) shall have unit tests covering:
- Normal operation with valid inputs
- Edge cases (minimum/maximum samples, boundary thresholds)
- Invalid inputs (NULL pointers, out-of-range values)
- Known test vectors with expected outputs

### 10.2 Integration Tests

Full system tests with sample EKG recordings:
- **Normal Sinus Rhythm**: Should return shock_advised = 0
- **Sinus Tachycardia**: Should return shock_advised = 0
- **Ventricular Tachycardia**: Should return shock_advised = 1
- **Ventricular Fibrillation**: Should return shock_advised = 1
- **Asystole**: Should return shock_advised = 0
- **Noisy/Invalid Signal**: Should return error or shock_advised = 0

### 10.3 Test Data Sources

Recommended public databases for test data:
- MIT-BIH Arrhythmia Database
- AHA ECG Database
- PhysioNet databases
- Simulated waveforms (can be generated for initial testing)

### 10.4 Performance Tests

- **Timing**: Verify processing completes within 100 ms
- **Memory**: Verify RAM usage <10 KB
- **Accuracy**: Verify detection rates meet performance requirements

---

## 11. Constraints and Assumptions

### 11.1 Assumptions

1. Input signal is pre-filtered (baseline wander and high-frequency noise removed)
2. Single-lead EKG (Lead II equivalent)
3. Patient is adult (pediatric rhythms may differ)
4. Signal quality is adequate (not excessively noisy)
5. Sample rate is exactly 100 Hz (not variable)
6. Samples are uniformly spaced in time
7. Cortex M33 running at ≥64 MHz with FPU enabled

### 11.2 Limitations

1. **Not a medical device**: Educational/prototype only
2. **Single-lead**: Less reliable than multi-lead analysis
3. **No motion artifact handling**: Assumes patient is still
4. **No electrode contact detection**: Assumes electrodes properly attached
5. **Limited rhythm types**: Only detects VF and VT, not all arrhythmias
6. **Fixed thresholds**: Not adaptable to individual patient characteristics
7. **No trend analysis**: Analyzes single snapshot, not rhythm history

### 11.3 Environmental Constraints

- **Temperature**: Processor operates within specified range (typically -40°C to +85°C)
- **Power**: Sufficient power available for continuous operation
- **Memory**: At least 32 KB RAM and 64 KB Flash available
- **Clock**: Stable clock source for accurate timing

---

## 12. Traceability Matrix

| Requirement ID | Description | Algorithm Component | Test Method |
|---------------|-------------|---------------------|-------------|
| FR-VF-001 | Detect VF with >90% sensitivity | VF detector, Goertzel | Test data |
| FR-VT-001 | Detect VT with >85% sensitivity | VT detector, QRS analysis | Test data |
| FR-QRS-001 | Detect QRS complexes | Pan-Tompkins | Unit test |
| NFR-TIME-001 | Process in <100 ms | All components | Timing test |
| NFR-MEM-001 | Use <10 KB RAM | All components | Memory profiling |
| NFR-ACC-001 | False positive <10% | Decision logic | Test data |

---

## 13. References

### 13.1 Medical Standards

- AHA/ACC/HRS Guideline for Management of Ventricular Arrhythmias (2017)
- AHA Recommendations for ECG Standardization
- IEC 60601 (for reference, not compliance target)

### 13.2 Technical References

- Pan, J., & Tompkins, W. J. (1985). "A real-time QRS detection algorithm"
- Studies on VF detection algorithms in automated external defibrillators
- ARM Cortex-M33 Technical Reference Manual
- C99 Standard (ISO/IEC 9899:1999)

### 13.3 Database References

- PhysioNet - MIT-BIH Arrhythmia Database
- American Heart Association ECG Database
- CU Ventricular Tachyarrhythmia Database

---

## 14. Glossary

| Term | Definition |
|------|------------|
| **VF** | Ventricular Fibrillation - chaotic, disorganized ventricular activity |
| **VT** | Ventricular Tachycardia - rapid ventricular rhythm with wide QRS |
| **QRS** | Ventricular depolarization complex in ECG waveform |
| **R-R interval** | Time between consecutive QRS peaks |
| **bpm** | Beats per minute (heart rate unit) |
| **Hz** | Hertz (cycles per second, frequency unit) |
| **PEA** | Pulseless Electrical Activity - organized electrical without pulse |
| **Asystole** | Complete absence of electrical activity (flat line) |
| **SVT** | Supraventricular Tachycardia - rapid rhythm from above ventricles |
| **Goertzel** | Efficient algorithm for computing specific frequency components |
| **Pan-Tompkins** | Standard QRS detection algorithm |
| **FPU** | Floating Point Unit (hardware for float operations) |
| **CV** | Coefficient of Variation (standard deviation / mean) |

---

**Document Version**: 1.0
**Date**: 2025-11-15
**Status**: Draft for Review
