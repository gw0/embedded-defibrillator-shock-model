# Plan: Improve VF Detection F2-Score

## Current Performance
| Metric | Value |
|--------|-------|
| F2-Score | 68.6% |
| Sensitivity | 64.3% |
| Specificity | 95.4% |
| False Negatives | 1112 (35.7% missed) |

## Root Cause Analysis

**Failure Breakdown:**
- **Frequency issues: 87.9%** (977 cases) - Primary problem
- **Confidence issues: 17.3%** (192 cases)

**Why frequency detection fails:**
1. Fixed 0.5 Hz resolution misses VF between test frequencies
2. Dominance threshold (0.40) too strict for chaotic VF with distributed power
3. VF is spectrally flat; current algorithm expects peaked spectrum

**Why confidence fails:**
1. 0.95 threshold is extremely strict
2. Signal quality multiplicatively halves confidence for noisy signals
3. Double-gating: both evidence AND confidence must pass

---

## Implementation Plan (Priority Order)

### 1. Add Spectral Flatness Measure (HIGH IMPACT)

**File:** `src/vf_detector.c`

Add new function to measure spectral flatness (ratio of geometric to arithmetic mean of power spectrum):
- VF produces flatness ~0.7-1.0 (uniform/chaotic)
- Organized rhythms produce flatness ~0.1-0.4 (peaked)

```c
typedef struct {
    float flatness;      /* [0, 1] */
    float confidence;
} flatness_result_t;

flatness_result_t calculate_spectral_flatness(const float *signal, uint16_t num_samples, uint16_t sample_rate);
```

Add to evidence scoring:
```c
if (flatness > 0.7f) evidence_score += 1.5f * confidence;
else if (flatness > 0.5f) evidence_score += 0.75f * confidence;
```

**Expected:** +15-25% sensitivity, -0-2% specificity

---

### 2. Lower VF Confidence Threshold

**File:** `src/ekg_config.h`

Change:
```c
#define VF_CONFIDENCE_THRESHOLD    0.95f  // Current - too strict
```
To:
```c
#define VF_CONFIDENCE_THRESHOLD    0.75f  // Proposed
```

**Expected:** +10-15% sensitivity, -2-5% specificity

---

### 3. Make Signal Quality Scaling Less Aggressive

**File:** `src/vf_detector.c` (line ~584)

Change multiplicative penalty:
```c
result->confidence *= signal_quality;  // Current: harsh penalty
```
To softer scaling:
```c
result->confidence *= (0.5f + 0.5f * signal_quality);  // Range 0.5-1.0
```

**Expected:** +5-8% sensitivity, -1-2% specificity

---

### 4. Lower Dominance Threshold

**File:** `src/ekg_config.h`

Change:
```c
#define VF_DOMINANCE_THRESHOLD     0.40f  // Current
```
To:
```c
#define VF_DOMINANCE_THRESHOLD     0.25f  // Proposed - VF is chaotic
```

**Expected:** +5-10% sensitivity, -1-3% specificity

---

### 5. Dedicated Fine VF Detection Path

**File:** `src/vf_detector.c`

Add explicit path for low-amplitude VF (< 250 mV):
- Lower evidence threshold (2.5 instead of 3.0)
- Require spectral flatness > 0.6
- Require QRS absent
- Use lower confidence requirement (0.60)

**Expected:** +5-10% sensitivity for fine VF, minimal specificity impact

---

### 6. Increase Edge Frequency Evidence Weights

**File:** `src/ekg_config.h`

Change:
```c
#define VF_EV_FREQ_EDGE           0.5f   // Current - too low
```
To:
```c
#define VF_EV_FREQ_EDGE           1.0f   // VF spans 2.5-12 Hz range
```

**Expected:** +3-5% sensitivity, -1-2% specificity

---

### 7. Add Frequency Peak Interpolation

**File:** `src/vf_detector.c` in `find_dominant_frequency()`

Add parabolic interpolation between adjacent bins:
```c
float delta = 0.5f * (y_m1 - y_p1) / (y_m1 - 2.0f * y_0 + y_p1);
result.frequency = VF_TEST_FREQS[max_idx] + delta * 0.5f;
```

**Expected:** +2-5% sensitivity, neutral specificity

---

### 8. Improve Out-of-Band Power Measurement

**File:** `src/vf_detector.c` in `find_dominant_frequency()`

Expand from 2 frequencies to full range:
- Low band: 0.5-2.0 Hz (4 frequencies)
- High band: 15-25 Hz (5 frequencies)

**Expected:** +2-3% sensitivity, +1-2% specificity

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/vf_detector.c` | Add spectral flatness, improve confidence calc, fine VF path, peak interpolation, out-of-band measurement |
| `src/vf_detector.h` | Add flatness_result_t struct, new function declarations |
| `src/ekg_config.h` | Add flatness thresholds, lower confidence/dominance thresholds, adjust evidence weights |
| `tools/ekg_train.c` | Add new parameters to training grid |

---

## Verification Plan

1. **Build and run existing tests:**
   ```bash
   make clean && make
   make test
   ```

2. **Run evaluation on dataset:**
   ```bash
   ./ekg_eval ./data/PROCESSEDINTEGER/
   ```

3. **Compare metrics before/after each change:**
   - Track sensitivity, specificity, F2-score
   - Monitor per-rhythm breakdown (especially VF Fine)
   - Check failure breakdown (frequency vs confidence)

4. **Run parameter optimization after adding spectral flatness:**
   ```bash
   ./ekg_train --strategy adaptive --num-combinations 10000 ./data/PROCESSEDINTEGER/
   ```

5. **Target metrics:**
   - F2-Score: > 80% (from 68.6%)
   - Sensitivity: > 85% (from 64.3%)
   - Specificity: > 90% (from 95.4%, acceptable trade-off)

---

## Literature Support

- Spectral flatness for VF detection: [MDPI Review 2024](https://www.mdpi.com/2076-3417/14/23/11167)
- Wearable device achieving 99.61% sensitivity: [PMC Study](https://pmc.ncbi.nlm.nih.gov/articles/PMC5047284/)
- Fine VF detection challenges: [Resuscitation Journal](https://www.resuscitationjournal.com/article/S0300-9572(24)00798-6/fulltext)
- Spectral methods achieving F1 > 0.97: [BioMedical Engineering OnLine](https://link.springer.com/article/10.1186/1475-925X-4-60)
