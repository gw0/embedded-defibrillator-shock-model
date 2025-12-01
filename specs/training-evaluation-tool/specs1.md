# Training & Evaluation Tool - Requirements Specification

## 1. System Overview

### 1.1 Purpose
This document specifies the requirements for an automated parameter optimization and evaluation tool that uses the EKG signal dataset to find optimal algorithm parameters through grid search optimization. The tool will systematically test parameter combinations, evaluate performance using cross-validation, and report comprehensive metrics including confusion matrices for both binary (SHOCK/NOSHOCK) and per-rhythm-type classification.

### 1.2 Scope
- **Target Platform**: Linux/Unix systems (development/testing environment)
- **Programming Language**: C (C99 standard) with optional OpenMP for parallelization
- **Application Type**: Command-line utility with batch processing
- **Dataset**: 273 labeled CSV files (67 SHOCK, 206 NOSHOCK)
- **Optimization Approach**: Automated grid search with multiple strategies
- **Output**: Performance metrics, confusion matrices, optimal parameters
- **Regulatory Status**: Educational/prototype (NOT a medical device)

### 1.3 Safety Notice
This implementation is intended for educational and prototype purposes only. It is NOT certified for medical use and does NOT comply with IEC 60601 or other medical device standards. Do not use this system for actual patient care.

**Medical Safety Emphasis**: In cardiac arrest scenarios, false negatives (missed shockable rhythms) are more dangerous than false positives. The tool prioritizes sensitivity (recall) over specificity when optimizing parameters.

---

## 2. Dataset Specifications

### 2.1 Dataset Structure

The tool SHALL expect the following directory structure:

```
data/20251129-ECGrecording/PROCESSEDINTEGER/
├── SHOCK/                          (67 files - shockable rhythms)
│   ├── VFibCoarse.integer.csv
│   ├── VFibFine.integer.csv
│   ├── VFib2Coarse.integer.csv
│   ├── VFib2Fine.integer.csv
│   ├── PolyVTachType1.integer.csv
│   ├── PolyVTachType2.integer.csv
│   ├── PolyVTachType3.integer.csv
│   ├── PolyVTachType4.integer.csv
│   ├── PolyVTachType5.integer.csv
│   └── [other shockable rhythms]
└── NOSHOCK/                        (206 files - non-shockable rhythms)
    ├── Normal100.integer.csv
    ├── Normal150.integer.csv
    ├── asystole.integer.csv
    ├── AFib*.integer.csv
    ├── AFlutter*.integer.csv
    └── [other non-shockable rhythms]
```

### 2.2 Ground Truth Labels

**Binary Classification (SHOCK/NOSHOCK)**:
- Ground truth derived from directory structure
- Files in `SHOCK/` directory → ground_truth = 1 (shockable)
- Files in `NOSHOCK/` directory → ground_truth = 0 (non-shockable)

**Per-Rhythm-Type Classification**:
Ground truth rhythm type derived from filename:

| Filename Pattern | Ground Truth Rhythm | Notes |
|-----------------|---------------------|-------|
| VFib*.csv, VFib2*.csv | VF (Ventricular Fibrillation) | Include coarse and fine variants |
| PolyVTach*.csv | VT (Polymorphic VT) | Types 1-5 |
| MonoVTach*.csv (fast) | VT (Monomorphic VT) | HR >150 bpm |
| Normal*.csv | Normal (Sinus Rhythm) | Various heart rates |
| asystole*.csv | Other (Asystole) | Flatline |
| AFib*.csv | Other (Atrial Fibrillation) | Fine/coarse variants |
| AFlutter*.csv | Other (Atrial Flutter) | Regular atrial rhythm |
| [others] | Other | All other arrhythmias |

**Rhythm Type Encoding:**
- 0 = Unknown (not used in ground truth)
- 1 = Normal Sinus Rhythm
- 2 = VT (Ventricular Tachycardia)
- 3 = VF (Ventricular Fibrillation)
- 4 = Other Arrhythmias

### 2.3 Dataset Loading

The tool SHALL implement the following dataset loading process:

**Step 1: Directory Scanning**
```c
/* Recursively scan directories */
- Find all *.integer.csv files in SHOCK/
- Find all *.integer.csv files in NOSHOCK/
- Build file list with ground truth labels
```

**Step 2: File Information Extraction**
For each file:
- Store full path
- Extract binary label (SHOCK=1, NOSHOCK=0)
- Parse filename for rhythm type
- Validate file exists and is readable

**Step 3: Dataset Split**
- Stratified K-fold cross-validation (default K=10)
- Or simple train/test split (80/20)
- Maintain class balance in each fold

### 2.4 Data Statistics

