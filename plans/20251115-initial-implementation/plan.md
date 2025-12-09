# EKG Signal Analyzer - Implementation Plan

## 1. Architecture Overview

### 1.1 System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Main Analysis Function                    │
│                      ekg_analyze()                           │
└──────────────────┬──────────────────────────────────────────┘
                   │
        ┌──────────┴──────────┐
        │                     │
        ▼                     ▼
┌───────────────┐    ┌────────────────┐
│ Preprocessing │    │ QRS Detection  │
│   Module      │───▶│    Module      │
└───────────────┘    └────────┬───────┘
                              │
                 ┌────────────┴────────────┐
                 │                         │
                 ▼                         ▼
        ┌─────────────────┐      ┌─────────────────┐
        │  VT Detection   │      │  VF Detection   │
        │     Module      │      │     Module      │
        └────────┬────────┘      └────────┬────────┘
                 │                         │
                 └────────────┬────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │ Decision Logic   │
                    │     Module       │
                    └──────────────────┘
                              │
                              ▼
                        [Result Output]
```

### 1.2 Data Flow

```
Input: int16_t samples[num_samples]
  │
  ├─▶ [DC Offset Removal] ──▶ float normalized[num_samples]
  │
  ├─▶ [Bandpass Filter 5-18Hz] ──▶ float filtered[num_samples]
  │
  ├─▶ [Pan-Tompkins QRS Detection]
  │      ├─▶ Derivative
  │      ├─▶ Squaring
  │      ├─▶ Moving Window Integration
  │      └─▶ Adaptive Threshold Peak Detection
  │             │
  │             ├─▶ qrs_locs[] (QRS peak indices)
  │             └─▶ rr_intervals[] (R-R intervals)
  │
  ├─▶ [Heart Rate Analysis]
  │      └─▶ heart_rate (bpm), regularity (CV)
  │
  ├─▶ [QRS Width Measurement]
  │      └─▶ qrs_width[] (ms)
  │
  ├─▶ [VT Detection]
  │      ├─▶ HR > 120 bpm?
  │      ├─▶ QRS width > 120 ms?
  │      ├─▶ Regular rhythm (CV < 20%)?
  │      └─▶ ≥3 consecutive beats?
  │             │
  │             └─▶ VT confidence score
  │
  ├─▶ [VF Detection]
  │      ├─▶ Goertzel @ 4,6,8,10 Hz
  │      ├─▶ Dominant frequency 4-10 Hz?
  │      ├─▶ Zero-crossing rate > 0.17?
  │      ├─▶ No regular QRS?
  │      └─▶ Adequate amplitude?
  │             │
  │             └─▶ VF confidence score
  │
  └─▶ [Decision Logic]
         ├─▶ VF confidence > threshold? ──▶ SHOCK
         ├─▶ VT confidence > threshold? ──▶ SHOCK
         └─▶ Otherwise ──────────────────▶ NO SHOCK
            │
            ▼
Output: ekg_analysis_result_t
```

---

## 2. Module Specifications

### 2.1 Signal Preprocessing Module

**File**: `src/signal_processing.c` / `src/signal_processing.h`

**Purpose**: Prepare raw EKG samples for analysis

#### Functions:

##### 2.1.1 DC Offset Removal
```c
/**
 * Remove DC offset from signal by subtracting mean value
 *
 * @param input       Input signal (int16_t array)
 * @param output      Output signal (float array)
 * @param num_samples Number of samples
 */
void remove_dc_offset(const int16_t *input, float *output, uint16_t num_samples);
```

**Algorithm**:
```
1. Calculate mean:
   mean = (1/N) * Σ(input[i]) for i = 0 to N-1

2. Subtract mean from each sample:
   output[i] = (float)input[i] - mean
```

**Implementation Notes**:
- Use int32_t for accumulation to prevent overflow
- Convert to float after mean subtraction

##### 2.1.2 Bandpass Filter (5-18 Hz)
```c
/**
 * Apply bandpass filter (5-18 Hz) to emphasize QRS complexes
 * Uses cascaded 2nd-order IIR Butterworth filters
 *
 * @param input       Input signal (float array)
 * @param output      Output signal (float array)
 * @param num_samples Number of samples
 * @param state       Filter state for IIR (8 floats, initialized to 0)
 */
void bandpass_filter(const float *input, float *output,
                     uint16_t num_samples, float *state);
```

**Algorithm**:
Cascade of two 2nd-order IIR filters (high-pass 5 Hz, then low-pass 18 Hz)

**High-Pass Filter @ 5 Hz** (sample rate = 100 Hz):
```
Transfer function: H(z) = (b0 + b1*z^-1 + b2*z^-2) / (a0 + a1*z^-1 + a2*z^-2)

Normalized coefficients (a0 = 1):
b0 =  0.9150
b1 = -1.8299
b2 =  0.9150
a1 = -1.8227
a2 =  0.8372
```

**Low-Pass Filter @ 18 Hz** (sample rate = 100 Hz):
```
Normalized coefficients (a0 = 1):
b0 =  0.2452
b1 =  0.4905
b2 =  0.2452
a1 = -0.2097
a2 =  0.1906
```

**Direct Form II Implementation**:
```c
// For each 2nd-order section:
w[0] = x[n] - a1*w[1] - a2*w[2]
y[n] = b0*w[0] + b1*w[1] + b2*w[2]
w[2] = w[1]
w[1] = w[0]
```

**State Array**:
- `state[0..3]`: High-pass filter state [w1, w2, reserved, reserved]
- `state[4..7]`: Low-pass filter state [w1, w2, reserved, reserved]

##### 2.1.3 Moving Average Filter
```c
/**
 * Apply moving average filter for smoothing
 *
 * @param input       Input signal
 * @param output      Output signal
 * @param num_samples Number of samples
 * @param window_size Window size for averaging (e.g., 15 for 150ms at 100Hz)
 */
void moving_average(const float *input, float *output,
                   uint16_t num_samples, uint8_t window_size);
```

**Algorithm**:
```
For each sample i:
  sum = 0
  for j = max(0, i - window_size/2) to min(num_samples-1, i + window_size/2):
    sum += input[j]
  output[i] = sum / count
```

**Optimization**: Use sliding window to avoid recalculating entire sum

---

### 2.2 QRS Detection Module (Pan-Tompkins)

**File**: `src/qrs_detector.c` / `src/qrs_detector.h`

**Purpose**: Detect QRS complexes (ventricular depolarizations) in filtered EKG

#### Data Structures:

```c
typedef struct {
    uint16_t *locations;     // Array of QRS peak sample indices
    uint8_t count;           // Number of QRS peaks detected
    uint16_t *rr_intervals;  // R-R intervals in samples
    uint8_t rr_count;        // Number of R-R intervals
} qrs_detection_result_t;
```

#### Functions:

##### 2.2.1 Derivative Filter
```c
/**
 * Apply 5-point derivative to emphasize QRS slope
 *
 * @param input       Input filtered signal
 * @param output      Output derivative signal
 * @param num_samples Number of samples
 */
void derivative_filter(const float *input, float *output, uint16_t num_samples);
```

**Algorithm** (5-point derivative):
```
y[n] = (1/8T) * (-x[n-2] - 2*x[n-1] + 2*x[n+1] + x[n+2])

