# ECG/EKG Defibrillator Shock Algorithm

An ECG/EKG (electrocardiogram) signal analyzer for detecting ventricular arrhythmias and providing defibrillator shock recommendations.

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

## Features

- **Real-time processing** at 100 Hz sample rate
- **Pan-Tompkins QRS detection** for identifying ventricular depolarizations
- **Frequency domain analysis** using Goertzel algorithm for VF detection
- **Time domain analysis** for rhythm characterization
- **Optimized for embedded systems** (<10 KB RAM, <20 KB Flash)
- **Hardware floating-point** support for ARM Cortex-M33 FPU

## Input Specifications

- **Signal Type**: Single-lead EKG (Lead II equivalent)
- **Data Type**: 16-bit signed integers (`int16_t`)
- **Sample Rate**: 100 Hz
- **Duration**: 3-10 seconds (300-1000 samples)
- **Amplitude**: Any reasonable ADC range (auto-normalized internally)
- **DC Offset**: Can be present (removed and normalized automatically)
- **Pre-filtering**: Optional (baseline wander removal and normalization handled internally)

## Output

The analyzer provides:
- **Shock recommendation** (yes/no)
- **Rhythm classification** (normal, VT, VF, other)
- **Confidence level** (0-100%)
- **Heart rate** (if detectable)
- **Diagnostic information** (QRS count, dominant frequency)

## Building

### For Host System
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
```bash
make arm      # Cross-compile for ARM
```

## Testing
```bash
make test     # Build and run all tests
```

## Requirements

### Host Build
- GCC compiler with C99 support
- Standard math library (-lm)

### ARM Build
- ARM GCC cross-compiler (`arm-none-eabi-gcc`)
- ARM Cortex-M33 target support

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
│   └── vf_detector.c/h     # Ventricular Fibrillation detection
├── tests/                  # Test suite
│   ├── test_signal_processing.c  # Unit tests
│   └── test_ekg_analyzer.c       # Integration tests
├── tools/                  # Development utilities
│   ├── ekg_csv.c           # CSV signal analyzer
│   ├── ekg_train.c         # Training/optimization tool
│   └── ekg_eval.c          # Evaluation tool
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
./bin/ekg_train data/SHOCK/*.csv data/NOSHOCK/*.csv
```

See `docs/tools.md` for detailed usage information.

## Algorithm References

1. **Pan, J., & Tompkins, W. J. (1985)** - "A Real-Time QRS Detection Algorithm"
2. **AHA/ACC/HRS (2017)** - "Guideline for Management of Patients With Ventricular Arrhythmias"
3. **Goertzel Algorithm** - Efficient frequency domain analysis

## License

Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;

