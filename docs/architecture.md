# Architecture

## Signal Processing Pipeline

```
Input: int16_t samples[300-1000] @ 100 Hz
    ↓
[DC Offset Removal] → Remove baseline wander
    ↓
[Bandpass Filter 5-18 Hz] → Butterworth filters (high-pass 5 Hz, low-pass 18 Hz)
    ↓
[Pan-Tompkins QRS Detection] → Identify ventricular depolarizations
    ├─ Derivative filter (5-point)
    ├─ Squaring (emphasize high frequencies)
    ├─ Moving window integration (150ms)
    └─ Adaptive threshold peak detection
         ↓
    QRS locations + R-R intervals
         ↓
    ┌────┴─────┬──────────────────┐
    ↓          ↓                  ↓
[Heart Rate] [VT Detector]  [VF Detector]
    ↓          ↓                  ↓
    └──────────┴──────────────────┘
                 ↓
    [Decision Logic] → shock_advised, rhythm_type, confidence
```

## Core Components

### Signal Processing (`signal_processing.c/h`)
- DC offset removal (mean subtraction)
- 2nd-order Butterworth bandpass filter (5-18 Hz)
- Moving average filter
- Filter state size: 8 coefficients

### QRS Detection (`qrs_detector.c/h`)
- Pan-Tompkins algorithm (industry standard real-time QRS detection)
- Adaptive thresholding with learning rate 0.125
- Searchback capability for missed beats (1660ms window)
- Refractory period: 120ms (allows detection up to 250 bpm)
- Returns: QRS locations array (max 50 detections)

### Heart Rate Analysis (`heart_rate.c/h`)
- Calculates BPM from R-R intervals
- Regularity metric: Coefficient of Variation (CV%)
- CV < 15% indicates regular rhythm

### VT Detection (`vt_detector.c/h`)
- Heart rate threshold: 140 bpm (default)
- QRS width threshold: 110ms (wide QRS indicates ventricular origin)
- Regularity check: CV < 15%
- Confidence threshold: 0.80

### VF Detection (`vf_detector.c/h`)
- Goertzel algorithm for efficient single-frequency DFT
- Tests 7 frequencies: 4, 5, 6, 7, 8, 9, 10 Hz
- VFib typically peaks at 5-9 Hz (default range)
- Zero-crossing rate threshold: 0.10
- Minimum amplitude check: 100.0 (distinguish from asystole)
- Confidence threshold: 0.90

### Main Analyzer (`ekg_analyzer.c/h`)
- Main API: `ekg_analyze(samples, num_samples, result, config)`
- Returns: shock_advised, rhythm_type (normal/VT/VF/other), confidence, heart_rate
- Configuration system: Pass NULL for defaults or custom `ekg_config_t` struct

## Configuration System

All tunable parameters are centralized in `src/ekg_config.h`:
- Compile-time constants (e.g., `VT_HR_THRESHOLD`, `VF_FREQ_MIN`)
- Runtime configuration via `ekg_config_t` struct
- Pass NULL to `ekg_analyze()` to use `EKG_DEFAULT_CONFIG`
- Pass custom config for parameter tuning/optimization

This design allows algorithm optimization without recompilation.