At 100 Hz, T = 0.01s, so 1/8T = 12.5
Simplified: y[n] = 0.125 * (-x[n-2] - 2*x[n-1] + 2*x[n+1] + x[n+2])
```

**Boundary Handling**:
- For n < 2: pad with x[0]
- For n >= num_samples-2: pad with x[num_samples-1]

##### 2.2.2 Squaring
```c
/**
 * Square the signal to emphasize high-frequency components
 *
 * @param input       Input derivative signal
 * @param output      Output squared signal
 * @param num_samples Number of samples
 */
void square_signal(const float *input, float *output, uint16_t num_samples);
```

**Algorithm**:
```
output[i] = input[i] * input[i]
```

##### 2.2.3 Moving Window Integration
```c
/**
 * Apply moving window integration (150ms window at 100Hz = 15 samples)
 *
 * @param input       Input squared signal
 * @param output      Output integrated signal
 * @param num_samples Number of samples
 */
void moving_window_integrate(const float *input, float *output,
                             uint16_t num_samples);
```

**Algorithm**:
```
Window size N = 15 samples (150ms at 100Hz)

output[i] = (1/N) * Σ(input[i-j]) for j = 0 to N-1
```

##### 2.2.4 Adaptive Threshold Detection
```c
/**
 * Detect QRS peaks using adaptive thresholding
 *
 * @param integrated  Integrated signal from moving window
 * @param num_samples Number of samples
 * @param result      Structure to store detected QRS locations
 */
void detect_qrs_peaks(const float *integrated, uint16_t num_samples,
                     qrs_detection_result_t *result);
```

**Algorithm**:

**Initialization** (analyze first 2 seconds = 200 samples):
```
SPKI = max(integrated[0..199]) * 0.25      // Signal peak estimate
NPKI = mean(integrated[0..199]) * 0.5      // Noise peak estimate
THRESHOLD_I1 = NPKI + 0.25 * (SPKI - NPKI) // Primary threshold
THRESHOLD_I2 = 0.5 * THRESHOLD_I1          // Secondary threshold (search-back)
```

**Detection Loop**:
```
For each sample i from 200 to num_samples:

  1. Check if potential peak:
     IF integrated[i] > THRESHOLD_I1 AND
        integrated[i] is local maximum AND
        (i - last_qrs_location) > 20 samples (200ms refractory)
     THEN:
       - Mark as QRS peak
       - SPKI = 0.125 * integrated[i] + 0.875 * SPKI  (update signal level)
       - Update thresholds:
         THRESHOLD_I1 = NPKI + 0.25 * (SPKI - NPKI)
         THRESHOLD_I2 = 0.5 * THRESHOLD_I1

  2. If no peak detected for >166 samples (1.66s = 36 bpm threshold):
     - Search back with THRESHOLD_I2
     - If peak found, update SPKI and thresholds

  3. If not a peak:
     - NPKI = 0.125 * integrated[i] + 0.875 * NPKI  (update noise level)
     - Update thresholds
```

**Local Maximum Test**:
```
integrated[i] is local max if:
  integrated[i] > integrated[i-1] AND
  integrated[i] > integrated[i+1]
```

**Search-Back**:
If no QRS detected for 1.66 seconds (166 samples):
```
Search in range [last_qrs + 20, current - 20] for peaks > THRESHOLD_I2
Take highest peak if found
```

##### 2.2.5 R-R Interval Calculation
```c
/**
 * Calculate R-R intervals from detected QRS locations
 *
 * @param qrs_locs    Array of QRS peak sample indices
 * @param num_qrs     Number of QRS peaks
 * @param rr_intervals Output array for R-R intervals (in samples)
 * @return            Number of R-R intervals (num_qrs - 1)
 */
uint8_t calculate_rr_intervals(const uint16_t *qrs_locs, uint8_t num_qrs,
                               uint16_t *rr_intervals);
```

**Algorithm**:
```
For i = 0 to num_qrs - 2:
  rr_intervals[i] = qrs_locs[i+1] - qrs_locs[i]
```

##### 2.2.6 Complete QRS Detection
```c
/**
 * Complete Pan-Tompkins QRS detection pipeline
 *
 * @param filtered    Bandpass filtered EKG signal (5-18 Hz)
 * @param num_samples Number of samples
 * @param result      Structure to store QRS detection results
 * @return            0 on success, negative on error
 */
int detect_qrs_complexes(const float *filtered, uint16_t num_samples,
                        qrs_detection_result_t *result);
```

**Algorithm Flow**:
```
1. Allocate temporary buffers (derivative, squared, integrated)
2. Apply derivative_filter()
3. Apply square_signal()
4. Apply moving_window_integrate()
5. Apply detect_qrs_peaks()
6. Calculate R-R intervals
7. Free temporary buffers
8. Return result
```

---

### 2.3 Heart Rate Analysis Module

**File**: `src/heart_rate.c` / `src/heart_rate.h`

#### Functions:

##### 2.3.1 Heart Rate Calculation
```c
/**
 * Calculate heart rate from R-R intervals
 *
 * @param rr_intervals Array of R-R intervals (in samples)
 * @param num_intervals Number of R-R intervals
 * @param sample_rate  Sample rate in Hz (100)
 * @return             Heart rate in bpm, or 0 if cannot calculate
 */
float calculate_heart_rate(const uint16_t *rr_intervals, uint8_t num_intervals,
                          uint16_t sample_rate);
```

**Algorithm**:
```
1. Calculate mean R-R interval:
   mean_rr = (1/N) * Σ(rr_intervals[i]) for i = 0 to N-1
   (in samples)

2. Convert to time:
   mean_rr_sec = mean_rr / sample_rate

3. Convert to heart rate:
   heart_rate = 60.0 / mean_rr_sec

Example:
  mean_rr = 80 samples at 100 Hz
  mean_rr_sec = 80/100 = 0.8 seconds
  heart_rate = 60/0.8 = 75 bpm
```

##### 2.3.2 R-R Regularity (Coefficient of Variation)
```c
/**
 * Calculate R-R interval regularity (coefficient of variation)
 *
 * @param rr_intervals Array of R-R intervals (in samples)
 * @param num_intervals Number of R-R intervals
 * @return             Coefficient of variation as percentage (0-100+)
 */
float calculate_rr_regularity(const uint16_t *rr_intervals,
                              uint8_t num_intervals);
```

**Algorithm**:
```
1. Calculate mean:
   mean = (1/N) * Σ(rr_intervals[i])

2. Calculate variance:
   variance = (1/N) * Σ((rr_intervals[i] - mean)^2)

3. Calculate standard deviation:
   std_dev = sqrt(variance)

4. Calculate coefficient of variation:
   CV = (std_dev / mean) * 100

Interpretation:
  CV < 5%  : Very regular (normal sinus rhythm)
  CV 5-10% : Mostly regular (possibly VT)
  CV 10-20%: Irregular
  CV > 50% : Very irregular (VF)
```

---

### 2.4 VT Detection Module

**File**: `src/vt_detector.c` / `src/vt_detector.h`

**Purpose**: Detect Ventricular Tachycardia based on QRS analysis

#### Data Structures:

```c
typedef struct {
    uint8_t vt_detected;      // 1 if VT detected, 0 otherwise
    float confidence;         // 0.0 to 1.0
    float heart_rate;         // Heart rate in bpm
    float mean_qrs_width;     // Mean QRS width in ms
    float rr_regularity;      // CV in percentage
    uint8_t wide_qrs_count;   // Number of consecutive wide QRS
} vt_detection_result_t;
```

#### Functions:

##### 2.4.1 QRS Width Measurement
```c
/**
 * Measure QRS complex width
 *
 * @param signal      Filtered EKG signal
 * @param qrs_loc     QRS peak location (sample index)
 * @param num_samples Total number of samples
 * @param sample_rate Sample rate in Hz
 * @return            QRS width in milliseconds
 */
