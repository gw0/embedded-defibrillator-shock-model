# Plan: Measurement-Level Confidence for Detector Functions

## Summary

Refactor analysis functions to return **measurement confidence** - how certain each function is about its own computed result. This confidence reflects the reliability of the measurement itself, not its relevance to VF detection.

---

## Concept

Each detector function returns:
1. **The measurement value** (as before)
2. **Confidence in that measurement** [0, 1]

Example scenarios for `find_dominant_frequency()`:
- Clear single peak at 6.5 Hz → returns (6.5, **0.95**) - high confidence
- Two competing peaks at 6.5 Hz and 4.0 Hz → returns (6.5, **0.55**) - uncertain
- Flat spectrum, no clear peak → returns (0.0, **0.80**) - confident there's no dominant freq

This measurement confidence then informs the VF detection decision-making.

---

## Implementation Plan

### Step 1: Define result structures for each detector

```c
/* Frequency analysis result */
typedef struct {
    float frequency;     /* Dominant frequency in Hz (0 if none) */
    float confidence;    /* Confidence in this measurement [0, 1] */
} freq_result_t;

/* Entropy analysis result */
typedef struct {
    float entropy;       /* Spectral entropy [0, 1] */
    float confidence;    /* Confidence in measurement [0, 1] */
} entropy_result_t;

/* Crossing rate result */
typedef struct {
    float rate;          /* Crossings per second */
    float confidence;    /* Confidence in measurement [0, 1] */
} crossing_result_t;
```

### Step 2: Update `find_dominant_frequency()` to return confidence

Confidence factors:
- **Peak prominence**: How much does max peak exceed others?
- **Power concentration**: Is power concentrated or spread across spectrum?
- **VF band dominance**: Is VF band power > out-of-band power?

```c
freq_result_t find_dominant_frequency_with_confidence(
    const float *signal, uint16_t num_samples,
    uint16_t sample_rate, float dominance_thresh)
{
    freq_result_t result = {0.0f, 0.0f};

    /* Calculate magnitudes at all test frequencies */
    float magnitudes[VF_NUM_TEST_FREQS];
    float total_power = 0.0f;
    float max_mag = 0.0f, second_max = 0.0f;
    uint8_t max_idx = 0;

    for (uint8_t i = 0; i < VF_NUM_TEST_FREQS; i++) {
        magnitudes[i] = goertzel_magnitude(...);
        total_power += magnitudes[i];
        if (magnitudes[i] > max_mag) {
            second_max = max_mag;
            max_mag = magnitudes[i];
            max_idx = i;
        } else if (magnitudes[i] > second_max) {
            second_max = magnitudes[i];
        }
    }

    /* Confidence calculation */
    if (total_power > 0.0f && max_mag > 0.0f) {
        /* Factor 1: Peak prominence (max vs second) */
        float prominence = (max_mag - second_max) / max_mag;  /* 0-1 */

        /* Factor 2: Power concentration (max / total) */
        float concentration = max_mag / total_power;  /* 0-1 */

        /* Factor 3: VF band dominance vs out-of-band */
        float vf_ratio = total_power / (total_power + out_of_band + 0.001f);

        /* Combine factors */
        result.confidence = (prominence * 0.4f + concentration * 0.3f + vf_ratio * 0.3f);
        result.frequency = VF_TEST_FREQS[max_idx];
    }

    return result;
}
```

### Step 3: Update `calculate_spectral_entropy()` to return confidence

Confidence based on:
- **Sample count**: More samples = more reliable entropy estimate
- **Power level**: Very low power makes entropy unreliable

```c
entropy_result_t calculate_spectral_entropy_with_confidence(
    const float *signal, uint16_t num_samples, uint16_t sample_rate)
{
    entropy_result_t result = {0.0f, 0.0f};

    /* Existing entropy calculation... */
    float entropy = /* computed value */;

    /* Confidence factors */
    float sample_factor = fminf(1.0f, (float)num_samples / 500.0f);  /* 500+ = full confidence */
    float power_factor = (total_power > 100.0f) ? 1.0f : total_power / 100.0f;

    result.entropy = entropy;
    result.confidence = sample_factor * power_factor;

    return result;
}
```

### Step 4: Update `detect_vf()` - Unified Evidence & Confidence

Evidence and confidence unified into one calculation:

