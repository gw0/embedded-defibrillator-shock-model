# Development Tools

These tools are for training and evaluating the algorithm on a host system. The core algorithm in `src/` is designed for embedded devices and does not depend on these tools or datasets.

## Dataset Structure

```
datasets/
├── SHOCK/      # Shockable rhythms
└── NOSHOCK/    # Non-shockable rhythms
```

Rhythm type parsed from filenames: `vfib=coarse`, `vfib=fine`, `nsr=`, `vntwave=`, `prewave=`, `spvwave=`, `cndwave=`.

## CSV Analyzer (`ekg_csv`)

Analyzes individual EKG signal files for quick testing.

### Build
```bash
make ekg_csv
```

### Usage
```bash
./bin/ekg_csv datasets/SHOCK/<file>.csv
```

### Output
- QRS count
- Heart rate (bpm)
- Dominant frequency
- Shock decision (yes/no)
- Confidence level
- Rhythm classification

### Output Example
```
OVERALL RECOMMENDATION: SHOCK ADVISED
Shock advised in 3 of 5 windows (60.0%)
```

## Training Tool (`ekg_train`)

Performs grid search optimization over parameter space to find optimal algorithm configuration.

### Build
```bash
make ekg_train
```

### Usage
```bash
./bin/ekg_train datasets/
```

### Output Files
- `outputs/best_config.h` - Auto-generated optimal parameters
- `outputs/results.csv` - Performance metrics (accuracy, sensitivity, specificity, F1)

### Method
- Rolling window analysis (500 samples with 50% overlap)
- Evaluates confusion matrix metrics
- Grid search over configurable parameter combinations

### Interpreting Results
Best training result: F2=96.0%, Sensitivity=99.9%, Specificity=79.8%:
- Algorithm favors sensitivity (catches nearly all true positives)
- Higher false positive rate acceptable for safety
- Medically appropriate: missing a shockable rhythm is more dangerous than false alarm

### Optimization Metric

Training optimizes F2 score (beta=2): weights sensitivity higher than precision.
Missing a shockable rhythm is more dangerous than a false alarm.

### Output Example
```
Baseline: F2=0.8500, F1=0.7800, Sens=0.9200, Spec=0.8000
Search:   F2=0.8800, F1=0.8100, Sens=0.9500, Spec=0.8200  (F2 +0.0300)
```

## Evaluation Tool (`ekg_eval`)

Evaluates algorithm performance on the dataset with current parameters.

### Build
```bash
make ekg_eval
```

### Usage
```bash
./bin/ekg_eval datasets/
```

### Output
- Accuracy, sensitivity, specificity
- Confusion matrix
- Per-file classification results

### Output Example
```
Files loaded: 42 (12 SHOCK, 30 NOSHOCK)
  Accuracy:    85.0%
  Sensitivity: 92.0%
  Specificity: 80.0%
  F1-Score:    78.0%
```

## Diagnostic Evaluation Tool (`ekg_eval_diag`)

Evaluates algorithm performance with detailed failure breakdown for debugging.

### Build
```bash
make ekg_eval_diag
```

### Usage
```bash
./bin/ekg_eval_diag datasets/
```

### Output
Output includes failure breakdown for missed shockable rhythms.