| Category | Count | Percentage | Description |
|----------|-------|------------|-------------|
| Total Files | 273 | 100% | Complete labeled dataset |
| SHOCK | 67 | 24.5% | Shockable rhythms (VF, fast VT) |
| NOSHOCK | 206 | 75.5% | Non-shockable rhythms |
| VF | ~20 | 7.3% | Ventricular fibrillation variants |
| VT | ~25 | 9.2% | Ventricular tachycardia |
| Normal | ~80 | 29.3% | Normal sinus rhythms |
| Other | ~148 | 54.2% | AFib, Flutter, Asystole, PVCs, etc. |

**Class Imbalance Note**: Dataset is imbalanced (24.5% positive class). Stratified sampling is critical for reliable evaluation.

---

## 3. Parameter Grid Search

### 3.1 Tunable Parameters

The tool SHALL support optimization of the following parameters from `ekg_config.h`:

#### 3.1.1 High Priority Parameters (8 parameters)

| Parameter | Description | Default | Min | Max | Step | Count |
|-----------|-------------|---------|-----|-----|------|-------|
| `VT_HR_THRESHOLD` | VT heart rate threshold (bpm) | 120 | 100 | 140 | 5 | 9 |
| `VT_QRS_WIDTH_MS` | VT QRS width threshold (ms) | 120 | 100 | 140 | 10 | 5 |
| `VT_REGULARITY_CV` | VT regularity coefficient of variation (%) | 20 | 15 | 30 | 5 | 4 |
| `VT_CONFIDENCE_THRESHOLD` | VT detection confidence threshold | 0.7 | 0.5 | 0.9 | 0.1 | 5 |
| `VF_FREQ_MIN` | VF minimum frequency (Hz) | 4.0 | 3.5 | 5.0 | 0.5 | 4 |
| `VF_FREQ_MAX` | VF maximum frequency (Hz) | 10.0 | 9.0 | 12.0 | 0.5 | 7 |
| `VF_ZCR_THRESHOLD` | VF zero-crossing rate threshold | 0.17 | 0.10 | 0.25 | 0.05 | 4 |
| `VF_CONFIDENCE_THRESHOLD` | VF detection confidence threshold | 0.7 | 0.5 | 0.9 | 0.1 | 5 |

**Full Grid Size**: 9 × 5 × 4 × 5 × 4 × 7 × 4 × 5 = **504,000 combinations**

#### 3.1.2 Secondary Parameters (Optional Fine-Tuning)

| Parameter | Description | Default | Min | Max | Step |
|-----------|-------------|---------|-----|-----|------|
| `VF_MIN_AMPLITUDE` | VF minimum amplitude threshold | 100 | 50 | 200 | 25 |
| `QRS_THRESHOLD_FACTOR` | QRS detection threshold factor | 0.25 | 0.15 | 0.35 | 0.05 |

### 3.2 Search Strategies

The tool SHALL implement three search strategies:

#### 3.2.1 Strategy 1: Exhaustive Grid Search

**Description**: Test all parameter combinations in the grid

**Advantages**:
- Guaranteed to find global optimum within grid
- Comprehensive coverage

**Disadvantages**:
- Computationally expensive (504K combinations)
- Runtime: ~40-200 hours single-threaded

**Use Case**: Small grids only (when grid size < 5,000 combinations)

#### 3.2.2 Strategy 2: Random Search

**Description**: Randomly sample N combinations from the parameter space

**Algorithm**:
```
FOR i = 1 to N:
    params = random_sample_from_grid()
    score = evaluate(params)
    IF score > best_score:
        best_score = score
        best_params = params
```

**Advantages**:
- Much faster (typical N = 500-2000)
- Often finds near-optimal solutions
- Runtime: ~30-120 minutes

**Disadvantages**:
- May miss global optimum
- Results vary between runs

**Use Case**: Quick exploration of parameter space

#### 3.2.3 Strategy 3: Adaptive Search (Recommended)

**Description**: Coarse grid search followed by fine-grained refinement

**Algorithm**:
```
PHASE 1: Coarse Search
- Reduce grid resolution (e.g., double step sizes)
- Test all combinations (~2,000 combinations)
- Find top K best parameter sets (K=5)

PHASE 2: Fine Search
- For each top-K parameter set:
    - Create fine grid around it (±2 steps)
    - Test all combinations (~500 per set)
- Return overall best

PHASE 3: Validation
- Validate best parameters on held-out test set
```

**Advantages**:
- Balances speed and accuracy
- Typically finds near-optimal parameters
- Runtime: ~1-4 hours

**Disadvantages**:
- Slightly more complex implementation

**Use Case**: Default recommended strategy

### 3.3 Grid Configuration

The tool SHALL support command-line configuration of search space:

**Global Grid Specification:**
```bash
./ekg_train --dataset data/PROCESSEDINTEGER/ --strategy adaptive
```

**Parameter Override (Narrow Search):**
```bash
./ekg_train --dataset data/PROCESSEDINTEGER/ \
    --vt-hr-min 115 --vt-hr-max 130 --vt-hr-step 5 \
    --vf-conf-min 0.6 --vf-conf-max 0.8 --vf-conf-step 0.05
```

