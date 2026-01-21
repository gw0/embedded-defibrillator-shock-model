# Implementation Plan: Log-Odds Confidence Calculation

## Overview

Replace arbitrary additive confidence scoring with Bayesian log-odds calculation to produce calibrated probability estimates.

## Files to Modify

| File | Changes |
|------|---------|
| `src/config.h` | Add likelihood ratio constants |
| `src/vf_detector.c` | Replace additive scoring with log-odds |
| `src/vt_detector.c` | Replace additive scoring with log-odds |
| `src/ekg_analyzer.c` | Fix integer truncation, handle non-shockable confidence |
| `src/signal_utils.c` | Add signal quality estimation (new function) |
| `src/signal_utils.h` | Declare signal quality function |

## Implementation Steps

### Step 1: Add Log-Odds Helper and Constants to config.h

Add at end of config.h:

```c
/* Log-likelihood ratios for Bayesian confidence calculation */
/* LR+ when feature present, LR- when absent (in log space) */

/* VF detection log-LRs */
#define LR_VF_FREQ_BASIC_POS      1.10f   /* freq 3-12 Hz */
#define LR_VF_FREQ_BASIC_NEG     -1.61f
#define LR_VF_FREQ_OPTIMAL_POS    1.61f   /* freq 5-8 Hz */
#define LR_VF_FREQ_OPTIMAL_NEG   -0.69f
#define LR_VF_ZCR_HIGH_POS        0.92f
#define LR_VF_ZCR_HIGH_NEG       -0.92f
#define LR_VF_TCR_HIGH_POS        0.69f
#define LR_VF_TCR_HIGH_NEG       -0.69f
#define LR_VF_QRS_ABSENT_POS      1.39f
#define LR_VF_QRS_ABSENT_NEG     -2.30f
#define LR_VF_FINE_PENALTY       -0.50f   /* penalty for fine VF */

/* VT detection log-LRs */
#define LR_VT_HR_THRESH_POS       1.10f
#define LR_VT_HR_THRESH_NEG      -1.20f
#define LR_VT_HR_HIGH_POS         0.69f   /* HR > 150 */
#define LR_VT_HR_HIGH_NEG        -0.36f
#define LR_VT_QRS_WIDE_POS        1.39f
#define LR_VT_QRS_WIDE_NEG       -1.61f
#define LR_VT_QRS_VWIDE_POS       0.92f   /* QRS > 140ms */
#define LR_VT_QRS_VWIDE_NEG      -0.51f
#define LR_VT_REGULAR_POS         1.10f
#define LR_VT_REGULAR_NEG        -0.92f
#define LR_VT_VREGULAR_POS        0.69f   /* CV < 10% */
#define LR_VT_VREGULAR_NEG       -0.36f
#define LR_VT_SUSTAINED_POS       0.92f
#define LR_VT_SUSTAINED_NEG      -0.69f

/* Signal quality */
#define SNR_GOOD_THRESHOLD       10.0f
#define SIGNAL_QUALITY_MIN        0.5f
```

### Step 2: Add Signal Quality Function to signal_utils

Add to `signal_utils.h`:
```c
float estimate_signal_quality(const float *signal, size_t num_samples, float sample_rate);
```

Add to `signal_utils.c`:
```c
float estimate_signal_quality(const float *signal, size_t num_samples, float sample_rate) {
    if (num_samples < 10) return SIGNAL_QUALITY_MIN;

    /* Estimate signal power (variance) */
    float mean = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        mean += signal[i];
    }
    mean /= num_samples;

    float signal_var = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float diff = signal[i] - mean;
        signal_var += diff * diff;
    }
    signal_var /= num_samples;

    /* Estimate noise via high-frequency residual (simple differencing) */
    float noise_var = 0.0f;
    for (size_t i = 1; i < num_samples; i++) {
        float diff = signal[i] - signal[i-1];
        noise_var += diff * diff;
    }
    noise_var /= (2.0f * (num_samples - 1));  /* Factor of 2 for diff variance */

    if (noise_var < 1e-10f) return 1.0f;  /* Perfect signal */

    float snr = signal_var / noise_var;
    float quality = snr / SNR_GOOD_THRESHOLD;

    /* Clamp to [SIGNAL_QUALITY_MIN, 1.0] */
    if (quality < SIGNAL_QUALITY_MIN) quality = SIGNAL_QUALITY_MIN;
    if (quality > 1.0f) quality = 1.0f;

    return quality;
}
```

### Step 3: Add Log-Odds to Probability Helper

Add to `signal_utils.h`:
```c
float log_odds_to_probability(float log_odds);
```

Add to `signal_utils.c`:
```c
#include <math.h>

float log_odds_to_probability(float log_odds) {
    /* Clamp to prevent overflow */
    if (log_odds > 20.0f) return 1.0f;
    if (log_odds < -20.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-log_odds));
}
```

### Step 4: Update VF Detector Confidence Calculation

In `vf_detector.c`, replace lines 366-389 (standard VF confidence):

