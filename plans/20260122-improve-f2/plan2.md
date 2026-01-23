# Simplified Plan: Improve VF Detection F2-Score

## Current Performance
- F2-Score: 68.6% | Sensitivity: 64.3% | Specificity: 95.4%
- Primary failure: 87.9% frequency detection issues (VF is spectrally flat, algorithm expects peaked)

## Key Findings from Code Review

1. **Sample duration (3-10s)**: Algorithm handles variable lengths. Low heart rate (2-3 beats) triggers "QRS absent" flag (+2.0 evidence). Hanning window handles beat misalignment.

2. **Double-gating is NOT what we thought**: `VF_CONFIDENCE_THRESHOLD` (0.95) is UNUSED/legacy. Real gates:
   - Evidence score ≥ 3.0
   - Amplitude > adaptive threshold
   - Final confidence = sigmoid(margin) × signal_quality
   - Measurement confidence already scales frequency/entropy/TCR contributions

3. **No separate Fine VF path exists**: Unified evidence scoring for all VF. "Fine VF" is just a post-detection label.

---

## Simplified Implementation Plan

### 1. Add Spectral Flatness to Evidence Scoring (HIGH IMPACT)

**File:** `src/vf_detector.c`

Add flatness calculation using existing Goertzel magnitudes:
```c
// Flatness = geometric_mean(mags) / arithmetic_mean(mags)
// VF: ~0.7-1.0 (chaotic) | Organized: ~0.1-0.4 (peaked)
```

Add to evidence scoring (lines ~540-563):
```c
if (flatness > 0.7f) evidence_score += VF_EV_FLATNESS_HIGH * flatness_confidence;
else if (flatness > 0.5f) evidence_score += VF_EV_FLATNESS_MOD * flatness_confidence;
```

**Expected:** +15-25% sensitivity

### 2. Lower Dominance Threshold

**File:** `src/ekg_config.h`
```c
#define VF_DOMINANCE_THRESHOLD  0.25f  // From 0.40f - VF is chaotic
```

**Expected:** +5-10% sensitivity

### 3. Softer Signal Quality Scaling

**File:** `src/vf_detector.c` (line ~584)
```c
result->confidence *= (0.5f + 0.5f * signal_quality);  // Range 0.5-1.0 instead of 0.0-1.0
```

**Expected:** +5-8% sensitivity

### 4. Increase Edge Frequency Evidence Weight

**File:** `src/ekg_config.h`
```c
#define VF_EV_FREQ_EDGE  1.0f  // From 0.5f - VF spans 2.5-12 Hz
```

**Expected:** +3-5% sensitivity

---

## NOT Doing (Per "Keep It Simple")

- ❌ Dedicated Fine VF path (complexity, already unified)
- ❌ Peak interpolation (marginal benefit)
- ❌ Expanded out-of-band measurement (marginal benefit)
- ❌ VF_CONFIDENCE_THRESHOLD changes (confirmed legacy/unused)

---

## Cleanup

- Remove unused `VF_CONFIDENCE_THRESHOLD` from `ekg_config.h` to reduce confusion

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/vf_detector.c` | Add spectral flatness calc (~30 lines), softer signal quality scaling |
| `src/ekg_config.h` | Add flatness thresholds/weights, lower dominance threshold, increase edge freq weight, remove unused VF_CONFIDENCE_THRESHOLD |
| `tools/ekg_train.c` | Add flatness parameters to training grid |

---

## Verification

```bash
make clean && make
./ekg_eval ./data/PROCESSEDINTEGER/
# Compare: sensitivity, specificity, F2-score, failure breakdown
```

---

## Target Metrics

- F2-Score: > 80% (from 68.6%)
- Sensitivity: > 85% (from 64.3%)
- Specificity: > 90% (from 95.4%, acceptable trade-off)