**Dry Run (Show Grid Size):**
```bash
./ekg_train --dataset data/PROCESSEDINTEGER/ --dry-run
```

Output:
```
Grid Configuration:
  VT_HR_THRESHOLD: 100 to 140 step 5 (9 values)
  VT_QRS_WIDTH_MS: 100 to 140 step 10 (5 values)
  ...
  VF_CONFIDENCE_THRESHOLD: 0.5 to 0.9 step 0.1 (5 values)

Total Combinations: 504,000
Estimated Runtime: 42-168 hours (single-threaded)
                   5-21 hours (8 threads)

Recommendation: Use --strategy adaptive or --strategy random
```

---

## 4. Evaluation Methodology

### 4.1 Cross-Validation Strategy

**Default: Stratified K-Fold Cross-Validation (K=19)**

**Algorithm**:
```
1. Shuffle dataset with fixed random seed (reproducibility)
2. Split into K folds maintaining class balance
3. For each parameter combination:
   a. For each fold i (i = 1 to K):
      - Train set: All folds except i
      - Test set: Fold i
      - Evaluate on test set
   b. Average metrics across K folds
   c. Record mean and standard deviation
4. Select best parameters based on primary metric (F1-score)
```

**Stratification**: Ensure each fold has approximately same proportion of SHOCK/NOSHOCK

**Alternative: Simple Split (80/20)**
- 80% training set (218 files)
- 20% test set (55 files)
- Stratified to maintain class balance
- Faster evaluation, less robust

### 4.2 Single Parameter Set Evaluation

For a given parameter configuration, evaluate as follows:

**Step 1: Set Parameters**
```c
/* Modify analyzer configuration */
config.VT_HR_THRESHOLD = params[0];
config.VT_QRS_WIDTH_MS = params[1];
/* ... etc ... */
```

**Step 2: Process All Files**
```c
int TP = 0, TN = 0, FP = 0, FN = 0;
int confusion_matrix[5][5] = {0};  /* Per-rhythm-type */

for each file in dataset:
    /* Load and analyze */
    load_csv(file, &samples, &num_samples);
    ekg_analyze(samples, num_samples, &result);

    /* Compare to ground truth */
    predicted = result.shock_advised;
    actual = file.ground_truth_shock;

    /* Update binary confusion matrix */
    if (predicted == 1 && actual == 1) TP++;
    else if (predicted == 0 && actual == 0) TN++;
    else if (predicted == 1 && actual == 0) FP++;
    else if (predicted == 0 && actual == 1) FN++;

    /* Update per-rhythm confusion matrix */
    actual_rhythm = file.ground_truth_rhythm;
    predicted_rhythm = result.rhythm_type;
    confusion_matrix[actual_rhythm][predicted_rhythm]++;
```

**Step 3: Calculate Metrics**
```c
accuracy = (TP + TN) / (TP + TN + FP + FN);
sensitivity = TP / (TP + FN);  /* Recall */
specificity = TN / (TN + FP);
precision = TP / (TP + FP);
f1_score = 2 * (precision * sensitivity) / (precision + sensitivity);
```

### 4.3 Overfitting Detection

The tool SHALL detect and report potential overfitting:

**Check 1: Training vs Validation Gap**
- If validation_accuracy < training_accuracy - 10%:
  - Flag as potential overfitting
  - Warn user

**Check 2: Extreme Parameters**
- If any parameter is at min/max boundary:
  - Flag as potential grid boundary issue
  - Suggest expanding grid range

**Check 3: Cross-Validation Variance**
- Calculate standard deviation across K folds
- If std_dev > 10%:
  - Flag as high variance
  - Suggest more data or simpler model

### 4.4 Performance Requirements

| Operation | Target Time | Notes |
|-----------|-------------|-------|
| Single file load & analyze | <50 ms | Average across dataset |
| Full dataset evaluation | <15 seconds | 273 files × 50 ms |
| Grid search (adaptive) | 1-4 hours | Depends on grid size |
| Grid search (random, N=1000) | 30-60 minutes | 1000 × 15 sec |

---

## 5. Performance Metrics

### 5.1 Binary Classification Metrics (SHOCK vs NOSHOCK)

The tool SHALL calculate the following standard metrics:

#### 5.1.1 Accuracy
```
Accuracy = (TP + TN) / (TP + TN + FP + FN)
```
- **Interpretation**: Overall correctness
- **Range**: 0.0 to 1.0 (or 0% to 100%)
- **Target**: >90%

#### 5.1.2 Sensitivity (Recall)
```
Sensitivity = TP / (TP + FN)
```
- **Interpretation**: Proportion of actual shockable rhythms correctly identified
- **Clinical Significance**: **CRITICAL** - missed shockable rhythms are life-threatening
- **Range**: 0.0 to 1.0
- **Target**: >95% (medical devices: >97%)