float measure_qrs_width(const float *signal, uint16_t qrs_loc,
                       uint16_t num_samples, uint16_t sample_rate);
```

**Algorithm**:
```
1. Find QRS peak amplitude:
   peak_amp = signal[qrs_loc]

2. Calculate threshold (20% of peak):
   threshold = 0.2 * abs(peak_amp)

3. Search backward from peak for QRS onset:
   onset = qrs_loc
   while onset > 0 AND abs(signal[onset]) > threshold:
     onset--

4. Search forward from peak for QRS end:
   end = qrs_loc
   while end < num_samples-1 AND abs(signal[end]) > threshold:
     end++

5. Calculate width:
   width_samples = end - onset
   width_ms = (width_samples / sample_rate) * 1000

Example at 100 Hz:
  onset = 100, end = 115
  width = 15 samples
  width_ms = (15/100) * 1000 = 150 ms
```

##### 2.4.2 VT Detection
```c
/**
 * Detect Ventricular Tachycardia
 *
 * @param filtered     Filtered EKG signal
 * @param num_samples  Number of samples
 * @param qrs_result   QRS detection results
 * @param sample_rate  Sample rate in Hz
 * @param result       Structure to store VT detection results
 * @return             0 on success, negative on error
 */
int detect_vt(const float *filtered, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             vt_detection_result_t *result);
```

**Algorithm**:

```
1. Initialize result structure

2. Calculate heart rate:
   heart_rate = calculate_heart_rate(qrs_result->rr_intervals,
                                     qrs_result->rr_count,
                                     sample_rate)

3. If heart_rate <= 120 bpm:
   return NOT VT (confidence = 0)

4. Measure QRS width for each detected QRS:
   For i = 0 to qrs_result->count - 1:
     qrs_widths[i] = measure_qrs_width(filtered,
                                       qrs_result->locations[i],
                                       num_samples, sample_rate)

   mean_qrs_width = mean(qrs_widths[])

5. Calculate R-R regularity:
   rr_cv = calculate_rr_regularity(qrs_result->rr_intervals,
                                   qrs_result->rr_count)

6. Count consecutive wide QRS complexes (>120ms):
   wide_count = 0
   max_consecutive = 0
   current_consecutive = 0

   For each qrs_widths[i]:
     if qrs_widths[i] > 120:
       current_consecutive++
       wide_count++
     else:
       if current_consecutive > max_consecutive:
         max_consecutive = current_consecutive
       current_consecutive = 0

7. VT Decision Logic:

   vt_score = 0.0

   // Criterion 1: Heart rate
   if heart_rate > 120:
     vt_score += 0.3
   if heart_rate > 150:
     vt_score += 0.1

   // Criterion 2: QRS width
   if mean_qrs_width > 120:
     vt_score += 0.3
   if mean_qrs_width > 140:
     vt_score += 0.1

   // Criterion 3: Regularity
   if rr_cv < 20:
     vt_score += 0.2
   if rr_cv < 10:
     vt_score += 0.1

   // Criterion 4: Sustained (≥3 consecutive wide QRS)
   if max_consecutive >= 3:
     vt_score += 0.2

   // VT detected if score >= 0.7
   if vt_score >= 0.7:
     result->vt_detected = 1
     result->confidence = vt_score
   else:
     result->vt_detected = 0
     result->confidence = vt_score

8. Fill result structure and return
```

**Decision Thresholds**:
- Heart rate: >120 bpm (required)
- QRS width: >120 ms (required for high confidence)
- R-R regularity: CV <20% (indicates regular rhythm)
- Consecutive wide QRS: ≥3 (indicates sustained VT)
- Confidence threshold: ≥0.7 for VT detection

---

### 2.5 VF Detection Module

**File**: `src/vf_detector.c` / `src/vf_detector.h`

**Purpose**: Detect Ventricular Fibrillation using frequency and time domain analysis

#### Data Structures:

```c
typedef struct {
    uint8_t vf_detected;      // 1 if VF detected, 0 otherwise
    float confidence;         // 0.0 to 1.0
    float dominant_freq;      // Dominant frequency in Hz
    float zero_crossing_rate; // Zero crossings per second
    float amplitude;          // Peak-to-peak amplitude
    uint8_t qrs_absent;       // 1 if no QRS detected
} vf_detection_result_t;
```

#### Functions:

##### 2.5.1 Goertzel Algorithm (Single Frequency)
```c
/**
 * Calculate magnitude at specific frequency using Goertzel algorithm
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @param target_freq Target frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @return            Magnitude at target frequency
 */
float goertzel_magnitude(const float *signal, uint16_t num_samples,
                        float target_freq, uint16_t sample_rate);
```

**Algorithm**:

```
1. Calculate coefficient:
   k = (int)(0.5 + (num_samples * target_freq / sample_rate))
   omega = (2.0 * PI * k) / num_samples
   coeff = 2.0 * cos(omega)

2. Initialize:
   s_prev = 0.0
   s_prev2 = 0.0

3. Process all samples:
   For i = 0 to num_samples - 1:
     s = signal[i] + coeff * s_prev - s_prev2
     s_prev2 = s_prev
     s_prev = s

4. Calculate magnitude:
   real = s_prev - s_prev2 * cos(omega)
   imag = s_prev2 * sin(omega)
   magnitude = sqrt(real^2 + imag^2)

   return magnitude

Example for 6 Hz at 100 Hz sample rate, 500 samples:
  k = 0.5 + (500 * 6 / 100) = 30.5 = 30
  omega = (2 * PI * 30) / 500 = 0.377
  coeff = 2 * cos(0.377) = 1.859
```

##### 2.5.2 Dominant Frequency Detection
```c
/**
 * Find dominant frequency in VF range (4-10 Hz)
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @param sample_rate Sample rate in Hz
 * @return            Dominant frequency in Hz, or 0 if not in VF range
 */
float find_dominant_frequency(const float *signal, uint16_t num_samples,
                              uint16_t sample_rate);
```

**Algorithm**:

```
1. Define test frequencies in VF range:
   test_freqs[] = {4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}
   num_freqs = 7

2. Calculate magnitude at each frequency:
   For i = 0 to num_freqs - 1:
     magnitudes[i] = goertzel_magnitude(signal, num_samples,
                                        test_freqs[i], sample_rate)

3. Find frequency with maximum magnitude:
   max_mag = magnitudes[0]
   max_idx = 0
   For i = 1 to num_freqs - 1:
     if magnitudes[i] > max_mag:
       max_mag = magnitudes[i]
       max_idx = i

4. Check if dominant frequency is in VF range:
   dominant_freq = test_freqs[max_idx]

   // Calculate total power in VF band
   vf_power = sum(magnitudes[])

   // Calculate power in non-VF bands (0-4 Hz and 10-30 Hz)
   // Simplified: check if VF band has >60% of power

   if max_mag > 0.6 * vf_power:
     return dominant_freq
   else:
     return 0  // No clear dominant frequency in VF range
```

##### 2.5.3 Zero-Crossing Rate
```c
/**
 * Calculate zero-crossing rate
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @param sample_rate Sample rate in Hz
 * @return            Zero crossings per second
 */
