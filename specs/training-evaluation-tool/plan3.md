# Confidence-Based Optimization for EKG Training Tool

## Problem Analysis

### Current Situation
The rolling-window implementation works, but optimization is ineffective because:

**Problem 1: Binary Aggregation Has No Gradient**
- Any-positive strategy: if ANY window advises shock → file = SHOCK
- Result: Once triggered, no signal to improve further
- Example: Config A (1 window @ 60% confidence) = Config B (8 windows @ 95% confidence) = both SHOCK

**Problem 2: Confidence Is Ignored in Training**
- `ekg_train.c` only uses `result.shock_advised` (binary 0/1)
- `result.confidence` is calculated but never tracked or used
- Optimizer cannot see how confidence thresholds affect quality

**Problem 3: Poor Specificity Despite High Thresholds**
Current best config shows:
- VT/VF confidence thresholds: 0.90 (very high)
- Sensitivity: 100% (perfect)
- Specificity: 7.1% (terrible - 39 FP out of 42 negatives)
- **Conclusion**: Confidence scoring in VT/VF detectors is poorly calibrated

### Root Cause
The confidence values from VT/VF detectors frequently exceed 0.90 even for non-shockable rhythms, indicating the scoring functions need better discrimination.

---

## Solution: Confidence-Based Optimization Metric

### User Requirements (from questions)
- **Metric**: Confidence-based scoring
- **Aggregation**: Average confidence across windows
- **Goal**: Provide continuous gradient for optimization

### Proposed Metric: Average Confidence Score

**Scoring Function**:
```
For each file:
  1. Analyze all windows
  2. Track shock-advised windows and their confidence
  3. Calculate average confidence of shock-advised windows

  If file is SHOCK (ground truth):
    score += avg_confidence / 100.0  (reward high confidence)

  If file is NO-SHOCK (ground truth):
    score += 1.0 - (avg_confidence / 100.0)  (reward low/no confidence)

Overall metric: average score across all files (0.0 to 1.0, higher is better)
```

**Benefits**:
- Continuous gradient for optimization
- Rewards strong confident detections on SHOCK files
- Penalizes confident false positives on NO-SHOCK files
- Still uses binary shock_advised for classification
- Backward compatible with existing analyzer

---

## Implementation Plan

### Step 1: Add Confidence Tracking to metrics_t

**File**: `tools/ekg_train.c` (lines 48-58)

**Current Structure**:
```c
typedef struct {
    uint32_t tp, tn, fp, fn;
    float accuracy;
    float sensitivity;
    float specificity;
    float precision;
    float f1_score;
    uint32_t rhythm_confusion[5][5];
} metrics_t;
```

**New Structure**:
```c
typedef struct {
    uint32_t tp, tn, fp, fn;
    float accuracy;
    float sensitivity;
    float specificity;
    float precision;
    float f1_score;
    uint32_t rhythm_confusion[5][5];

    /* NEW: Confidence-based metrics */
    float total_confidence_score;  /* Sum of confidence scores */
    uint32_t num_files_evaluated;  /* Number of files processed */
    float confidence_score;        /* Average confidence score (0-1) */
} metrics_t;
```

### Step 2: Modify evaluate_config() to Track Confidence

**File**: `tools/ekg_train.c` (lines 420-500)

**Changes to the rolling-window loop**:

```c
/* Evaluate each file */
for (uint32_t i = 0; i < num_files; i++) {
    // ... load file ...

    /* Rolling-window analysis with confidence tracking */
    uint8_t file_shock_advised = 0;
    float sum_confidence = 0.0f;
    int shock_window_count = 0;

    for (int w = 0; w < num_windows; w++) {
        // ... analyze window ...

        if (ret == EKG_SUCCESS) {
            /* Track all shock-advised windows */
            if (result.shock_advised) {
                file_shock_advised = 1;
                sum_confidence += result.confidence;  // 0-100 range
                shock_window_count++;
            }
        }
    }

    /* Calculate average confidence for this file */
    float avg_confidence = (shock_window_count > 0)
        ? (sum_confidence / shock_window_count)
        : 0.0f;

    /* Confidence-based scoring */
    float file_score;
    if (file->ground_truth_shock) {
        /* SHOCK file: reward high confidence detections */
        file_score = avg_confidence / 100.0f;
    } else {
        /* NO-SHOCK file: penalize high confidence (false positives) */
        file_score = 1.0f - (avg_confidence / 100.0f);
    }

    metrics->total_confidence_score += file_score;
    metrics->num_files_evaluated++;

    /* Still update binary confusion matrix for validation */
    if (file_shock_advised && file->ground_truth_shock) {
        metrics->tp++;
    } else if (!file_shock_advised && !file->ground_truth_shock) {
        metrics->tn++;
    } else if (file_shock_advised && !file->ground_truth_shock) {
        metrics->fp++;
    } else if (!file_shock_advised && file->ground_truth_shock) {
        metrics->fn++;
    }
}
```

### Step 3: Calculate Confidence Score in calculate_metrics()

**File**: `tools/ekg_train.c` (lines 377-418)

