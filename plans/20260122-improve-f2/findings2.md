# VF Detection Improvement Findings - Round 2

## Baseline Performance
- F2-Score: 68.6% | Sensitivity: 64.3% | Specificity: 95.4%
- VF Coarse Sensitivity: ~63.5%
- VF Fine Sensitivity: ~66.5%

## Current Performance (after changes)
- F2-Score: 68.4% | Sensitivity: 64.3% | Specificity: 94.2%
- Essentially unchanged from baseline

---

## Changes Implemented

### 1. Removed `VF_CONFIDENCE_THRESHOLD` from Config
- **Files:** `src/ekg_config.h`, `src/ekg_analyzer.c`, `tools/ekg_train.c`, `tools/eval_utils.c`
- **Reason:** Redundant with evidence-based scoring
- **Result:** Replaced with hardcoded 0.75 threshold in `ekg_analyzer.c`

### 2. Added Spectral Flatness Infrastructure
- **Files:** `src/vf_detector.h`, `src/vf_detector.c`, `src/ekg_config.h`, `tools/ekg_train.c`
- **Function:** `calculate_spectral_flatness()` - computes geometric_mean/arithmetic_mean of Goertzel magnitudes
- **Status:** DISABLED in evidence scoring (causes too many false positives)
- **Config params added:** `vf_flatness_high_thresh`, `vf_flatness_mod_thresh`
- **Evidence weights defined:** `VF_EV_FLATNESS_HIGH` (2.0), `VF_EV_FLATNESS_MOD` (1.0)

---

## Experiments Conducted

### Confidence Threshold Tuning
| Threshold | F2-Score | Sensitivity | Specificity | NSR Spec |
|-----------|----------|-------------|-------------|----------|
| 0.70      | 68.3%    | 64.3%       | 93.8%       | 99.7%    |
| 0.75      | 68.3%    | 64.3%       | 93.9%       | 99.7%    |
| 0.80      | 68.4%    | 64.3%       | 94.6%       | 99.8%    |

**Finding:** Confidence threshold affects specificity slightly but NOT sensitivity. Sensitivity stuck at 64.3%.

### Evidence Threshold Tuning
| Threshold | Result |
|-----------|--------|
| 3.0       | Baseline - works |
| 2.5       | No change in sensitivity |
| 2.0       | TEST FAILURE - NSR triggers VF (amplitude alone = 2.0 points) |

**Finding:** Can't lower evidence threshold below 3.0 without breaking NSR specificity.

### Dominance Threshold Tuning
| Threshold | Result |
|-----------|--------|
| 0.40      | Baseline |
| 0.30      | No change |

**Finding:** Dominance threshold has no measurable effect on current dataset.

### Flatness Feature (Full Enable)
| Config | F2-Score | Sensitivity | Specificity | NSR Spec |
|--------|----------|-------------|-------------|----------|
| Flatness enabled (0.7/0.5 thresholds, 2.0/1.0 weights) | 65.4% | 70.1% | 35.2% | 28.3% |
| Flatness enabled + all improvements | 65.7% | 66.3% | 62.5% | 43.0% |
| Conservative flatness (0.8/0.6, 1.5/0.5) | 65.1% | 66.9% | 53.7% | 28.8% |

**Finding:** Flatness DESTROYS specificity on NSR (drops from 99.7% to ~28-43%). The feature triggers on organized rhythms.

### Softer Signal Quality Scaling
| Config | F2-Score | Sensitivity | Specificity | NSR Spec |
|--------|----------|-------------|-------------|----------|
| Original (0.0-1.0 range) | 68.3% | 64.3% | 93.8% | 99.7% |
| Softer (0.5-1.0 range) | 66.7% | 64.3% | 82.7% | 73.7% |

**Finding:** Softer scaling degrades specificity without improving sensitivity.

### Edge Frequency Weight
| Weight | Result |
|--------|--------|
| 0.5    | Baseline |
| 1.0    | Degrades when combined with other changes |

**Finding:** Not tested in isolation, but contributes to FP increase.

---

## Key Insights

### Why Sensitivity is Stuck at 64.3%
1. ~36% of VF samples don't accumulate enough evidence (score < 3.0)
2. OR they accumulate evidence but confidence gets scaled down by signal quality
3. The confidence threshold is NOT the bottleneck - lowering it doesn't help

### Why Flatness Failed
1. NSR and SVT have higher-than-expected spectral flatness
2. The Goertzel frequencies (2.5-12 Hz) may not capture organized rhythm peaks well
3. QRS complexes have broadband energy that spreads across the spectrum

### Evidence Scoring Constraint
- High amplitude alone = 2.0 points (VF_EV_AMP_HIGH)
- Evidence threshold must be > 2.0 to prevent amplitude-only detections
- Current threshold of 3.0 requires at least 2 positive features

---

## NOT Yet Tried

### 1. Flatness Weight Tuning
- Try much lower weights: VF_EV_FLATNESS_HIGH = 0.5, VF_EV_FLATNESS_MOD = 0.25
- This would add small positive evidence for VF without overwhelming the scoring

### 2. Flatness Threshold Tuning (Very High)
- Try VF_FLATNESS_HIGH_THRESH = 0.9 (only truly chaotic signals)
- Try VF_FLATNESS_MOD_THRESH = 0.8

### 3. Flatness as Tie-Breaker Only
- Only use flatness when other evidence is borderline (score 2.5-3.5)

### 4. Analyze Failing VF Samples
- What evidence are the 36% missing VF samples lacking?
- Are they low amplitude? Missing frequency? High QRS detection?

### 5. Increase Negative Penalties
- VF_EV_QRS_PRESENT_PENALTY currently -3.0
- Could increase to -4.0 to suppress more FPs

---

## Recommended Next Steps

1. **Try flatness with very low weights (0.5/0.25)** - add slight positive evidence without overwhelming
2. **Analyze failing VF samples** - understand what evidence they're missing
3. **Consider training optimization** - let grid search find optimal flatness thresholds/weights

---

## Current Code State

```c
// ekg_analyzer.c:120 - Hardcoded confidence threshold
if (vf_result.vf_detected == 1 && vf_result.confidence >= 0.75f) {

// vf_detector.c:646-648 - Flatness DISABLED
/* Spectral flatness - DISABLED, causes too many false positives on NSR/SVT */
/* Infrastructure kept for future optimization */
(void)flat;

// ekg_config.h - Flatness infrastructure present but unused
#define VF_FLATNESS_HIGH_THRESH   0.7f
#define VF_FLATNESS_MOD_THRESH    0.5f
#define VF_EV_FLATNESS_HIGH       2.0f
#define VF_EV_FLATNESS_MOD        1.0f
```
