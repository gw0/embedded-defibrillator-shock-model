# Training & Evaluation Tool - Implementation Plan

## Status: Ready for Implementation

### Completed Work

✅ **Configuration Infrastructure** (Complete)
- Created `ekg_config_t` struct in `src/ekg_config.h` with all tunable parameters
- Added `EKG_DEFAULT_CONFIG` static const for default values
- Updated `ekg_analyze()` to accept optional `const ekg_config_t *config` (NULL = use defaults)
- Modified `detect_vt()` and `detect_vf()` to accept and use config parameters
- Updated all existing code (ekg_csv tool, tests) to pass NULL for default behavior
- **Code compiles successfully**

### Implementation Tasks

The training tool (`tools/ekg_train.c`) needs to be implemented with the following components:

---

## 1. Dataset Loading & Ground Truth Parsing

**File**: `tools/ekg_train.c` (and helper functions)

### 1.1 Dataset File Structure
```c
typedef struct {
    char filepath[512];         /* Full path to CSV file */
    uint8_t ground_truth_shock; /* 0=NOSHOCK, 1=SHOCK (from directory) */
    uint8_t ground_truth_rhythm; /* 0=Unknown, 1=Normal, 2=VT, 3=VF, 4=Other */
} dataset_file_t;

typedef struct {
    dataset_file_t *files;      /* Array of files */
    uint32_t count;             /* Total number of files */
    uint32_t shock_count;       /* Number of SHOCK files */
    uint32_t noshock_count;     /* Number of NOSHOCK files */
} dataset_t;
```

### 1.2 Directory Scanning
Use POSIX `dirent.h` to scan:
- `data/20251129-ECGrecording/PROCESSEDINTEGER/SHOCK/*.integer.csv`
- `data/20251129-ECGrecording/PROCESSEDINTEGER/NOSHOCK/*.integer.csv`

**Function**: `int load_dataset(const char *base_path, dataset_t *dataset)`

### 1.3 Ground Truth Parsing (from filename)
Extract rhythm type from filename using simple pattern matching:

```c
uint8_t parse_rhythm_type(const char *filename) {
    // VFib*.csv or VFib2*.csv → 3 (VF)
    // PolyVTach*.csv or MonoVTach*.csv → 2 (VT)
    // Normal*.csv → 1 (Normal)
    // asystole*.csv, AFib*.csv, AFlutter*.csv → 4 (Other)
    // Default → 4 (Other)
}
```

---

## 2. Metrics Calculation

### 2.1 Confusion Matrix Structure
```c
typedef struct {
    uint32_t tp, tn, fp, fn;    /* Binary (SHOCK/NOSHOCK) */
    float accuracy;
    float sensitivity;          /* Critical: TP / (TP + FN) */
    float specificity;          /* TN / (TN + FP) */
    float precision;            /* TP / (TP + FP) */
    float f1_score;             /* Primary optimization metric */
    uint32_t rhythm_confusion[5][5]; /* Per-rhythm 5x5 matrix */
} metrics_t;
```

### 2.2 Evaluation Function
```c
/* Evaluate config on dataset subset */
int evaluate_config(const ekg_config_t *config,
                   const dataset_t *dataset,
                   const uint32_t *file_indices,  /* Which files to test */
                   uint32_t num_files,
                   metrics_t *metrics);
```

**Algorithm**:
1. For each file in subset:
   - Load CSV using existing `load_csv_file()` from ekg_csv.c (extract to shared function)
   - Call `ekg_analyze(samples, num_samples, &result, config)`
   - Compare `result.shock_advised` to `ground_truth_shock` → update TP/TN/FP/FN
   - Compare `result.rhythm_type` to `ground_truth_rhythm` → update 5x5 matrix
2. Calculate metrics: accuracy, sensitivity, specificity, precision, F1

---

## 3. Grid Search Parameter Space

### 3.1 Parameter Grid Structure
```c
typedef struct {
    float *vt_hr_values;        uint32_t vt_hr_count;
    float *vt_qrs_values;       uint32_t vt_qrs_count;
    float *vt_reg_values;       uint32_t vt_reg_count;
    float *vt_conf_values;      uint32_t vt_conf_count;
    float *vf_fmin_values;      uint32_t vf_fmin_count;
    float *vf_fmax_values;      uint32_t vf_fmax_count;
    float *vf_zcr_values;       uint32_t vf_zcr_count;
    float *vf_conf_values;      uint32_t vf_conf_count;
} param_grid_t;
```