#### 5.1.3 Specificity
```
Specificity = TN / (TN + FP)
```
- **Interpretation**: Proportion of actual non-shockable rhythms correctly identified
- **Clinical Significance**: Important but less critical than sensitivity
- **Range**: 0.0 to 1.0
- **Target**: >90%

#### 5.1.4 Precision (Positive Predictive Value)
```
Precision = TP / (TP + FP)
```
- **Interpretation**: When shock is advised, how often is it correct?
- **Clinical Significance**: Unnecessary shocks cause patient harm
- **Range**: 0.0 to 1.0
- **Target**: >85%

#### 5.1.5 F1-Score
```
F1 = 2 × (Precision × Sensitivity) / (Precision + Sensitivity)
```
- **Interpretation**: Harmonic mean of precision and sensitivity
- **Use**: Primary optimization metric (balances both concerns)
- **Range**: 0.0 to 1.0
- **Target**: >90%

**Optimization Priority**: The tool SHALL primarily optimize F1-score, with sensitivity weighted higher than specificity (e.g., F-beta score with beta=1.5).

### 5.2 Confusion Matrix (2×2, SHOCK/NOSHOCK)

```
                    Predicted
                 SHOCK    NOSHOCK
Actual  SHOCK     TP        FN
        NOSHOCK   FP        TN
```

**Where**:
- **TP (True Positive)**: Shock advised, ground truth = SHOCK (correct shock)
- **TN (True Negative)**: No shock advised, ground truth = NOSHOCK (correct rejection)
- **FP (False Positive)**: Shock advised, ground truth = NOSHOCK (unnecessary shock)
- **FN (False Negative)**: No shock advised, ground truth = SHOCK (**missed shockable - DANGEROUS**)

**Display Format**:
```
Confusion Matrix (SHOCK/NOSHOCK):
                 Predicted
              SHOCK   NOSHOCK
Actual SHOCK    62       5      (Sensitivity: 92.5%)
       NOSHOCK  12     194      (Specificity: 94.2%)
              (PPV: 83.8%)
```

### 5.3 Per-Rhythm-Type Confusion Matrix (5×5)

```
                      Predicted
                Normal  VT   VF   Other  Unknown
Actual  Normal    85    2    0     5       8
        VT         3   20    2     0       0
        VF         0    5   15     0       0
        Other      8    0    0   130      10
        Unknown    0    0    0     0       0
```

**Rhythm Type Categories**:
- **Normal**: Normal sinus rhythm (various heart rates)
- **VT**: Ventricular tachycardia (mono + polymorphic)
- **VF**: Ventricular fibrillation (coarse + fine)
- **Other**: All other rhythms (AFib, Flutter, Asystole, PVCs, etc.)
- **Unknown**: Ambiguous/unclassifiable (typically analyzer uncertainty)

**Per-Class Metrics**:
For each rhythm type, calculate:
- **Sensitivity**: TP / (TP + FN) for that class
- **Specificity**: TN / (TN + FP) for that class (one-vs-rest)
- **Precision**: TP / (TP + FP) for that class

**Display Format**:
```
Per-Rhythm-Type Performance:
  Normal: Sensitivity=85.0%, Precision=88.5%
  VT:     Sensitivity=80.0%, Precision=74.1%
  VF:     Sensitivity=75.0%, Precision=88.2%
  Other:  Sensitivity=87.8%, Precision=96.3%
```

### 5.4 Aggregate Statistics

For cross-validation, report mean and standard deviation across folds:

```
Cross-Validation Results (K=5):
  Accuracy:     92.3% ± 2.1%
  Sensitivity:  94.5% ± 3.2%
  Specificity:  91.6% ± 2.8%
  Precision:    86.7% ± 4.1%
  F1-Score:     90.4% ± 2.5%
```

### 5.5 Additional Metrics (Optional)

- **ROC-AUC**: Area under ROC curve (if confidence scores used as threshold)
- **Balanced Accuracy**: (Sensitivity + Specificity) / 2 (handles class imbalance)
- **Matthews Correlation Coefficient**: Correlation between predictions and ground truth

---

## 6. Output Specifications

### 6.1 Progress Output (to stdout during execution)

```
========================================
EKG Training & Evaluation Tool
========================================
Dataset: data/20251129-ECGrecording/PROCESSEDINTEGER/
Files loaded: 273 (67 SHOCK, 206 NOSHOCK)
Strategy: Adaptive Grid Search
Cross-Validation: 10-fold stratified

Grid Configuration:
  VT_HR_THRESHOLD: 100-140 step 5 (9 values)
  VT_QRS_WIDTH_MS: 100-140 step 10 (5 values)
  VT_REGULARITY_CV: 15-30 step 5 (4 values)
  VT_CONFIDENCE_THRESHOLD: 0.5-0.9 step 0.1 (5 values)
  VF_FREQ_MIN: 3.5-5.0 step 0.5 (4 values)
  VF_FREQ_MAX: 9.0-12.0 step 0.5 (7 values)
  VF_ZCR_THRESHOLD: 0.10-0.25 step 0.05 (4 values)
  VF_CONFIDENCE_THRESHOLD: 0.5-0.9 step 0.1 (5 values)

Phase 1: Coarse Grid Search
  Combinations to test: 2,016
  Estimated time: 8.4 hours (1 thread) or 1.1 hours (8 threads)

Progress: [████████████--------] 60% (1210/2016) ETA: 24m
Current best: F1=0.912, Sensitivity=0.945, Specificity=0.901
  Parameters: VT_HR=125, VT_QRS=120, VT_REG=20, VT_CONF=0.7,
              VF_MIN=4.0, VF_MAX=10.0, VF_ZCR=0.17, VF_CONF=0.7

...

Phase 1 complete: Top 5 parameter sets identified

Phase 2: Fine Grid Search around top performers
  Combinations to test: 500 per set (2,500 total)

...

Optimization complete!
Total time: 1h 42m
```