float calculate_zero_crossing_rate(const float *signal, uint16_t num_samples,
                                   uint16_t sample_rate);
```

**Algorithm**:

```
1. Count zero crossings:
   crossings = 0
   For i = 1 to num_samples - 1:
     if (signal[i-1] >= 0 AND signal[i] < 0) OR
        (signal[i-1] < 0 AND signal[i] >= 0):
       crossings++

2. Calculate rate:
   duration_sec = num_samples / sample_rate
   zcr = crossings / duration_sec

Example:
  500 samples at 100 Hz = 5 seconds
  120 zero crossings
  zcr = 120 / 5 = 24 crossings/second = 0.24
```

##### 2.5.4 Amplitude Measurement
```c
/**
 * Calculate peak-to-peak amplitude
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @return            Peak-to-peak amplitude
 */
float calculate_amplitude(const float *signal, uint16_t num_samples);
```

**Algorithm**:

```
1. Find minimum and maximum:
   min_val = signal[0]
   max_val = signal[0]

   For i = 1 to num_samples - 1:
     if signal[i] < min_val:
       min_val = signal[i]
     if signal[i] > max_val:
       max_val = signal[i]

2. Calculate peak-to-peak:
   amplitude = max_val - min_val

   return amplitude
```

##### 2.5.5 VF Detection
```c
/**
 * Detect Ventricular Fibrillation
 *
 * @param signal       Original (DC-removed) EKG signal
 * @param num_samples  Number of samples
 * @param qrs_result   QRS detection results
 * @param sample_rate  Sample rate in Hz
 * @param result       Structure to store VF detection results
 * @return             0 on success, negative on error
 */
int detect_vf(const float *signal, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             vf_detection_result_t *result);
```

**Algorithm**:

```
1. Initialize result structure

2. Calculate dominant frequency:
   dominant_freq = find_dominant_frequency(signal, num_samples, sample_rate)

3. Calculate zero-crossing rate:
   zcr = calculate_zero_crossing_rate(signal, num_samples, sample_rate)

4. Calculate amplitude:
   amplitude = calculate_amplitude(signal, num_samples)

5. Check QRS detection:
   qrs_count = qrs_result->count
   duration_sec = num_samples / sample_rate
   qrs_rate = qrs_count / duration_sec  // QRS per second

   // If fewer than 1 QRS per 2 seconds, consider QRS absent
   if qrs_rate < 0.5:
     qrs_absent = 1
   else:
     qrs_absent = 0

6. VF Decision Logic:

   vf_score = 0.0

   // Criterion 1: Dominant frequency in VF range (4-10 Hz)
   if dominant_freq >= 4.0 AND dominant_freq <= 10.0:
     vf_score += 0.4
     // Bonus for typical VF frequency (5-8 Hz)
     if dominant_freq >= 5.0 AND dominant_freq <= 8.0:
       vf_score += 0.1

   // Criterion 2: High zero-crossing rate
   if zcr > 0.17:
     vf_score += 0.2
   if zcr > 0.25:
     vf_score += 0.1

   // Criterion 3: No regular QRS complexes
   if qrs_absent == 1:
     vf_score += 0.3
   else if qrs_count < 5 AND duration_sec >= 5:
     vf_score += 0.1  // Very few QRS (possibly coarse VF)

   // Criterion 4: Adequate amplitude (not asystole)
   if amplitude > 100:
     vf_score += 0.1
   else:
     vf_score = 0  // Too low amplitude, likely asystole, NOT VF

   // VF detected if score >= 0.7
   if vf_score >= 0.7:
     result->vf_detected = 1
     result->confidence = vf_score
   else:
     result->vf_detected = 0
     result->confidence = vf_score

7. Fill result structure:
   result->dominant_freq = dominant_freq
   result->zero_crossing_rate = zcr
   result->amplitude = amplitude
   result->qrs_absent = qrs_absent

8. Return success
```

**Decision Thresholds**:
- Dominant frequency: 4-10 Hz (required, especially 5-8 Hz)
- Zero-crossing rate: >0.17 (indicates high-frequency chaotic activity)
- QRS absent: <0.5 QRS/second (no regular rhythm)
- Amplitude: >100 (distinguishes from asystole)
- Confidence threshold: ≥0.7 for VF detection

---

### 2.6 Decision Logic Module

**File**: `src/ekg_analyzer.c` / `src/ekg_analyzer.h`

**Purpose**: Main analysis function integrating all modules

#### Main Function:

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

**Algorithm**:

```
1. Input Validation:

   if samples == NULL OR result == NULL:
     return EKG_ERROR_NULL_PTR

   if num_samples < EKG_MIN_SAMPLES OR num_samples > EKG_MAX_SAMPLES:
     return EKG_ERROR_INVALID_LEN

   // Check for saturated signal (all samples at min or max)
   saturated_count = 0
   for i = 0 to num_samples - 1:
     if samples[i] == -32768 OR samples[i] == 32767:
       saturated_count++

   if saturated_count > num_samples * 0.9:  // >90% saturated
     return EKG_ERROR_SATURATED

2. Initialize result structure:
   memset(result, 0, sizeof(ekg_analysis_result_t))

3. Preprocessing:

   // Allocate working buffers
   float *signal = malloc(num_samples * sizeof(float))
   float *filtered = malloc(num_samples * sizeof(float))

   // Remove DC offset
   remove_dc_offset(samples, signal, num_samples)

   // Apply bandpass filter (5-18 Hz)
   float filter_state[8] = {0}
   bandpass_filter(signal, filtered, num_samples, filter_state)

4. QRS Detection:

   qrs_detection_result_t qrs_result
   qrs_result.locations = malloc(50 * sizeof(uint16_t))
   qrs_result.rr_intervals = malloc(50 * sizeof(uint16_t))

   int ret = detect_qrs_complexes(filtered, num_samples, &qrs_result)
   if ret < 0:
     // QRS detection failed, but continue (might be VF)
     qrs_result.count = 0
     qrs_result.rr_count = 0

5. VT Detection:

   vt_detection_result_t vt_result

   detect_vt(filtered, num_samples, &qrs_result,
             EKG_SAMPLE_RATE, &vt_result)

6. VF Detection:

   vf_detection_result_t vf_result

   detect_vf(signal, num_samples, &qrs_result,
             EKG_SAMPLE_RATE, &vf_result)

7. Decision Logic:

   // Priority: VF first (more critical), then VT

   if vf_result.vf_detected == 1 AND vf_result.confidence >= 0.7:
     result->shock_advised = 1
     result->rhythm_type = 3  // VF
     result->confidence = (uint8_t)(vf_result.confidence * 100)
     result->dominant_freq = vf_result.dominant_freq
     result->heart_rate = 0  // Cannot measure in VF
     result->num_qrs_detected = qrs_result.count

   else if vt_result.vt_detected == 1 AND vt_result.confidence >= 0.7:
     result->shock_advised = 1
     result->rhythm_type = 2  // VT
     result->confidence = (uint8_t)(vt_result.confidence * 100)
     result->heart_rate = (uint16_t)vt_result.heart_rate
     result->dominant_freq = 0
     result->num_qrs_detected = qrs_result.count

   else:
     result->shock_advised = 0

     // Determine rhythm type for diagnostics
     if qrs_result.count > 0:
       float hr = calculate_heart_rate(qrs_result.rr_intervals,
                                       qrs_result.rr_count,
                                       EKG_SAMPLE_RATE)
       result->heart_rate = (uint16_t)hr

       if hr >= 60 AND hr <= 100:
         result->rhythm_type = 1  // Normal sinus rhythm
       else:
         result->rhythm_type = 4  // Other (non-shockable)

       result->num_qrs_detected = qrs_result.count
       result->confidence = 50  // Medium confidence for non-shockable
     else:
       result->rhythm_type = 0  // Unknown/indeterminate
       result->confidence = 0