### 3.2 Grid Generation
```c
/* Initialize default grid from specs */
void init_default_grid(param_grid_t *grid);

/* Calculate total combinations */
uint64_t grid_size(const param_grid_t *grid);

/* Get Nth configuration from grid */
void grid_get_config(const param_grid_t *grid, uint64_t index, ekg_config_t *config);
```

**Default Grid** (from spec section 3.1.1):
- VT_HR_THRESHOLD: 100 to 140 step 5 (9 values)
- VT_QRS_WIDTH_MS: 100 to 140 step 10 (5 values)
- VT_REGULARITY_CV: 15 to 30 step 5 (4 values)
- VT_CONFIDENCE_THRESHOLD: 0.5 to 0.9 step 0.1 (5 values)
- VF_FREQ_MIN: 3.5 to 5.0 step 0.5 (4 values)
- VF_FREQ_MAX: 9.0 to 12.0 step 0.5 (7 values)
- VF_ZCR_THRESHOLD: 0.10 to 0.25 step 0.05 (4 values)
- VF_CONFIDENCE_THRESHOLD: 0.5 to 0.9 step 0.1 (5 values)
- **Total: 504,000 combinations**

---

## 4. Search Strategies

### 4.1 Strategy 1: Exhaustive Grid Search
```c
void search_exhaustive(const param_grid_t *grid,
                      const dataset_t *dataset,
                      const uint32_t *train_indices,
                      uint32_t num_train,
                      metrics_t *best_metrics,
                      ekg_config_t *best_config);
```
- Loop through ALL combinations: `for (i = 0; i < grid_size(grid); i++)`
- Evaluate each config
- Track best F1 score
- **Use only for small grids (<5000 combinations)**

### 4.2 Strategy 2: Random Search
```c
void search_random(const param_grid_t *grid,
                  const dataset_t *dataset,
                  const uint32_t *train_indices,
                  uint32_t num_train,
                  uint32_t num_samples,  /* e.g., 500-2000 */
                  metrics_t *best_metrics,
                  ekg_config_t *best_config);
```
- Randomly sample N configurations from grid
- Use `rand()` with fixed seed for reproducibility
- Evaluate each sampled config
- Track best F1 score

### 4.3 Strategy 3: Adaptive Search (Recommended)
```c
void search_adaptive(const param_grid_t *grid,
                    const dataset_t *dataset,
                    const uint32_t *train_indices,
                    uint32_t num_train,
                    metrics_t *best_metrics,
                    ekg_config_t *best_config);
```

**Algorithm**:
1. **Phase 1: Coarse Search**
   - Create coarse grid (double all step sizes) → ~2000 combinations
   - Test all coarse combinations
   - Find top K=5 best configs
2. **Phase 2: Fine Search**
   - For each of top-5:
     - Create fine grid around it (±2 steps in each dimension)
     - Test all fine combinations (~500 each)
   - Return overall best

---

## 5. Cross-Validation

### 5.1 K-Fold Stratified Split
```c
typedef struct {
    uint32_t **folds;           /* folds[k] = array of file indices */
    uint32_t *fold_sizes;       /* fold_sizes[k] = number of files */
    uint32_t num_folds;         /* K (default: 10) */
} cross_validation_t;
```

```c
/* Create stratified K-fold split (maintain SHOCK/NOSHOCK ratio) */
void create_kfold_split(const dataset_t *dataset,
                       uint32_t k,
                       cross_validation_t *cv);
```

### 5.2 Evaluation with CV
```c
/* Evaluate config using K-fold CV, return mean metrics */
void evaluate_with_cv(const ekg_config_t *config,
                     const dataset_t *dataset,
                     const cross_validation_t *cv,
                     metrics_t *mean_metrics,
                     float *std_dev);  /* Standard deviation across folds */
```

---

## 6. Progress Reporting

### 6.1 Progress Tracker
```c
typedef struct {
    time_t start_time;
    uint64_t total_combinations;
    uint64_t completed;
    metrics_t current_best;
    ekg_config_t current_best_config;
} progress_t;
```

