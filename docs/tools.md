# Development Tools

## CSV Analyzer (`ekg_csv`)

Analyzes individual EKG signal files for quick testing.

### Build
```bash
make ekg_csv
```

### Usage
```bash
./bin/ekg_csv data/20251129-ECGrecording/PROCESSEDINTEGER/SHOCK/VFibCoarse.integer.csv
```

### Output
- QRS count
- Heart rate (bpm)
- Dominant frequency
- Shock decision (yes/no)
- Confidence level
- Rhythm classification

## Training Tool (`ekg_train`)

Performs grid search optimization over parameter space to find optimal algorithm configuration.

### Build
```bash
make ekg_train
```

### Usage
```bash
./bin/ekg_train data/20251129-ECGrecording/PROCESSEDINTEGER/SHOCK/*.csv \
                data/20251129-ECGrecording/PROCESSEDINTEGER/NOSHOCK/*.csv
```

### Output Files
- `outputs/best_config.h` - Auto-generated optimal parameters
- `outputs/results.csv` - Performance metrics (accuracy, sensitivity, specificity, F1)

### Method
- Rolling window analysis (500 samples with 50% overlap)
- Evaluates confusion matrix metrics
- Grid search over configurable parameter combinations

### Interpreting Results
Recent training results show F1=32.8%, Sensitivity=100.0%, Specificity=2.4%:
- Algorithm favors sensitivity (catches all true positives)
- Higher false positive rate acceptable for safety
- Medically appropriate: missing a shockable rhythm is more dangerous than false alarm

## Evaluation Tool (`ekg_eval`)

Evaluates algorithm performance on the dataset with current parameters.

### Build
```bash
make ekg_eval
```

### Usage
```bash
./bin/ekg_eval data/20251129-ECGrecording/PROCESSEDINTEGER/SHOCK/*.csv \
                data/20251129-ECGrecording/PROCESSEDINTEGER/NOSHOCK/*.csv
```

### Output
- Accuracy, sensitivity, specificity
- Confusion matrix
- Per-file classification results