8. Cleanup:
   free(signal)
   free(filtered)
   free(qrs_result.locations)
   free(qrs_result.rr_intervals)

9. Return success:
   return EKG_SUCCESS
```

**Decision Priority**:
1. **VF** (highest priority): Most time-critical, requires immediate shock
2. **VT** (second priority): Also requires shock if detected
3. **Non-shockable**: All other rhythms (default to no shock when uncertain)

---

## 3. File Structure

```
project/
├── docs/
│   ├── specs.md              # Requirements specification
│   └── plan.md               # This implementation plan
├── src/
│   ├── ekg_analyzer.c        # Main analysis function
│   ├── ekg_analyzer.h        # Main header with public API
│   ├── signal_processing.c   # Preprocessing and filtering
│   ├── signal_processing.h
│   ├── qrs_detector.c        # Pan-Tompkins QRS detection
│   ├── qrs_detector.h
│   ├── heart_rate.c          # Heart rate and R-R analysis
│   ├── heart_rate.h
│   ├── vt_detector.c         # VT detection logic
│   ├── vt_detector.h
│   ├── vf_detector.c         # VF detection logic
│   ├── vf_detector.h
│   └── ekg_config.h          # Configuration constants
├── tests/
│   ├── test_signal_processing.c
│   ├── test_qrs_detector.c
│   ├── test_vt_detector.c
│   ├── test_vf_detector.c
│   ├── test_integration.c
│   └── test_data/
│       ├── normal_rhythm.txt
│       ├── vt_sample.txt
│       ├── vf_sample.txt
│       └── README.md
├── Makefile                  # Build configuration
└── README.md                 # Project overview
```

---

## 4. Implementation Phases

### Phase 1: Project Setup and Infrastructure
**Duration**: 1 day

**Tasks**:
1. Create directory structure
2. Create header files with function prototypes and data structures
3. Create `ekg_config.h` with all constants and thresholds
4. Set up Makefile for compilation
5. Create stub implementations (functions that return error/empty results)
6. Verify compilation without errors

**Deliverables**:
- Complete file structure
- All header files with documented interfaces
- Compiling (but non-functional) code

### Phase 2: Signal Preprocessing Module
**Duration**: 1-2 days

**Tasks**:
1. Implement `remove_dc_offset()`
2. Implement `bandpass_filter()` with IIR coefficients
3. Implement `moving_average()`
4. Create unit tests with synthetic signals
5. Verify filter frequency response (manual check with known frequencies)

**Deliverables**:
- Functional signal_processing.c module
- Unit tests passing
- Documented filter coefficients

**Testing**:
- Test DC offset removal with constant offset signal
- Test bandpass filter with sine waves at 1 Hz, 10 Hz, 30 Hz
- Verify 5-18 Hz band passes with minimal attenuation
- Verify <5 Hz and >18 Hz are attenuated

### Phase 3: QRS Detection Module (Pan-Tompkins)
**Duration**: 2-3 days

**Tasks**:
1. Implement `derivative_filter()`
2. Implement `square_signal()`
3. Implement `moving_window_integrate()`
4. Implement `detect_qrs_peaks()` with adaptive thresholding
5. Implement `calculate_rr_intervals()`
6. Implement `detect_qrs_complexes()` (complete pipeline)
7. Create unit tests with synthetic QRS waveforms
8. Test with simple simulated EKG (regular peaks)

**Deliverables**:
- Functional qrs_detector.c module
- Reliable QRS detection on clean signals
- Unit tests passing

**Testing**:
- Test derivative filter output shape
- Test detection with synthetic signal (known QRS locations)
- Test with different heart rates (60, 100, 150 bpm)
- Verify 200ms refractory period prevents double-detection
- Check adaptive threshold adjustment

### Phase 4: Heart Rate Analysis Module
**Duration**: 1 day

**Tasks**:
1. Implement `calculate_heart_rate()`
2. Implement `calculate_rr_regularity()`
3. Create unit tests with known R-R intervals
4. Verify calculations with manual computation

**Deliverables**:
- Functional heart_rate.c module
- Accurate HR and CV calculations
- Unit tests passing

**Testing**:
- Test with regular intervals (CV should be ~0)
- Test with irregular intervals (calculate expected CV)
- Test edge cases (very few intervals, very high/low HR)

### Phase 5: VT Detection Module
**Duration**: 2 days

**Tasks**:
1. Implement `measure_qrs_width()`
2. Implement `detect_vt()` with scoring logic
3. Create unit tests with simulated VT waveforms
4. Tune thresholds if needed

**Deliverables**:
- Functional vt_detector.c module
- VT detection on synthetic VT signals
- Unit tests passing

**Testing**:
- Test with normal rhythm (narrow QRS, HR 75) -> No VT
- Test with sinus tachycardia (narrow QRS, HR 130) -> No VT
- Test with VT (wide QRS >120ms, HR 150) -> VT detected
- Test threshold boundary cases

### Phase 6: VF Detection Module
**Duration**: 2-3 days

**Tasks**:
1. Implement `goertzel_magnitude()`
2. Implement `find_dominant_frequency()`
3. Implement `calculate_zero_crossing_rate()`
4. Implement `calculate_amplitude()`
5. Implement `detect_vf()` with scoring logic
6. Create unit tests with synthetic VF waveforms
7. Tune thresholds if needed

**Deliverables**:
- Functional vf_detector.c module
- VF detection on synthetic VF signals
- Unit tests passing

**Testing**:
- Test Goertzel with known sine waves (verify magnitude)
- Test with VF simulation (random signal 4-10 Hz) -> VF detected
- Test with normal rhythm -> No VF
- Test with asystole (flat line) -> No VF
- Test zero-crossing rate with known signals

### Phase 7: Integration and Main Analysis Function
**Duration**: 1-2 days

**Tasks**:
1. Implement `ekg_analyze()` main function
2. Integrate all modules
3. Implement decision logic (VF priority, then VT)
4. Create integration tests with complete EKG samples
5. Test with various rhythm types

**Deliverables**:
- Functional ekg_analyzer.c main module
- Complete system integration
- Integration tests passing

**Testing**:
- Test with normal sinus rhythm -> shock_advised = 0
- Test with VT -> shock_advised = 1, rhythm_type = 2
- Test with VF -> shock_advised = 1, rhythm_type = 3
- Test with invalid inputs (NULL, too few samples)
- Test with saturated signal
- Verify error handling

### Phase 8: Optimization and Documentation
**Duration**: 1-2 days

**Tasks**:
1. Profile code for performance bottlenecks
2. Optimize critical sections if needed
3. Measure RAM usage and verify <10 KB
4. Add comprehensive code comments
5. Create README with usage examples
6. Document test data requirements

**Deliverables**:
- Optimized code (if needed)
- Complete documentation
- Performance measurements
- User guide

**Testing**:
- Measure execution time (should be <100 ms)
- Measure RAM usage (should be <10 KB)
- Verify CPU utilization on target hardware
- Stress test with maximum duration (1000 samples)

---

## 5. Testing Strategy

### 5.1 Unit Testing

**Framework**: Simple C test framework or custom assertions

**Test Categories**:

1. **Signal Processing Tests**:
   - DC offset removal accuracy
   - Filter frequency response
   - Moving average correctness
   - Boundary conditions

2. **QRS Detection Tests**:
   - Derivative filter output
   - Peak detection accuracy
   - Refractory period enforcement
   - Adaptive threshold behavior
   - R-R interval calculation

3. **Heart Rate Tests**:
   - HR calculation accuracy
   - CV calculation accuracy
   - Edge cases (few intervals, extreme rates)

4. **VT Detection Tests**:
   - QRS width measurement
   - VT scoring logic
   - Threshold boundaries
   - False positive prevention

5. **VF Detection Tests**:
   - Goertzel algorithm accuracy
   - Dominant frequency detection
   - Zero-crossing rate
   - VF scoring logic
   - Asystole rejection

### 5.2 Integration Testing

**Test Data Requirements**:

Create or obtain EKG samples for:
- Normal sinus rhythm (60-100 bpm)
- Sinus tachycardia (100-150 bpm, narrow QRS)
- Ventricular tachycardia (120-250 bpm, wide QRS)
- Ventricular fibrillation (chaotic, 4-10 Hz dominant)
- Asystole (flat line)
- Noisy signal (to test robustness)

**Test Data Format**:
```
# Comment lines start with #
# Sample rate: 100 Hz
# Duration: 5 seconds
# Number of samples: 500
-120
-115
-110
...
```

**Test Cases**:

| Test ID | Input | Expected Output | Pass Criteria |
|---------|-------|----------------|---------------|
| INT-001 | Normal rhythm | shock_advised=0, rhythm_type=1 | Correct classification |
| INT-002 | Sinus tachy | shock_advised=0, rhythm_type=4 | No false positive |
| INT-003 | VT sample | shock_advised=1, rhythm_type=2 | VT detected |
| INT-004 | VF sample | shock_advised=1, rhythm_type=3 | VF detected |
| INT-005 | Asystole | shock_advised=0 | No false positive |
| INT-006 | NULL pointer | EKG_ERROR_NULL_PTR | Error handling |
| INT-007 | Too few samples | EKG_ERROR_INVALID_LEN | Error handling |
| INT-008 | Saturated signal | EKG_ERROR_SATURATED | Error handling |

### 5.3 Performance Testing

**Metrics to Measure**:

1. **Execution Time**:
   - Measure time for `ekg_analyze()` with 500 samples
   - Target: <100 ms on Cortex M33 @ 64 MHz
   - Method: Use SysTick timer or profiling tools

2. **RAM Usage**:
   - Static allocation: measure sizeof() all global buffers
   - Dynamic allocation: measure malloc() calls
   - Stack usage: measure maximum stack depth
   - Target: <10 KB total

3. **Flash Usage**:
   - Measure compiled code size
   - Target: <20 KB
   - Method: objdump or linker map file

4. **CPU Utilization**:
   - Measure cycles per sample processing
   - Target: <5% at 100 Hz sample rate
   - Method: Cycle counter on Cortex M33

**Performance Test Plan**:
```c
void performance_test() {
    // Prepare test signal (500 samples)
    int16_t test_signal[500];
    generate_test_vt(test_signal, 500);

    // Measure execution time
    uint32_t start_cycles = DWT->CYCCNT;

    ekg_analysis_result_t result;
    ekg_analyze(test_signal, 500, &result);

    uint32_t end_cycles = DWT->CYCCNT;
    uint32_t elapsed_cycles = end_cycles - start_cycles;

    float time_ms = (elapsed_cycles / 64000.0);  // @ 64 MHz

    printf("Execution time: %.2f ms\n", time_ms);
    printf("Cycles: %lu\n", elapsed_cycles);

    // Check against target
    assert(time_ms < 100.0);
}
```

### 5.4 Validation with Public Data

**Optional but Recommended**:

If time permits, test against public EKG databases:

1. **MIT-BIH Arrhythmia Database**:
   - Download from PhysioNet
   - Resample to 100 Hz if needed
   - Test VT/VF episodes
   - Calculate sensitivity/specificity

2. **CU Database**:
   - Ventricular tachyarrhythmia database
   - Contains annotated VT/VF episodes

**Validation Metrics**:
- Sensitivity (True Positive Rate)
- Specificity (True Negative Rate)
- False Positive Rate
- Detection Latency

**Target Performance** (from specs):
- VF Sensitivity: >90% (target >95%)
- VF Specificity: >90% (target >95%)
- VT Sensitivity: >85% (target >90%)
- VT Specificity: >90% (target >95%)

---

## 6. Configuration and Tuning

### 6.1 Tunable Parameters

All parameters should be defined in `ekg_config.h`:

```c
// ekg_config.h