**Add to calculate_metrics()**:
```c
static void calculate_metrics(metrics_t *metrics)
{
    // ... existing binary metrics calculation ...

    /* Calculate average confidence score */
    if (metrics->num_files_evaluated > 0) {
        metrics->confidence_score = metrics->total_confidence_score /
                                   metrics->num_files_evaluated;
    } else {
        metrics->confidence_score = 0.0f;
    }
}
```

### Step 4: Use Confidence Score for Optimization

**File**: `tools/ekg_train.c` (search functions: lines 617-793)

**Change optimization comparison** in all three search functions:

**Before**:
```c
if (metrics.f1_score > progress->best_metrics.f1_score) {
    progress->best_metrics = metrics;
    progress->best_config = config;
}
```

**After**:
```c
/* Use confidence_score as primary metric */
if (metrics.confidence_score > progress->best_metrics.confidence_score) {
    progress->best_metrics = metrics;
    progress->best_config = config;
}
```

This changes:
- `search_random()` (line 650)
- `search_exhaustive()` (line 692)
- Adaptive search uses these internally

### Step 5: Update Progress Display

**File**: `tools/ekg_train.c` (line 850-857)

**Change print_progress()**:
```c
static void print_progress(const progress_t *progress)
{
    float percent = 100.0f * progress->completed / progress->total_combinations;

    printf("Progress: %lu/%lu (%.1f%%) - Best Score=%.3f (F1=%.3f)\n",
           progress->completed, progress->total_combinations, percent,
           progress->best_metrics.confidence_score,
           progress->best_metrics.f1_score);
}
```

### Step 6: Update Output Display

**File**: `tools/ekg_train.c` (lines 490-501)

**Add to print_metrics()**:
```c
static void print_metrics(const metrics_t *metrics)
{
    printf("  Confidence Score: %.1f%%\n", metrics->confidence_score * 100.0f);
    printf("  Accuracy:    %.1f%%\n", metrics->accuracy * 100.0f);
    printf("  Sensitivity: %.1f%%\n", metrics->sensitivity * 100.0f);
    printf("  Specificity: %.1f%%\n", metrics->specificity * 100.0f);
    printf("  Precision:   %.1f%%\n", metrics->precision * 100.0f);
    printf("  F1-Score:    %.1f%%\n", metrics->f1_score * 100.0f);
    printf("  Confusion: TP=%u TN=%u FP=%u FN=%u\n",
           metrics->tp, metrics->tn, metrics->fp, metrics->fn);
}
```

---

## Expected Improvements

### Before (Binary Any-Positive)
- No gradient signal once any window triggers
- Optimizer blind to confidence values
- Best F1: 33.9%, Sensitivity: 100%, Specificity: 7.1%

### After (Confidence-Based)
- Continuous optimization signal
- Rewards configs with high-confidence correct detections
- Penalizes configs with high-confidence false positives
- Expected improvements:
  - Better specificity (fewer false positives)
  - More balanced sensitivity/specificity trade-off
  - Higher overall quality scores

---

## Files to Modify

1. **tools/ekg_train.c**
   - Line 48-58: Add confidence fields to `metrics_t`
   - Line 420-500: Modify `evaluate_config()` to track confidence
   - Line 377-418: Update `calculate_metrics()`
   - Line 617-793: Change optimization comparisons in search functions
   - Line 850-857: Update `print_progress()`
   - Line 490-501: Update `print_metrics()`

   **Total changes**: ~60-80 lines added/modified

---

## Testing Plan

### Baseline Comparison
Run with same dataset before/after:

**Before** (current binary):
```bash
./bin/ekg_train -n 100 data/20251129-ECGrecording/PROCESSEDINTEGER/
```

**After** (confidence-based):
```bash
./bin/ekg_train -n 100 data/20251129-ECGrecording/PROCESSEDINTEGER/
```

### Success Criteria
1. Confidence score shows in progress/output
2. Optimization produces different (better) configs
3. Specificity improves significantly (target: >50%)
4. F1-score improves or stays comparable
5. Sensitivity remains high (target: >80%)

### Validation Tests
1. **All-shock file**: Should score ~1.0 if high-confidence detections
2. **All-no-shock file**: Should score ~1.0 if no/low-confidence detections
3. **Mixed file**: Score should reflect balance

---

## Alternative: Fix VT/VF Confidence Calibration

If confidence-based optimization still shows poor results, the problem may be in the VT/VF detector confidence calculations themselves:

**VT Confidence Issues** (vt_detector.c, lines 208-253):
- Scoring is additive (0.0-1.0) but may not discriminate well
- Consider recalibrating weights or adding more criteria

**VF Confidence Issues** (vf_detector.c, lines 260-285):
- Base confidence 0.5 + bonuses may be too generous
- Consider stricter criteria or different weighting

**This is out of scope for current implementation** but worth investigating if confidence-based optimization doesn't improve results.

---

## Summary

This plan adds confidence-based scoring to the training loop, providing a continuous optimization signal instead of binary classification. The implementation is straightforward (~60-80 lines), maintains backward compatibility, and should significantly improve the optimizer's ability to find good parameter configurations.

Key insight: **The optimizer can only optimize what it can measure**. By making confidence visible to the optimization loop, we enable the algorithm to find configs that produce high-confidence correct predictions and low-confidence false positives.