### 6.2 Final Results Output (to stdout)

```
========================================
OPTIMIZATION RESULTS
========================================

BEST PARAMETERS FOUND:
  VT_HR_THRESHOLD:         125 bpm
  VT_QRS_WIDTH_MS:         120 ms
  VT_REGULARITY_CV:        20 %
  VT_CONFIDENCE_THRESHOLD: 0.70
  VF_FREQ_MIN:             4.0 Hz
  VF_FREQ_MAX:             10.0 Hz
  VF_ZCR_THRESHOLD:        0.17
  VF_CONFIDENCE_THRESHOLD: 0.70

PERFORMANCE METRICS (10-fold cross-validation):
  Accuracy:     94.2% ± 1.8%
  Sensitivity:  96.3% ± 2.1%  ← CRITICAL (detect shockable)
  Specificity:  93.1% ± 2.3%
  Precision:    87.5% ± 3.2%
  F1-Score:     91.7% ± 2.0%

CONFUSION MATRIX (SHOCK/NOSHOCK):
                 Predicted
              SHOCK   NOSHOCK
Actual SHOCK    65       2      (Sensitivity: 96.3%)
       NOSHOCK  14     192      (Specificity: 93.2%)
              (PPV: 82.3%)

PER-RHYTHM-TYPE CONFUSION MATRIX:
                      Predicted
                Normal  VT   VF   Other  Unknown
Actual  Normal    78    1    0     3       3
        VT         2   22    1     0       0
        VF         0    3   17     0       0
        Other      5    0    0   138       5

PER-RHYTHM-TYPE PERFORMANCE:
  Normal: Sensitivity=91.8%, Precision=91.8%, F1=91.8%
  VT:     Sensitivity=88.0%, Precision=84.6%, F1=86.3%
  VF:     Sensitivity=85.0%, Precision=94.4%, F1=89.5%
  Other:  Sensitivity=93.2%, Precision=97.9%, F1=95.5%

VALIDATION CHECKS:
  ✓ Sensitivity >90% requirement met
  ✓ Specificity >85% requirement met
  ✓ No overfitting detected (train/val gap <5%)
  ✓ Cross-validation variance acceptable (<5%)

MISCLASSIFIED FILES (False Negatives):
  1. SHOCK/PolyVTachType4.integer.csv (predicted: NOSHOCK, confidence: 55%)
  2. SHOCK/VFibFine.integer.csv (predicted: NOSHOCK, confidence: 62%)

RECOMMENDATIONS:
  - Overall performance meets targets for educational use
  - 2 false negatives detected (review these cases manually)
  - VF fine patterns may benefit from lower amplitude threshold
  - Consider testing with additional VF fine variants

Results saved to: results.csv, best_config.h, report.txt

========================================
```

### 6.3 CSV Results File (results.csv)

One row per parameter combination tested, all metrics recorded:

```csv
vt_hr,vt_qrs,vt_reg,vt_conf,vf_min,vf_max,vf_zcr,vf_conf,accuracy,sensitivity,specificity,precision,f1,tp,tn,fp,fn
100,100,15,0.5,3.5,9.0,0.10,0.5,0.878,0.925,0.859,0.782,0.847,62,177,29,5
100,100,15,0.5,3.5,9.0,0.10,0.6,0.885,0.910,0.873,0.798,0.850,61,180,26,6
...
125,120,20,0.7,4.0,10.0,0.17,0.7,0.942,0.963,0.931,0.875,0.917,65,192,14,2
...
```

**Columns**:
- 8 parameter values
- 5 main metrics (accuracy, sensitivity, specificity, precision, F1)
- 4 confusion matrix values (TP, TN, FP, FN)

**Usage**: Import to Excel, Python pandas, or R for visualization and further analysis

### 6.4 Best Configuration Header (best_config.h)

Generated C header file with optimal parameters:

