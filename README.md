# ECG/EKG Defibrillator Shock Model for Embedded

[![GitHub](https://img.shields.io/badge/GitHub-gw0%2Fembedded--defibrillator--shock--model-181717?logo=github)](https://github.com/gw0/embedded-defibrillator-shock-model)
[![Sponsor](https://img.shields.io/badge/sponsor-%E2%9D%A4-red?logo=github-sponsors)](https://github.com/sponsors/gw0)

An ECG/EKG (electrocardiogram) signal analyzer for detecting ventricular arrhythmias and providing defibrillator shock recommendations optimized for embedded systems achieving **96.0% F2-score**.

- **Optimized for embedded systems** (C99, no external dependencies, <10 KB RAM, <10 KB Flash)
- **Hardware floating-point** support for ARM Cortex-M33 FPU
- **Real-time processing** at 100 Hz sample rate, 3-10s windows
- **Training pipeline** with random/local/adaptive grid search over 30+ parameters
- **Pan-Tompkins QRS detection** for identifying ventricular depolarizations
- **Frequency domain analysis** using Goertzel algorithm for VF detection
- **Time domain analysis** for rhythm characterization

## ⚠️ SAFETY NOTICE

**THIS IS AN EDUCATIONAL/PROTOTYPE IMPLEMENTATION ONLY**

- **NOT** a medical device
- **NOT** certified for patient care
- **NOT** compliant with IEC 60601 or other medical device standards
- **DO NOT** use this system for actual patient care

This software is intended solely for educational purposes, algorithm development, and prototype testing.

## Overview

This analyzer processes EKG signals in real-time on an ARM Cortex M33 microcontroller to detect:
- **Ventricular Fibrillation (VF)** - chaotic, life-threatening arrhythmia
- **Ventricular Tachycardia (VT)** - rapid ventricular rhythm

The system recommends whether a defibrillator shock should be administered based on the detected rhythm.

## Input Specifications

- **Signal Type**: Single-lead EKG (Lead II equivalent)
- **Data Type**: 16-bit signed integers (`int16_t`)
- **Sample Rate**: 100 Hz
- **Duration**: 3-10 seconds (300-1000 samples)
- **Pre-filtering**: Baseline wander removed, noise filtered

## Output Specifications

The analyzer provides:
- **Shock recommendation** (yes/no)
- **Rhythm classification** (normal, VT, VF, other)
- **Confidence level** (0-100%)
- **Heart rate** (if detectable)
- **Diagnostic information** (QRS count, dominant frequency)

## Training

### Dataset

6286 labeled single-lead ECG windows as headerless CSV files at 100 Hz, 5s (500 measurements) or 8s (800 measurements):

- **Shockable** (3123): ventricular fibrillation (coarse, fine)
- **Non-shockable** (3163): normal sinus rhythm, ventricular (non-VF) arrhythmias, premature beats, supraventricular arrhythmias, and conduction abnormalities
- **Labels**: encoded in file names (`shock-123059-vfib=coarse.csv` and `no-shock-084252-nsr=060.csv`)

See [`datasets/README.md`](datasets/README.md) for the full description.

### Training Pipeline

```
datasets/ ──► ekg_train ──► outputs/best_config.h ──► ekg_eval ──► src/ (embedded library)
 labeled CSV   grid search     tuned parameters        metrics      ekg_analyze()
               (F2 objective)                          per rhythm
```

### Training Results

Evaluated with `ekg_eval` on the 5s dataset (6286 windows):

| Metric | Before tuning | Tuned |
|--------|--------------:|------:|
| **F2-score** | 85.2% | **96.0%** |
| Sensitivity (shockable) | 86.2% | 99.9% |
| Specificity (non-shockable) | 80.5% | 79.8% |
| Accuracy | 83.3% | 89.8% |

Missing a shockable rhythm is far more dangerous than a false alarm, so training maximizes F2 (recall weighted over precision) and trades specificity for sensitivity. Results are measured on the same data used for tuning, without a held-out split.

## Building

### For Host System

- GCC compiler with C99 support
- Standard math library (-lm)

```bash
make          # Default: host build
make host     # Explicit host build
make test     # Build and run all tests
make ekg_csv  # Build CSV analyzer tool
make ekg_train # Build training/optimization tool
make ekg_eval # Build evaluation tool
make clean    # Clean build artifacts
make help     # Show all available targets
```

### For ARM Cortex-M33

- ARM GCC cross-compiler (`gcc-arm-none-eabi`)
- ARM Cortex-M33 target support

```bash
make arm      # Cross-compile for ARM
```

## Testing

```bash
make test     # Build and run all tests
```

## Project Structure

```
.
├── src/                    # Core algorithm implementation
│   ├── ekg_analyzer.c/h    # Main analysis API
│   ├── ekg_config.h        # Configuration constants
│   ├── signal_processing.c/h  # Filters and preprocessing
│   ├── qrs_detector.c/h    # Pan-Tompkins QRS detection
│   ├── heart_rate.c/h      # Heart rate analysis
│   ├── vt_detector.c/h     # Ventricular Tachycardia detection
│   ├── vf_detector.c/h     # Ventricular Fibrillation detection
│   └── signal_utils.c/h    # Signal quality and Bayesian helpers
├── tests/                  # Test suite
│   ├── test_signal_processing.c  # Unit tests
│   ├── test_ekg_analyzer.c       # Integration tests
│   └── test_vf_detector.c        # VF detector tests
├── tools/                  # Development utilities
│   ├── ekg_csv.c           # CSV signal analyzer
│   ├── ekg_train.c         # Training/optimization tool
│   ├── ekg_eval.c          # Evaluation tool
│   ├── csv_utils.c/h       # CSV parsing utilities
│   └── eval_utils.c/h      # Evaluation helpers
├── docs/                   # Documentation
│   ├── architecture.md     # Signal processing pipeline
│   ├── development.md      # Development guidelines
│   ├── tools.md            # Tool usage details
│   └── testing.md          # Test suite details
├── Makefile                # Build configuration
└── README.md               # This file
```

## Usage Example

```c
#include "ekg_analyzer.h"

int main(void) {
    // Sample EKG data (from ADC)
    int16_t ekg_samples[500];

    // ... acquire samples ...

    // Analyze
    ekg_analysis_result_t result;
    int ret = ekg_analyze(ekg_samples, 500, &result, NULL);

    if (ret == EKG_SUCCESS) {
        if (result.shock_advised) {
            // Trigger defibrillator
            printf("SHOCK ADVISED - Rhythm: %s\\n",
                   result.rhythm_type == 2 ? "VT" : "VF");
        } else {
            printf("No shock advised\\n");
        }
    }

    return 0;
}
```

## Development Tools

### CSV Analyzer
Analyze individual EKG signal files:
```bash
make ekg_csv
./bin/ekg_csv data/path/to/signal.csv
```

### Training Tool
Optimize algorithm parameters using grid search:
```bash
make ekg_train
./bin/ekg_train dataset/
```

### Diagnostic Evaluation
```bash
make ekg_eval_diag
./bin/ekg_eval_diag dataset/
```

See `docs/tools.md` for detailed usage information.

## Documentation

- [`docs/architecture.md`](docs/architecture.md) - Signal processing pipeline details
- [`docs/development.md`](docs/development.md) - Development guidelines and constraints
- [`docs/testing.md`](docs/testing.md) - Test suite documentation
- [`docs/tools.md`](docs/tools.md) - CLI tools for training and evaluation
- [`datasets/README.md`](datasets/README.md) - Dataset description

## Algorithm References

1. **Pan, J., & Tompkins, W. J. (1985)** - "A Real-Time QRS Detection Algorithm"
2. **AHA/ACC/HRS (2017)** - "Guideline for Management of Patients With Ventricular Arrhythmias"
3. **Goertzel Algorithm** - Efficient frequency domain analysis
4. **Shannon Entropy** - Signal complexity measurement
5. **Spectral Flatness (Wiener Entropy)** - Noise-like vs tonal signal characterization

## License

Copyright &copy; 2026 [gw0](https://gw.tnode.com/) &lt;<gw.2026@ena.one>&gt;. All rights reserved.

If my work saves you time, money, or tokens, consider [sponsoring](https://github.com/sponsors/gw0) the work that keeps it maintained — and tell whoever else benefits, human or agent.