```c
/* Get measurements with confidence */
freq_result_t freq = find_dominant_frequency_with_confidence(...);
entropy_result_t ent = calculate_spectral_entropy_with_confidence(...);

/* Weighted evidence score - measurement confidence scales contribution */
float evidence_score = 0.0f;

/* Frequency contribution - weighted by measurement confidence */
if (freq.frequency >= 5.0f && freq.frequency <= 8.0f) {
    evidence_score += VF_EV_FREQ_OPTIMAL * freq.confidence;
} else if (freq.frequency >= config->vf_freq_min && freq.frequency <= config->vf_freq_max) {
    evidence_score += VF_EV_FREQ_GOOD * freq.confidence;
} else if (freq.frequency >= 2.5f && freq.frequency <= 12.0f) {
    evidence_score += VF_EV_FREQ_EDGE * freq.confidence;
}

/* Entropy contribution - weighted by measurement confidence */
if (ent.entropy > 0.6f) {
    evidence_score += VF_EV_ENTROPY_HIGH * ent.confidence;
} else if (ent.entropy > 0.4f) {
    evidence_score += VF_EV_ENTROPY_MOD * ent.confidence;
}

/* Irregularity, amplitude contributions similarly... */

/* Organized rhythm penalties */
if (is_organized_tachy) {
    evidence_score += VF_EV_ORGANIZED_PENALTY;  /* -3.0 */
}

/* Detection + confidence from single evidence score */
if (evidence_score >= config->vf_evidence_threshold && amplitude > adaptive_threshold) {
    result->vf_detected = 1;
    result->is_fine_vf = (amplitude <= config->vf_fine_amplitude_max) ? 1 : 0;

    /* Clinical confidence = sigmoid of margin above threshold */
    float margin = evidence_score - config->vf_evidence_threshold;
    result->confidence = 1.0f / (1.0f + expf(-margin));  /* [0, 1] range */

    /* Scale by signal quality */
    float signal_quality = estimate_signal_quality(signal, num_samples, (float)sample_rate);
    result->confidence *= signal_quality;

    return EKG_SUCCESS;
}
```

### Step 5: Remove dead code

Remove unused log-likelihood ratio constants from `ekg_config.h`:
- `LR_VF_FREQ_BASIC_POS`, `LR_VF_FREQ_BASIC_NEG`
- `LR_VF_FREQ_OPTIMAL_POS`, `LR_VF_FREQ_OPTIMAL_NEG`
- `LR_VF_ZCR_HIGH_POS`, `LR_VF_ZCR_HIGH_NEG`
- `LR_VF_TCR_HIGH_POS`, `LR_VF_TCR_HIGH_NEG`
- `LR_VF_QRS_ABSENT_POS`, `LR_VF_QRS_ABSENT_NEG`
- `LR_VF_FINE_PENALTY`
- `LR_VF_ENTROPY_HIGH_POS`, `LR_VF_ENTROPY_HIGH_NEG`
- All `LR_VT_*` constants (if not used elsewhere)

Also remove `log_odds_to_probability()` function from `signal_utils.c` if no longer needed.

---

## Files to Modify

| File | Change |
|------|--------|
| `src/vf_detector.h` | Add result structures (freq_result_t, entropy_result_t, crossing_result_t) |
| `src/vf_detector.c` | Update analysis functions to return measurement confidence |
| `src/vf_detector.c` | Update detect_vf() to use weighted evidence + sigmoid confidence |
| `src/ekg_config.h` | Remove unused LR_* constants |
| `src/signal_utils.c` | Remove log_odds_to_probability() if unused |

---

## Summary of Architecture Change

**Before (complex, redundant):**
```
features → boolean criteria → detection
features → log-odds sum → probability → confidence
```

**After (unified):**
```
features → measurement confidence
weighted evidence = Σ(feature_weight × measurement_confidence)
detection = evidence >= threshold
confidence = sigmoid(evidence - threshold)
```

---

## Benefits

1. **No redundancy** - Single evidence calculation serves both detection and confidence
2. **Principled uncertainty** - Measurement confidence propagates through system
3. **Simpler** - Remove parallel log-odds calculation and 20+ constants
4. **Correct confidence** - [0, 1] range via sigmoid

---

## Verification

```bash
make clean && make ekg_eval
./tools/ekg_eval data/
```

Verify:
- Detection metrics remain acceptable
- Confidence values span [0, 1] range
- Low-confidence measurements appropriately reduce overall confidence