#ifndef EKG_CONFIG_H
#define EKG_CONFIG_H

// Sample rate
#define EKG_SAMPLE_RATE       100    // Hz

// Input constraints
#define EKG_MIN_SAMPLES       200    // 2 seconds
#define EKG_MAX_SAMPLES       1000   // 10 seconds
#define EKG_ANALYSIS_WINDOW   500    // 5 seconds (recommended)

// QRS Detection (Pan-Tompkins)
#define QRS_REFRACTORY_MS     200    // ms
#define QRS_REFRACTORY_SAMPLES (QRS_REFRACTORY_MS * EKG_SAMPLE_RATE / 1000)  // 20
#define QRS_INTEGRATION_MS    150    // ms
#define QRS_INTEGRATION_SAMPLES (QRS_INTEGRATION_MS * EKG_SAMPLE_RATE / 1000)  // 15
#define QRS_SEARCHBACK_MS     1660   // ms (corresponding to 36 bpm)
#define QRS_SEARCHBACK_SAMPLES (QRS_SEARCHBACK_MS * EKG_SAMPLE_RATE / 1000)  // 166
#define QRS_INIT_SPKI_FACTOR  0.25   // Signal peak initial estimate
#define QRS_INIT_NPKI_FACTOR  0.5    // Noise peak initial estimate
#define QRS_THRESHOLD_FACTOR  0.25   // Threshold = NPKI + factor*(SPKI-NPKI)
#define QRS_LEARNING_RATE     0.125  // Learning rate for adaptive thresholds

// VT Detection
#define VT_HR_THRESHOLD       120.0f  // bpm
#define VT_QRS_WIDTH_MS       120.0f  // ms
#define VT_QRS_WIDTH_SAMPLES  (VT_QRS_WIDTH_MS * EKG_SAMPLE_RATE / 1000.0f)  // 12
#define VT_REGULARITY_CV      20.0f   // percent (CV < 20% is regular)
#define VT_MIN_CONSECUTIVE    3       // Minimum consecutive wide QRS
#define VT_CONFIDENCE_THRESHOLD 0.7f  // Confidence threshold for VT

// VF Detection
#define VF_FREQ_MIN           4.0f    // Hz
#define VF_FREQ_MAX           10.0f   // Hz
#define VF_ZCR_THRESHOLD      0.17f   // Zero-crossing rate threshold
#define VF_MIN_AMPLITUDE      100.0f  // Minimum amplitude (not asystole)
#define VF_QRS_RATE_THRESHOLD 0.5f    // QRS per second (below = absent)
#define VF_CONFIDENCE_THRESHOLD 0.7f  // Confidence threshold for VF