```c
/**
 * Optimized EKG Configuration Parameters
 *
 * Generated by ekg_train on 2025-11-29
 * Performance: F1=91.7%, Sensitivity=96.3%, Specificity=93.1%
 * Dataset: 273 files (10-fold cross-validation)
 */

#ifndef EKG_OPTIMIZED_CONFIG_H
#define EKG_OPTIMIZED_CONFIG_H

/* VT Detection Parameters */
#define VT_HR_THRESHOLD       125.0f  /* bpm */
#define VT_QRS_WIDTH_MS       120.0f  /* ms */
#define VT_REGULARITY_CV      20.0f   /* percent */
#define VT_CONFIDENCE_THRESHOLD 0.7f

/* VF Detection Parameters */
#define VF_FREQ_MIN           4.0f    /* Hz */
#define VF_FREQ_MAX           10.0f   /* Hz */
#define VF_ZCR_THRESHOLD      0.17f
#define VF_CONFIDENCE_THRESHOLD 0.7f

/* To use these parameters, include this file instead of ekg_config.h
   or use #define USE_OPTIMIZED_CONFIG before including ekg_config.h */

#endif /* EKG_OPTIMIZED_CONFIG_H */
```

### 6.5 Detailed Report (report.txt)

Comprehensive markdown report with all details:

```markdown
# EKG Analyzer Training Report

## Executive Summary
Automated parameter optimization completed on 2025-11-29.
Dataset: 273 labeled EKG recordings (67 shockable, 206 non-shockable).
Strategy: Adaptive grid search with 10-fold cross-validation.
Best F1-Score: 91.7% (Sensitivity: 96.3%, Specificity: 93.1%)

## Dataset Composition
- Total files: 273
- SHOCK: 67 (24.5%)
  - VF variants: 20
  - Polymorphic VT: 25
  - Fast monomorphic VT: 22
- NOSHOCK: 206 (75.5%)
  - Normal rhythms: 85
  - Asystole: 8
  - AFib/Flutter: 45
  - Other: 68

## Optimization Process
... [full details] ...

## Recommendations
... [detailed recommendations] ...
```

---

## 7. Command-Line Interface

### 7.1 Usage Syntax

```
ekg_train [OPTIONS]
```

### 7.2 Required Arguments

| Argument | Long Form | Description |
|----------|-----------|-------------|
| `-d <path>` | `--dataset <path>` | Path to dataset root directory |

### 7.3 Optional Arguments

| Flag | Long Form | Argument | Default | Description |
|------|-----------|----------|---------|-------------|
| `-s` | `--strategy` | `<type>` | adaptive | Search strategy: exhaustive, random, adaptive |
| `-n` | `--num-samples` | `<N>` | 1000 | Number of samples (for random search) |
| `-k` | `--kfold` | `<K>` | 5 | K for K-fold cross-validation |
| `-o` | `--output` | `<file>` | results.csv | Output CSV file path |
| `-p` | `--parallel` | `<N>` | 1 | Number of parallel threads |
| | `--dry-run` | None | N/A | Show grid configuration and exit |
| `-h` | `--help` | None | N/A | Display help message |

### 7.4 Parameter Override Flags

For each tunable parameter, provide min/max/step override:

**VT Parameters:**
- `--vt-hr-min <bpm>`, `--vt-hr-max <bpm>`, `--vt-hr-step <bpm>`
- `--vt-qrs-min <ms>`, `--vt-qrs-max <ms>`, `--vt-qrs-step <ms>`
- `--vt-reg-min <%>`, `--vt-reg-max <%>`, `--vt-reg-step <%>`
- `--vt-conf-min <0-1>`, `--vt-conf-max <0-1>`, `--vt-conf-step <0-1>`

**VF Parameters:**
- `--vf-fmin-min <Hz>`, `--vf-fmin-max <Hz>`, `--vf-fmin-step <Hz>`
- `--vf-fmax-min <Hz>`, `--vf-fmax-max <Hz>`, `--vf-fmax-step <Hz>`
- `--vf-zcr-min <0-1>`, `--vf-zcr-max <0-1>`, `--vf-zcr-step <0-1>`
- `--vf-conf-min <0-1>`, `--vf-conf-max <0-1>`, `--vf-conf-step <0-1>`

### 7.5 Example Usage

**Example 1: Basic training with default settings**
```bash
./ekg_train --dataset data/20251129-ECGrecording/PROCESSEDINTEGER/
```

**Example 2: Random search with 500 samples**
```bash
./ekg_train -d data/PROCESSEDINTEGER/ -s random -n 500
```

**Example 3: Adaptive search with 8 threads**
```bash
./ekg_train -d data/PROCESSEDINTEGER/ -s adaptive -p 8
```

**Example 4: Narrow search around default parameters**
```bash
./ekg_train -d data/PROCESSEDINTEGER/ \
    --vt-hr-min 115 --vt-hr-max 130 --vt-hr-step 5 \
    --vf-conf-min 0.6 --vf-conf-max 0.8 --vf-conf-step 0.05
```

**Example 5: Dry run to check grid size**
```bash
./ekg_train -d data/PROCESSEDINTEGER/ --dry-run
```