### 6.2 Progress Display
Print every 10 combinations or every 30 seconds:
```
Progress: [████████████--------] 60% (1210/2016) ETA: 24m
Current best: F1=0.912, Sensitivity=0.945, Specificity=0.901
  Parameters: VT_HR=125, VT_QRS=120, ...
```

**Functions**:
```c
void print_progress(const progress_t *progress);
void update_eta(progress_t *progress);  /* Calculate ETA */
```

---

## 7. OpenMP Parallelization

### 7.1 Parallel For Loop
```c
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
for (uint64_t i = 0; i < num_combinations; i++) {
    ekg_config_t config;
    metrics_t metrics;

    grid_get_config(&grid, indices[i], &config);
    evaluate_config(&config, dataset, train_indices, num_train, &metrics);

    #pragma omp critical
    {
        if (metrics.f1_score > best_f1) {
            best_f1 = metrics.f1_score;
            best_config = config;
            best_metrics = metrics;
        }
    }
}
```

### 7.2 Thread-Safe Considerations
- Each thread needs own workspace (CSV buffer, analysis buffers)
- Use `#pragma omp critical` for updating best result
- Use `schedule(dynamic)` for load balancing

---

## 8. Output Generation

### 8.1 CSV Results File
**File**: `results.csv`
```csv
vt_hr,vt_qrs,vt_reg,vt_conf,vf_min,vf_max,vf_zcr,vf_conf,accuracy,sensitivity,specificity,precision,f1,tp,tn,fp,fn
125,120,20,0.7,4.0,10.0,0.17,0.7,0.942,0.963,0.931,0.875,0.917,65,192,14,2
...
```

### 8.2 Best Config Header File
**File**: `best_config.h`
```c
/* Generated by ekg_train on YYYY-MM-DD */
/* Performance: F1=91.7%, Sensitivity=96.3%, Specificity=93.1% */
#ifndef EKG_OPTIMIZED_CONFIG_H
#define EKG_OPTIMIZED_CONFIG_H

#define VT_HR_THRESHOLD       125.0f
#define VT_QRS_WIDTH_MS       120.0f
...
#endif
```

### 8.3 Text Report
**File**: `report.txt`
- Summary of best parameters
- Performance metrics with confusion matrices
- List of false negatives/positives
- Recommendations

---

## 9. Command-Line Interface

### 9.1 Required Arguments
- `-d, --dataset <path>`: Path to dataset root directory

### 9.2 Optional Arguments
- `-s, --strategy <type>`: exhaustive, random, adaptive (default: adaptive)
- `-n, --num-samples <N>`: Number of samples for random search (default: 1000)
- `-k, --kfold <K>`: K for K-fold cross-validation (default: 10)
- `-o, --output <file>`: Output CSV file (default: results.csv)
- `-p, --parallel <N>`: Number of threads (default: 1)
- `--dry-run`: Show grid config and exit
- `-h, --help`: Show help

### 9.3 Parameter Overrides
Each parameter can have min/max/step override:
- `--vt-hr-min <val>`, `--vt-hr-max <val>`, `--vt-hr-step <val>`
- `--vt-qrs-min <val>`, `--vt-qrs-max <val>`, `--vt-qrs-step <val>`
- etc. for all 8 parameters

---

## 10. Implementation Order (Recommended)

### Phase 1: Core Infrastructure
1. **Dataset loading** (directory scan, ground truth parsing)
2. **Metrics calculation** (evaluate single config on dataset subset)
3. **Grid generation** (create parameter space)
4. **CSV loading** (extract from ekg_csv.c to shared utility)

### Phase 2: Basic Search
5. **Random search** (simplest strategy, good for testing)
6. **Progress reporting** (basic version)
7. **Output generation** (CSV results file)

### Phase 3: Advanced Features
8. **Exhaustive search** (straightforward loop)
9. **Adaptive search** (coarse → fine)
10. **Cross-validation** (K-fold stratified)
11. **OpenMP parallelization** (add pragmas)

### Phase 4: Polish
12. **Best config header generation**
13. **Text report generation**
14. **Command-line parsing** (getopt_long)
15. **Validation checks** (spec section 10.1)
16. **Recommendations** (spec section 10.2)

---

## 11. Code Reuse Opportunities