// Goertzel frequencies for VF detection
#define VF_NUM_TEST_FREQS     7
static const float VF_TEST_FREQS[VF_NUM_TEST_FREQS] =
    {4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

// Bandpass filter coefficients (5-18 Hz at 100 Hz sample rate)
// High-pass 5 Hz
#define HP_B0  0.9150f
#define HP_B1 -1.8299f
#define HP_B2  0.9150f
#define HP_A1 -1.8227f
#define HP_A2  0.8372f

// Low-pass 18 Hz
#define LP_B0  0.2452f
#define LP_B1  0.4905f
#define LP_B2  0.2452f
#define LP_A1 -0.2097f
#define LP_A2  0.1906f

// Error codes
#define EKG_SUCCESS           0
#define EKG_ERROR_NULL_PTR   -1
#define EKG_ERROR_INVALID_LEN -2
#define EKG_ERROR_LOW_QUALITY -3
#define EKG_ERROR_SATURATED   -4

// Mathematical constants
#define PI 3.14159265358979323846f

#endif // EKG_CONFIG_H
```

### 6.2 Threshold Tuning Procedure

If detection performance is not meeting targets:

1. **Collect Test Data**:
   - Gather labeled EKG samples (normal, VT, VF)
   - Minimum 20 samples of each type

2. **Measure Current Performance**:
   - Run analyzer on all test samples
   - Calculate sensitivity, specificity, false positive rate

3. **Identify Issues**:
   - False Positives: Thresholds too low, increase confidence threshold
   - False Negatives: Thresholds too high, decrease confidence threshold
   - Check which specific criterion is causing misclassification

4. **Adjust Parameters**:

   **For VT Detection**:
   - If missing VT: Lower `VT_CONFIDENCE_THRESHOLD` or `VT_HR_THRESHOLD`
   - If false VT: Increase `VT_CONFIDENCE_THRESHOLD` or make QRS width detection stricter

   **For VF Detection**:
   - If missing VF: Lower `VF_CONFIDENCE_THRESHOLD` or widen frequency range
   - If false VF: Increase `VF_CONFIDENCE_THRESHOLD` or narrow frequency range

   **For QRS Detection**:
   - If missing QRS: Lower `QRS_THRESHOLD_FACTOR` or adjust `QRS_INIT_SPKI_FACTOR`
   - If false QRS: Increase refractory period or threshold factor

5. **Re-test and Iterate**:
   - Make small incremental changes (±0.05 for float, ±5 for int)
   - Re-measure performance after each change
   - Document which changes improve performance

6. **Validation**:
   - Once tuned, validate on independent test set
   - Ensure no overfitting to training data

---

## 7. Error Handling and Edge Cases

### 7.1 Input Validation

**Check in `ekg_analyze()`**:

1. **Null Pointer Check**:
   ```c
   if (samples == NULL || result == NULL) {
       return EKG_ERROR_NULL_PTR;
   }
   ```

2. **Sample Count Check**:
   ```c
   if (num_samples < EKG_MIN_SAMPLES || num_samples > EKG_MAX_SAMPLES) {
       return EKG_ERROR_INVALID_LEN;
   }
   ```

3. **Signal Saturation Check**:
   ```c
   int saturated = 0;
   for (int i = 0; i < num_samples; i++) {
       if (samples[i] == INT16_MIN || samples[i] == INT16_MAX) {
           saturated++;
       }
   }
   if (saturated > num_samples * 0.9) {
       return EKG_ERROR_SATURATED;
   }
   ```

4. **Signal Quality Check** (optional):
   ```c
   // Check if signal variance is too low (possible disconnect)
   float variance = calculate_variance(samples, num_samples);
   if (variance < 10.0) {
       return EKG_ERROR_LOW_QUALITY;
   }
   ```

### 7.2 Division by Zero Prevention

**Critical locations**:

1. **Heart Rate Calculation**:
   ```c
   if (num_intervals == 0 || mean_rr == 0) {
       return 0.0f;  // Cannot calculate HR
   }
   ```

2. **Coefficient of Variation**:
   ```c
   if (mean == 0.0f) {
       return 0.0f;  // Undefined CV
   }
   ```

3. **Goertzel Algorithm**:
   ```c
   if (num_samples == 0) {
       return 0.0f;
   }
   ```

### 7.3 Memory Allocation Failures

**Check all malloc() calls**:

```c
float *buffer = malloc(num_samples * sizeof(float));
if (buffer == NULL) {
    // Handle error: return or use static buffer
    return EKG_ERROR_MEMORY;
}

// ... use buffer ...

free(buffer);
```

**Alternative**: Use static buffers if dynamic allocation is not available:
```c
static float static_buffer[EKG_MAX_SAMPLES];
// Use static_buffer instead of malloc
```

### 7.4 Numerical Stability

1. **Prevent Overflow in Accumulations**:
   ```c
   // Use int32_t for sum of int16_t values
   int32_t sum = 0;
   for (int i = 0; i < num_samples; i++) {
       sum += samples[i];
   }
   ```

2. **Check for NaN/Inf**:
   ```c
   #include <math.h>

   if (isnan(value) || isinf(value)) {
       // Handle error
       return default_value;
   }
   ```

3. **Avoid Very Small Denominators**:
   ```c
   if (fabs(denominator) < 1e-6f) {
       return 0.0f;  // Or appropriate default
   }
   ```

---

## 8. Memory Layout

### 8.1 Static Memory (Global/Static Variables)

Minimize use of static variables. Use only for:
- Filter state (if needed across calls)
- Configuration constants (in .rodata)

**Estimated Static RAM**: <1 KB

### 8.2 Stack Memory

**Per-call stack usage** (estimated):

```
ekg_analyze() frame:
  - Local variables: ~100 bytes
  - filter_state[8]: 32 bytes

detect_qrs_complexes() frame:
  - Local variables: ~100 bytes

detect_vt() frame:
  - qrs_widths[50]: 200 bytes
  - Local variables: ~100 bytes

detect_vf() frame:
  - magnitudes[7]: 28 bytes
  - Local variables: ~100 bytes

Maximum stack depth: ~700 bytes
```

**Stack Requirement**: 2 KB (with safety margin)

### 8.3 Heap Memory (Dynamic Allocation)

**Per-call heap usage**:

```
ekg_analyze():
  - signal buffer: 1000 * 4 = 4000 bytes (max)
  - filtered buffer: 1000 * 4 = 4000 bytes (max)
  - qrs_result.locations: 50 * 2 = 100 bytes
  - qrs_result.rr_intervals: 50 * 2 = 100 bytes

detect_qrs_complexes():
  - derivative: 1000 * 4 = 4000 bytes (max)
  - squared: 1000 * 4 = 4000 bytes (max)
  - integrated: 1000 * 4 = 4000 bytes (max)

Total (worst case): ~20 KB during processing
Peak (at one instant): ~12 KB
```

**Optimization**: Reuse buffers to reduce peak memory:
```c
// Instead of separate derivative, squared, integrated buffers:
// Use a single working buffer and process in-place where possible
float *working_buffer = malloc(num_samples * sizeof(float));
// Process stages sequentially, reusing buffer
```

**Optimized Heap Usage**: <10 KB (meets requirement)

### 8.4 Flash Memory (Code)

**Estimated code size**:

```
signal_processing.c: ~2 KB
qrs_detector.c: ~4 KB
heart_rate.c: ~1 KB
vt_detector.c: ~2 KB
vf_detector.c: ~3 KB
ekg_analyzer.c: ~2 KB

Total: ~14 KB (well under 20 KB target)
```

---

## 9. Build Configuration

### 9.1 Makefile

```makefile
# Makefile for EKG Signal Analyzer

CC = arm-none-eabi-gcc
CFLAGS = -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
CFLAGS += -O2 -std=c99 -Wall -Wextra
CFLAGS += -Isrc

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET = $(BIN_DIR)/ekg_analyzer.elf

# Main target
all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ -lm

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Test target (compile for host)
test: CC = gcc
test: CFLAGS = -std=c99 -Wall -Wextra -Isrc -g
test:
	$(CC) $(CFLAGS) $(SRC_DIR)/*.c $(TEST_DIR)/test_integration.c -o $(BIN_DIR)/test_runner -lm
	./$(BIN_DIR)/test_runner

# Clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all test clean
```

### 9.2 Compilation Flags

**For Cortex M33**:
- `-mcpu=cortex-m33`: Target CPU
- `-mthumb`: Thumb instruction set
- `-mfloat-abi=hard`: Hardware floating-point
- `-mfpu=fpv5-sp-d16`: FPU type (single-precision)
- `-O2`: Optimization level (balance speed/size)
- `-std=c99`: C99 standard
- `-Wall -Wextra`: All warnings

**For Testing on Host**:
- Use `gcc` or `clang`
- Remove ARM-specific flags
- Keep `-std=c99 -Wall -Wextra`
- Add `-g` for debugging symbols

---

## 10. Usage Example

### 10.1 Basic Usage

```c
#include "ekg_analyzer.h"
#include <stdio.h>

int main(void) {
    // Sample EKG data (would come from ADC in real application)
    int16_t ekg_samples[500];

    // ... acquire samples from ADC ...

    // Analyze
    ekg_analysis_result_t result;
    int ret = ekg_analyze(ekg_samples, 500, &result);

    if (ret == EKG_SUCCESS) {
        if (result.shock_advised) {
            printf("SHOCK ADVISED!\n");
            printf("Rhythm: %s\n",
                   result.rhythm_type == 2 ? "VT" :
                   result.rhythm_type == 3 ? "VF" : "Unknown");
            printf("Confidence: %d%%\n", result.confidence);

            // Trigger defibrillator shock
            trigger_defibrillator();
        } else {
            printf("No shock advised\n");
            printf("Heart rate: %d bpm\n", result.heart_rate);
        }
    } else {
        printf("Analysis error: %d\n", ret);
    }

    return 0;
}
```

### 10.2 Continuous Monitoring

```c
#define BUFFER_SIZE 500
int16_t ekg_buffer[BUFFER_SIZE];
uint16_t buffer_index = 0;

// Called every 10ms (100 Hz)
void ADC_IRQHandler(void) {
    int16_t sample = read_adc();

    ekg_buffer[buffer_index++] = sample;

    // When buffer full, analyze
    if (buffer_index >= BUFFER_SIZE) {
        buffer_index = 0;  // Reset for next acquisition

        ekg_analysis_result_t result;
        if (ekg_analyze(ekg_buffer, BUFFER_SIZE, &result) == EKG_SUCCESS) {
            if (result.shock_advised) {
                signal_shock_needed();
            }
        }
    }
}
```

### 10.3 Overlapping Window Analysis

```c
#define WINDOW_SIZE 500
#define SLIDE_SIZE 100  // Slide by 1 second

int16_t ekg_buffer[WINDOW_SIZE];
uint16_t buffer_count = 0;

void process_new_sample(int16_t sample) {
    // Shift buffer (could use circular buffer for efficiency)
    memmove(ekg_buffer, ekg_buffer + 1, (WINDOW_SIZE - 1) * sizeof(int16_t));
    ekg_buffer[WINDOW_SIZE - 1] = sample;

    buffer_count++;

    // Analyze every SLIDE_SIZE samples
    if (buffer_count >= WINDOW_SIZE && (buffer_count % SLIDE_SIZE) == 0) {
        ekg_analysis_result_t result;
        ekg_analyze(ekg_buffer, WINDOW_SIZE, &result);

        // Process result...
    }
}
```

---

## 11. Known Limitations and Future Enhancements

### 11.1 Current Limitations

1. **Single-lead only**: More reliable detection with multi-lead analysis
2. **Fixed thresholds**: Not adaptive to individual patient characteristics
3. **Limited rhythm types**: Only VT and VF; doesn't detect all arrhythmias
4. **No artifact rejection**: Motion artifacts may cause false detections
5. **No trending**: Analyzes single window, not rhythm evolution over time
6. **Pediatric rhythms**: Designed for adults, may not work well for children

### 11.2 Potential Enhancements

1. **Multi-lead support**:
   - Add support for Lead I, Lead III, etc.
   - Cross-correlation between leads for confirmation

2. **Adaptive thresholds**:
   - Learn patient baseline during calibration period
   - Adjust QRS detection and width thresholds per patient

3. **Additional rhythm detection**:
   - Atrial fibrillation
   - Atrial flutter
   - Various heart blocks
   - Premature ventricular contractions (PVCs)

4. **Motion artifact detection**:
   - Accelerometer integration
   - Baseline wander analysis
   - Signal quality index

5. **Shock success prediction**:
   - AMSA (Amplitude Spectrum Area) calculation
   - Predict likelihood of successful defibrillation

6. **Machine learning integration**:
   - Train classifier on large database
   - Feature extraction and ML classification
   - Continuous learning from outcomes

7. **Historical analysis**:
   - Track rhythm changes over time
   - Detect deterioration trends
   - Store rhythm history

8. **Optimization**:
   - Fixed-point arithmetic (if FPU not available)
   - CMSIS-DSP library integration
   - SIMD instructions for vectorization

---

## 12. References and Resources

### 12.1 Algorithm References

1. **Pan, J., & Tompkins, W. J. (1985)**
   "A Real-Time QRS Detection Algorithm"
   *IEEE Transactions on Biomedical Engineering*, BME-32(3), 230-236

2. **Hamilton, P. S., & Tompkins, W. J. (1986)**
   "Quantitative Investigation of QRS Detection Rules"
   *IEEE Transactions on Biomedical Engineering*, BME-33(12)

3. **Amann, A., et al. (2005)**
   "Detecting Ventricular Fibrillation by Time-Delay Methods"
   *IEEE Transactions on Biomedical Engineering*, 52(2)

### 12.2 Medical Standards

1. **AHA/ACC/HRS (2017)**
   "Guideline for Management of Patients With Ventricular Arrhythmias"

2. **AHA (2007)**
   "Recommendations for the Standardization and Interpretation of the ECG"

### 12.3 Technical Resources

1. **ARM Cortex-M33 Technical Reference Manual**
   https://developer.arm.com/documentation/

2. **CMSIS-DSP Library**
   https://www.keil.com/pack/doc/CMSIS/DSP/html/index.html

3. **PhysioNet**
   https://physionet.org/
   - MIT-BIH Arrhythmia Database
   - CU Ventricular Tachyarrhythmia Database

4. **C99 Standard**
   ISO/IEC 9899:1999

---

**Document Version**: 1.0
**Date**: 2025-11-15
**Status**: Ready for Implementation

This implementation plan provides complete details for a coding AI agent to implement the EKG signal analyzer without additional clarification. All algorithms are specified with mathematical formulas, thresholds are provided, and the development process is broken down into manageable phases.