**Example 6: 3-fold cross-validation for faster iteration**
```bash
./ekg_train -d data/PROCESSEDINTEGER/ -k 3
```

---

## 8. Computational Considerations

### 8.1 Runtime Estimation

| Configuration | Grid Size | Time per Combo | Total Time (1 thread) | Total Time (8 threads) |
|--------------|-----------|----------------|----------------------|----------------------|
| Full grid | 504,000 | 15 sec | 87.5 days | 11 days |
| Coarse grid | 2,000 | 15 sec | 8.3 hours | 1.0 hours |
| Random (N=1000) | 1,000 | 15 sec | 4.2 hours | 0.5 hours |
| Adaptive (2-phase) | ~5,000 | 15 sec | 20.8 hours | 2.6 hours |

**Factors Affecting Runtime:**
- Number of parameter combinations
- Dataset size (273 files)
- Single file analysis time (~50 ms)
- Cross-validation folds (K=5 default)
- Parallelization (linear speedup expected)

**Recommendations:**
- Start with random search (N=500) for quick exploration: **~2 hours**
- Use adaptive search for production optimization: **~3-5 hours (8 threads)**
- Full exhaustive search not practical

### 8.2 Parallelization Strategy

**Thread-Level Parallelization:**
- Each parameter combination is independent
- Use OpenMP parallel for loop
- Near-linear speedup expected (tested up to 16 threads)

```c
#pragma omp parallel for schedule(dynamic)
for (int i = 0; i < num_combinations; i++) {
    /* Evaluate parameter combination i */
    evaluate_params(params[i], &results[i]);
}
```

