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
- Filter uses Direct Form II transposed implementation for numerical stability
- Filter state layout: slots 0-1 for high-pass state, slots 4-5 for low-pass state
- Filter state size: 8 coefficients

### QRS Detection (`qrs_detector.c/h`)
- Pan-Tompkins algorithm (industry standard real-time QRS detection)
- Two-level adaptive thresholding (THRESHOLD_I1 and THRESHOLD_I2) with learning rate 0.125
- Searchback capability for missed beats (1660ms window) - if no peak found for 1.66× RR interval, searches back to find missed beats
- Minimum signal energy threshold to avoid detecting noise/asystole - prevents false positives in flat-line signals
- Refractory period: 120ms (allows detection up to 250 bpm)
- Returns: QRS locations array (max 50 detections)

### Heart Rate Analysis (`heart_rate.c/h`)
- Calculates BPM from R-R intervals
- Outlier filtering using median ± 30% tolerance to exclude ectopic beats and noise artifacts
- Sorted intervals with qsort for robust median calculation
- Regularity metric: Coefficient of Variation (CV%)
- CV < 15% indicates regular rhythm

### VT Detection (`vt_detector.c/h`)

**Detection Criteria:**
- Heart rate threshold: 140 bpm (default)
- QRS width threshold: 110ms (wide QRS indicates ventricular origin)
- Regularity check: CV < 15%

**Bayesian Confidence Calculation:**
Uses log-likelihood ratios accumulated from multiple evidence sources:
- Heart rate evidence (threshold and high threshold at 180 bpm)
- QRS width evidence (wide >110ms and very wide >140ms)
- RR regularity evidence (regular <15% CV and very regular <10% CV)
- Sustained duration evidence

**QRS Width Measurement:**
- Derivative-based method searching for onset/end points
- Searches backward from R peak for onset (slope reversal)
- Searches forward from R peak for end (return to baseline)
- Returns width in milliseconds

**Duration Requirement:**
- Consecutive wide QRS complexes must span at least 2 seconds minimum
- Prevents single ectopic beats from being classified as VT

### VF Detection (`vf_detector.c/h`)

VF detection uses a sophisticated multi-feature evidence scoring system to distinguish VF from other rhythms.

**Goertzel Frequency Analysis:**
- 20 frequencies at 0.5 Hz resolution spanning 2.5-12 Hz
- Hanning window applied to reduce spectral leakage
- VFib typically peaks at 5-9 Hz

**Evidence Scoring System:**
The detector accumulates weighted evidence from multiple features:

1. **Amplitude Scoring** (tiered thresholds):
   - High amplitude: strong positive evidence
   - Medium amplitude: moderate positive evidence
   - Low amplitude: weak positive evidence
   - Very low amplitude: minimal evidence (near asystole)

2. **Frequency Scoring** (dominant frequency location):
   - Optimal range (5-8 Hz): maximum evidence
   - Good range (3-10 Hz): moderate evidence
   - Edge range (2.5-12 Hz): weak evidence
   - Outside range: negative evidence

3. **Irregularity Scoring** (chaos indicators):
   - QRS absence: VF lacks organized QRS complexes
   - Zero-crossing rate (ZCR): chaotic signals have high ZCR
   - Threshold crossing rate (TCR): measures signal activity

4. **Spectral Entropy** (Shannon entropy):
   - High entropy indicates chaotic, disorganized VF
   - Low entropy indicates organized rhythm (not VF)

5. **Spectral Flatness** (geometric/arithmetic mean ratio):
   - Key discriminator between VF and organized rhythms
   - VF has flatter spectrum (more uniform frequency content)
   - Organized rhythms have peaked spectra

6. **Organized Rhythm Penalties** (negative evidence):
   - `check_organized_tachycardia()` detects regular fast rhythms
   - Prevents SVT and regular VT from being classified as VF
   - Applies negative evidence for regular RR intervals

**Fine VF Detection:**
Separate detection path for low-amplitude VF:
- Uses adaptive thresholds based on signal amplitude
- Prevents missing fine VF that may appear similar to asystole
- Applied when coarse VF detection is uncertain

**Duration Requirement:**
- VF pattern must persist for minimum 3 seconds
- Prevents transient artifacts from triggering shock

**Confidence Calculation:**
- Sigmoid function of evidence margin above threshold
- Maps continuous evidence score to 0-100% confidence

### Signal Utilities (`signal_utils.c/h`)
- Signal quality assessment helpers
- Noise floor estimation for adaptive thresholds
- Bayesian confidence calculation utilities
- Log-likelihood ratio computation for evidence accumulation

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
