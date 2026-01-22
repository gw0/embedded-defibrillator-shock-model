# Plan: Improve EKG Shock Algorithm F1 Score

## Problem Analysis

**Current Performance:**
- F1-Score: 75.8%
- Sensitivity: 64.3% (LOW - missing 35% of shockable rhythms)
- Specificity: 94.6% (HIGH - can trade some for sensitivity)

**Per-Rhythm Issues:**
| Rhythm | Metric | Current | Issue |
|--------|--------|---------|-------|
| VF Coarse | Sensitivity | 63.5% | Missing 845/2318 samples |
| VF Fine | Sensitivity | 66.5% | Missing 267/797 samples |
| Supraventricular | Specificity | 67.7% | 115/356 false positives |

**Root Cause:** Algorithm is too conservative. High specificity (94.6%) but low sensitivity means we have "headroom" to catch more true positives.

**Recent Code Change (commit 802bad3):** Confidence now uses Bayesian log-odds approach with likelihood ratios in `ekg_config.h` (lines 140-174). Confidence = `log_odds_to_probability(sum_of_LRs) * signal_quality`.

---

## Implementation Plan

### Phase 1: Tune Log-Likelihood Ratios for Sensitivity

**File:** `src/ekg_config.h` (lines 143-154)

The new Bayesian approach uses log-likelihood ratios (LRs). To improve sensitivity, increase positive LRs and decrease negative penalties:

1. **Increase VF frequency LRs:**
   - `LR_VF_FREQ_BASIC_POS`: 1.10 → 1.40 (boost basic freq match)
   - `LR_VF_FREQ_BASIC_NEG`: -1.61 → -1.20 (less penalty for edge freq)

2. **Reduce fine VF penalty:**
   - `LR_VF_FINE_PENALTY`: -0.50 → -0.20 (less harsh on fine VF)

3. **Boost QRS absence contribution:**
   - `LR_VF_QRS_ABSENT_POS`: 1.39 → 1.60 (stronger evidence when no QRS)

### Phase 2: Lower Detection Thresholds

**File:** `src/ekg_config.h`

1. **Lower VF confidence threshold** (line 50)
   - Change: `VF_CONFIDENCE_THRESHOLD` from 0.90 to 0.70
   - Rationale: Log-odds of ~0.85 maps to 70% probability

2. **Lower dominance threshold** (line 51)
   - Change: `VF_DOMINANCE_THRESHOLD` from 0.40 to 0.25
   - Rationale: VF is chaotic - no single frequency dominates

3. **Lower minimum amplitude** (line 48)
   - Change: `VF_MIN_AMPLITUDE` from 100.0 to 50.0
   - Rationale: Catch more low-amplitude VF

4. **Expand fine VF range** (line 52)
   - Change: `VF_FINE_AMPLITUDE_MAX` from 200.0 to 300.0
   - Rationale: Extend fine VF detection window

### Phase 3: Expand Frequency Range

**File:** `src/ekg_config.h`

1. **Extend VF frequency band** (lines 45-46, 57-62)
   - Change: `VF_FREQ_MIN` from 3.5 to 2.5
   - Change: `VF_FREQ_MAX` from 10.5 to 12.0
   - Add test frequencies: 2.5, 3.0, 11.0, 11.5, 12.0 Hz
   - Update `VF_NUM_TEST_FREQS` from 15 to 21

### Phase 4: Supraventricular Discrimination

**File:** `src/vf_detector.c`

Add function following codebase naming convention (`verb_noun`):
```c
uint8_t check_organized_tachycardia(const qrs_detection_result_t *qrs_result,
                                     uint16_t sample_rate)
```

Logic:
- SVT has regular QRS (CV < 15%)
- Heart rate 120-220 bpm
- If detected, do NOT classify as VF

Integration point: Call before returning VF positive result in `detect_vf()` (around line 363).

### Phase 5: Spectral Entropy Feature

**File:** `src/vf_detector.c`

Add spectral entropy calculation:
```c
float calculate_spectral_entropy(const float *signal, uint16_t num_samples,
                                 uint16_t sample_rate)
```

Logic:
- VF has high entropy (>0.65) - chaotic, broad spectrum
- NSR/SVT has low entropy (<0.50) - organized, narrow peaks
- Add as log-odds contribution: `LR_VF_ENTROPY_HIGH_POS/NEG`

### Phase 6: Training Improvements

**File:** `tools/ekg_train.c`

1. **Use F-beta score (beta=2) instead of F1** to prioritize sensitivity:
   ```c
   // F-beta formula: (1 + beta^2) * (precision * recall) / (beta^2 * precision + recall)
   // With beta=2, sensitivity is weighted 4x more than precision
   float beta = 2.0f;
   float beta_sq = beta * beta;
   float f_beta = (1 + beta_sq) * (precision * sensitivity) / (beta_sq * precision + sensitivity);
   ```
   This naturally encourages higher sensitivity without hard constraints.

2. **Expand grid ranges only for parameters at edge** (current defaults hit boundary):
   - `vf_conf`: 0.50-0.95 (was 0.50-0.90, default 0.90 at max edge)
   - `vf_freq_min`: 2.5-5.0 (was 3.5-5.0, default 3.5 at min edge)
   - `vf_zcr`: 0.05-0.25 (was 0.10-0.25, default 0.10 at min edge)

3. **Add LR tuning to parameter grid** - Include log-likelihood ratio parameters:
   - `LR_VF_FREQ_BASIC_POS`: 0.8-1.6 (step 0.2)
   - `LR_VF_FINE_PENALTY`: -0.8 to 0.0 (step 0.2)

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/ekg_config.h` | LR values (lines 143-154), thresholds (45-55), frequency array (57-62) |
| `src/vf_detector.c` | SVT discrimination, spectral entropy, integrate with log-odds |
| `src/vf_detector.h` | New function prototypes |
| `tools/ekg_train.c` | F-beta score, grid ranges, LR parameter tuning |

---

## Verification Plan

After each phase:
```bash
make clean && make host && make ekg_eval
./bin/ekg_eval data/20260120-ECG_individual/
```

**Expected Progression:**
| Phase | F1 | Sensitivity | Specificity |
|-------|-----|-------------|-------------|
| Current | 75.8% | 64.3% | 94.6% |
| Phase 1 (LRs) | ~78% | ~70% | ~92% |
| Phase 2 (Thresholds) | ~81% | ~74% | ~90% |
| Phase 3 (Freq) | ~82% | ~76% | ~90% |
| Phase 4 (SVT) | ~84% | ~78% | ~91% |
| Phase 5-6 | ~85% | ~80% | ~90% |

**Target:** F1 >= 85%, Sensitivity >= 80%, Specificity >= 90%