**Memory Considerations:**
- Each thread needs separate workspace
- Load files on-demand (don't cache entire dataset)
- Peak memory: ~2 MB per thread

### 8.3 Progress Reporting

The tool SHALL report progress every 10 combinations or every 30 seconds:

```
Progress: [████████████--------] 60% (1210/2016) ETA: 24m
Current best: F1=0.912, Sensitivity=0.945, Specificity=0.901
```

**Elements:**
- Progress bar (text-based)
- Percentage complete
- Combination count (current/total)
- ETA (estimated time remaining)
- Current best metrics

**Interrupt Handling:**
- Support Ctrl+C to gracefully stop
- Save progress to partial results file
- Option to resume from checkpoint

### 8.4 Memory Usage

| Component | Memory Usage | Notes |
|-----------|-------------|-------|
| Single CSV file | ~100 KB | 50K samples × int16_t |
| Analysis workspace | ~50 KB | Intermediate buffers |
| Results storage | ~100 KB | All metrics for 5000 combinations |
| Per-thread overhead | ~200 KB | Stack + workspace |
| **Total (8 threads)** | **~2 MB** | Very modest memory footprint |

No memory scaling issues expected for this dataset size.

---

## 9. Build Integration

### 9.1 Makefile Target

```makefile
# Training & Evaluation Tool
ekg_train: tools/ekg_train.c tools/csv_parser.c $(OBJ_FILES)
	$(CC) $(CFLAGS) -fopenmp -o $@ $^ $(LDFLAGS)

train: ekg_train
	./ekg_train --dataset data/20251129-ECGrecording/PROCESSEDINTEGER/ -s random -n 500

.PHONY: train
```

### 9.2 Dependencies

- **EKG Analyzer Library**: All object files
- **CSV Parser**: Shared with ekg_test_csv tool
- **Standard Libraries**: stdio, stdlib, string, math, time
- **POSIX**: dirent.h for directory scanning
- **OpenMP** (optional): For parallelization (-fopenmp flag)

### 9.3 Compilation

```bash
# Compile with OpenMP support (recommended)
make CFLAGS="-std=c99 -O3 -fopenmp" ekg_train

# Compile without OpenMP (single-threaded)
make CFLAGS="-std=c99 -O3" ekg_train
```

---

## 10. Validation and Recommendations

### 10.1 Validation Checks

The tool SHALL perform the following validation checks:

| Check | Threshold | Action if Failed |
|-------|-----------|------------------|
| Sensitivity ≥90% | 0.90 | WARNING: Safety threshold not met |
| Specificity ≥85% | 0.85 | WARNING: Too many false positives |
| Train/Val gap <10% | 0.10 | WARNING: Potential overfitting |
| CV std dev <10% | 0.10 | WARNING: High variance, need more data |
| Parameters at boundary | N/A | WARNING: Suggest expanding grid range |

### 10.2 Automated Recommendations

Based on results, generate recommendations:

**If Sensitivity < 90%:**
```
RECOMMENDATION: Sensitivity below target (X.X%)
Suggestions:
  - Lower VT_CONFIDENCE_THRESHOLD (increase detection sensitivity)
  - Lower VF_CONFIDENCE_THRESHOLD
  - Expand VF frequency range (lower VF_FREQ_MIN, raise VF_FREQ_MAX)
  - Review false negative cases for patterns
```

**If Specificity < 85%:**
```
RECOMMENDATION: Specificity below target (X.X%)
Suggestions:
  - Raise VT_CONFIDENCE_THRESHOLD (reduce false positives)
  - Raise VF_CONFIDENCE_THRESHOLD
  - Narrow VF frequency range for higher precision
  - Check if normal sinus tachycardia triggering VT detector
```

**If VF Detection Poor:**
```
RECOMMENDATION: VF detection suboptimal
Suggestions:
  - Check VF_FREQ_MIN and VF_FREQ_MAX range
  - Review VF_ZCR_THRESHOLD (zero-crossing rate)
  - Check VF_MIN_AMPLITUDE threshold
  - Examine VF fine variants (lower amplitude)
```

**If VT Detection Poor:**
```
RECOMMENDATION: VT detection suboptimal
Suggestions:
  - Review VT_HR_THRESHOLD (may be too high/low)
  - Check VT_QRS_WIDTH_MS threshold
  - Verify QRS detection accuracy
  - Distinguish from SVT (narrow complex tachycardia)
```

### 10.3 Manual Review Guidance

The tool SHALL list files that require manual review:

**False Negatives (Missed Shockable):**
```
Files requiring review (False Negatives):
  1. SHOCK/PolyVTachType4.integer.csv
     - Predicted: NOSHOCK (confidence: 55%)
     - Reason: Low confidence VT detection
     - Suggestion: Review QRS width and regularity thresholds

  2. SHOCK/VFibFine.integer.csv
     - Predicted: NOSHOCK (confidence: 62%)
     - Reason: Amplitude below VF threshold
     - Suggestion: Consider lowering VF_MIN_AMPLITUDE
```

**False Positives (Unnecessary Shock):**
```
Files requiring review (False Positives):
  1. NOSHOCK/SinusTach_180bpm.integer.csv
     - Predicted: SHOCK (VT detected, confidence: 73%)
     - Reason: Sinus tachycardia with narrow QRS
     - Suggestion: Verify QRS width threshold or improve QRS detector
```

---

## 11. Testing and Validation

### 11.1 Unit Tests

Test individual components:

| Component | Test Case | Expected Result |
|-----------|-----------|----------------|
| CSV Parsing | Load all 273 files | All files parse successfully |
| Ground Truth Extraction | Parse filenames | Correct rhythm type mapping |
| Metrics Calculation | Known confusion matrix | Correct metrics values |
| Grid Generation | Default parameters | Correct grid size |

### 11.2 Integration Tests

**Test 1: Small Synthetic Dataset**
- Create 10 synthetic files (5 SHOCK, 5 NOSHOCK)
- Run with exhaustive search (small grid)
- Verify metrics calculation
- Expected runtime: <5 minutes

**Test 2: Random Search on Full Dataset**
- Run with N=100 random samples
- Verify progress reporting
- Check output files generated
- Expected runtime: ~15 minutes

**Test 3: Adaptive Search**
- Run full adaptive search
- Verify 2-phase execution
- Check convergence to optimal parameters
- Expected runtime: ~2-4 hours (with 8 threads)

### 11.3 Performance Validation

- Verify single file analysis <50 ms
- Verify full dataset evaluation <15 seconds
- Verify parallel speedup (near-linear for 2-8 threads)
- Check memory usage stays <2 MB per thread

---

## 12. Future Enhancements

### 12.1 Potential Features

1. **Bayesian Optimization**: Intelligent parameter search using Gaussian processes
2. **Early Stopping**: Terminate search if no improvement for N iterations
3. **Ensemble Methods**: Combine multiple parameter sets for voting
4. **Per-Rhythm Optimization**: Optimize separately for VF and VT detection
5. **ROC Curve Generation**: Plot ROC curves for different confidence thresholds
6. **Learning Curves**: Plot performance vs dataset size
7. **Feature Importance**: Analyze which parameters have most impact
8. **Automated Report Generation**: HTML report with interactive plots

### 12.2 Advanced Optimization

- **Genetic Algorithms**: Evolutionary parameter optimization
- **Particle Swarm Optimization**: Swarm-based search
- **Simulated Annealing**: Probabilistic optimization
- **Multi-Objective Optimization**: Optimize multiple metrics simultaneously (Pareto frontier)

---

## 13. References

### 13.1 Related Documents

- `specs/initial-implementation/specs.md` - EKG Analyzer Specification
- `specs/csv-analyzer-tool.md` - CSV File Analyzer Tool
- `data/README.md` - Dataset Documentation

### 13.2 Literature References

- Hyperparameter Optimization: Grid Search, Random Search (Bergstra & Bengio, 2012)
- Cross-Validation Techniques (Kohavi, 1995)
- Evaluation Metrics for Imbalanced Classification (Chawla et al., 2002)
- ROC Analysis in Medical Decision Making (Zou et al., 2007)

---

**Document Version**: 1.0
**Date**: 2025-11-29
**Status**: Draft for Implementation