### From `tools/ekg_csv.c`:
- `load_csv_file()` - extract to shared utility
- `parse_line()` - CSV parsing logic
- `find_separator()` - handle arrow/comma separators

### Shared Utility File (Optional):
Create `tools/csv_utils.c` with:
```c
int load_csv_file(const char *filename, int16_t *samples, uint32_t *count);
```

---

## 12. Key Design Decisions

### 12.1 Memory Management
- **Load files on-demand** (don't cache entire dataset in memory)
- Each evaluation loads CSV → analyzes → frees
- Peak memory: ~2MB per thread (very manageable)

### 12.2 Optimization Metric
- **Primary**: F1-score (harmonic mean of precision and sensitivity)
- **Constraint**: Sensitivity must be high (>90%, ideally >95%)
- False negatives are more dangerous than false positives

### 12.3 Randomness
- Use fixed seed (`srand(42)`) for reproducibility
- Random search should give same results on multiple runs

### 12.4 Progress Tracking
- Store all tested configs to CSV (not just best)
- Allows post-analysis, visualization, debugging

---

## 13. Validation & Testing

### 13.1 Sanity Checks
Before implementing full tool:
1. Test dataset loading on actual files
2. Test single config evaluation
3. Verify metrics calculation with known examples
4. Test grid generation (correct number of combinations)

### 13.2 Dry Run Mode
`./ekg_train --dataset data/... --dry-run` should print:
```
Grid Configuration:
  VT_HR_THRESHOLD: 100-140 step 5 (9 values)
  ...
Total Combinations: 504,000
Estimated Runtime: 42-168 hours (single-threaded)
                   5-21 hours (8 threads)
Recommendation: Use --strategy adaptive or --strategy random
```

---

## 14. Makefile Integration

Add to `Makefile`:
```makefile
# Training tool
ekg_train: tools/ekg_train.c $(OBJ_FILES) obj/host/csv_utils.o
	$(CC) $(CFLAGS) -fopenmp -o $@ $^ -lm

train: ekg_train
	./ekg_train --dataset data/20251129-ECGrecording/PROCESSEDINTEGER/ \
	            --strategy random --num-samples 500

.PHONY: train
```

---

## 15. Expected Output Example

```
========================================
EKG Training & Evaluation Tool
========================================
Dataset: data/20251129-ECGrecording/PROCESSEDINTEGER/
Files loaded: 273 (67 SHOCK, 206 NOSHOCK)
Strategy: Random Search
Samples: 500
Cross-Validation: 10-fold stratified

Grid Configuration:
  VT_HR_THRESHOLD: 100-140 step 5 (9 values)
  ... [all parameters]
Total Grid Size: 504,000 combinations
Sampling: 500 random combinations

Progress: [████████████████████] 100% (500/500) Completed
Best found: F1=0.917, Sensitivity=0.963, Specificity=0.931

========================================
OPTIMIZATION RESULTS
========================================
BEST PARAMETERS FOUND:
  VT_HR_THRESHOLD: 125.0 bpm
  VT_QRS_WIDTH_MS: 120.0 ms
  ...

PERFORMANCE METRICS (10-fold CV):
  Accuracy:     94.2% ± 1.8%
  Sensitivity:  96.3% ± 2.1%
  Specificity:  93.1% ± 2.3%
  Precision:    87.5% ± 3.2%
  F1-Score:     91.7% ± 2.0%

[Confusion matrices, recommendations, etc.]

Results saved to: results.csv, best_config.h, report.txt
========================================
```

---

## 16. Success Criteria

The implementation is complete when:
- ✅ Can load dataset from directory structure
- ✅ Can evaluate any config on dataset subset
- ✅ All 3 search strategies implemented and working
- ✅ K-fold cross-validation implemented
- ✅ OpenMP parallelization working (near-linear speedup)
- ✅ Generates all output files (CSV, header, report)
- ✅ Command-line interface complete with all options
- ✅ Can run on actual dataset and find optimal parameters
- ✅ Completes random search (N=500) in <30 minutes on 8 cores
- ✅ Completes adaptive search in 1-4 hours on 8 cores

---

## Notes

- Keep all functions **simple and short** as requested
- Prefer clarity over cleverness
- Extract reusable code to small helper functions
- Add comments explaining algorithms (especially adaptive search)
- Follow existing code style from `ekg_csv.c`