```c
/* Calculate confidence using log-odds */
float log_odds = 0.0f;  /* Prior: 50% */

/* Frequency criterion contribution */
if (dominant_freq >= 5.0f && dominant_freq <= 8.0f) {
    log_odds += LR_VF_FREQ_OPTIMAL_POS;
} else if (dominant_freq >= config->vf_freq_min && dominant_freq <= config->vf_freq_max) {
    log_odds += LR_VF_FREQ_BASIC_POS;
} else {
    log_odds += LR_VF_FREQ_BASIC_NEG;
}

/* ZCR contribution */
log_odds += (zcr > config->vf_zcr_threshold) ? LR_VF_ZCR_HIGH_POS : LR_VF_ZCR_HIGH_NEG;

/* TCR contribution */
log_odds += (tcr > config->vf_zcr_threshold * 2.0f) ? LR_VF_TCR_HIGH_POS : LR_VF_TCR_HIGH_NEG;

/* QRS absence contribution */
log_odds += (qrs_absent == 1) ? LR_VF_QRS_ABSENT_POS : LR_VF_QRS_ABSENT_NEG;

/* Convert to probability and scale by signal quality */
float signal_quality = estimate_signal_quality(signal, num_samples, sample_rate);
result->confidence = log_odds_to_probability(log_odds) * signal_quality;
```

Replace lines 430-437 (fine VF confidence):

```c
/* Fine VF: same calculation with penalty */
float log_odds = 0.0f;

/* Frequency contribution */
log_odds += (fine_vf_freq >= 5.0f && fine_vf_freq <= 8.0f) ? LR_VF_FREQ_OPTIMAL_POS : LR_VF_FREQ_BASIC_POS;

/* Fine VF penalty (lower certainty due to low amplitude) */
log_odds += LR_VF_FINE_PENALTY;

/* Convert to probability and scale by signal quality */
float signal_quality = estimate_signal_quality(signal, num_samples, sample_rate);
result->confidence = log_odds_to_probability(log_odds) * signal_quality;
```

### Step 5: Update VT Detector Confidence Calculation

In `vt_detector.c`, replace lines 215-253:

```c
/* Calculate confidence using log-odds */
float log_odds = 0.0f;  /* Prior: 50% */

/* Heart rate contribution */
log_odds += (heart_rate > config->vt_hr_threshold) ? LR_VT_HR_THRESH_POS : LR_VT_HR_THRESH_NEG;
log_odds += (heart_rate > 150.0f) ? LR_VT_HR_HIGH_POS : LR_VT_HR_HIGH_NEG;

/* QRS width contribution */
log_odds += (mean_qrs_width > config->vt_qrs_width_ms) ? LR_VT_QRS_WIDE_POS : LR_VT_QRS_WIDE_NEG;
log_odds += (mean_qrs_width > 140.0f) ? LR_VT_QRS_VWIDE_POS : LR_VT_QRS_VWIDE_NEG;

/* Regularity contribution */
log_odds += (rr_cv < config->vt_regularity_cv) ? LR_VT_REGULAR_POS : LR_VT_REGULAR_NEG;
log_odds += (rr_cv < 10.0f) ? LR_VT_VREGULAR_POS : LR_VT_VREGULAR_NEG;

/* Sustained duration contribution */
log_odds += (max_consecutive >= VT_MIN_CONSECUTIVE && duration_met) ? LR_VT_SUSTAINED_POS : LR_VT_SUSTAINED_NEG;

/* Convert to probability */
float confidence = log_odds_to_probability(log_odds);

/* VT detected if confidence >= threshold AND duration requirement met */
if (confidence >= config->vt_confidence_threshold && duration_met) {
    result->vt_detected = 1;
} else {
    result->vt_detected = 0;
}
result->confidence = confidence;
```

### Step 6: Fix Integer Conversion in ekg_analyzer.c

Replace truncation with rounding at lines 120, 129:

```c
/* Before: result->confidence = (uint8_t)(vf_result.confidence * 100.0f); */
/* After: */
result->confidence = (uint8_t)(vf_result.confidence * 100.0f + 0.5f);
```

For non-shockable rhythms (lines 147, 150), compute actual confidence:

```c
/* Instead of fixed 50, use inverse of shockable probability */
result->confidence = (uint8_t)((1.0f - max(vf_result.confidence, vt_result.confidence)) * 100.0f + 0.5f);
```

## Testing

### Unit Tests

1. Test `log_odds_to_probability`:
   - Input 0.0 → Output 0.5
   - Input large positive → Output ~1.0
   - Input large negative → Output ~0.0

2. Test `estimate_signal_quality`:
   - Clean sine wave → quality ~1.0
   - Noisy signal → quality < 1.0
   - Pure noise → quality = 0.5

### Integration Tests

Run existing test suite:
```bash
make test
```

### Validation

1. Run evaluation on test dataset:
```bash
python scripts/evaluate.py --config configs/default.json
```

2. Check that:
   - VF sensitivity remains >90%
   - VT sensitivity remains >75%
   - Specificity for non-shockable >95%
   - Confidence values appear calibrated (higher confidence = higher accuracy)

## Rollback Plan

If issues arise, revert changes to:
- `src/vf_detector.c`
- `src/vt_detector.c`
- `src/ekg_analyzer.c`

The new functions in `signal_utils.c` are additive and won't break existing functionality.

## Future Improvements

1. **Tune LRs from data**: Use training set to estimate optimal likelihood ratios
2. **Calibration validation**: Plot reliability diagrams to verify probability calibration
3. **Per-feature LR training**: Learn LRs specific to this device's signal characteristics
