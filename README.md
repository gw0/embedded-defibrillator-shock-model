# EKG/ECG Signal Analyzer

An educational/prototype EKG/ECG (electrocardiogram) signal analyzer for detecting ventricular arrhythmias and providing defibrillator shock recommendations.

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
- **Pre-filtering**: Baseline wander removed, noise filtered

## Output

The analyzer provides:
- **Shock recommendation** (yes/no)
- **Rhythm classification** (normal, VT, VF, other)
- **Confidence level** (0-100%)
- **Heart rate** (if detectable)
- **Diagnostic information** (QRS count, dominant frequency)

## Building

### For Host System (Development/Testing)
```bash
make host
# or simply
make
```

### For ARM Cortex-M33
```bash
make arm
```

### Clean Build Artifacts
```bash
make clean
```

## Requirements

### Host Build
- GCC compiler with C99 support
- Standard math library

### ARM Build
- ARM GCC cross-compiler (`arm-none-eabi-gcc`)
- ARM Cortex-M33 target support

## Project Structure

```
.
├── docs/
│   ├── specs.md          # Requirements specification
│   └── plan.md           # Implementation plan
├── src/
│   ├── ekg_analyzer.c/h  # Main analysis function
│   ├── ekg_config.h      # Configuration constants
│   ├── signal_processing.c/h  # Preprocessing & filtering
│   ├── qrs_detector.c/h  # QRS detection (Pan-Tompkins)
│   ├── heart_rate.c/h    # Heart rate analysis
│   ├── vt_detector.c/h   # Ventricular Tachycardia detection
│   └── vf_detector.c/h   # Ventricular Fibrillation detection
├── Makefile              # Build configuration
└── README.md             # This file
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
    int ret = ekg_analyze(ekg_samples, 500, &result);

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

## Implementation Status

**Phase 1: Complete** ✓
- Project structure created
- All header files with interfaces defined
- Stub implementations created
- Build system configured

**Remaining Phases:**
- Phase 2: Signal Preprocessing
- Phase 3: QRS Detection (Pan-Tompkins)
- Phase 4: Heart Rate Analysis
- Phase 5: VT Detection
- Phase 6: VF Detection
- Phase 7: Integration
- Phase 8: Optimization & Documentation

## Algorithm References

1. **Pan, J., & Tompkins, W. J. (1985)** - "A Real-Time QRS Detection Algorithm"
2. **AHA/ACC/HRS (2017)** - "Guideline for Management of Patients With Ventricular Arrhythmias"
3. **Goertzel Algorithm** - Efficient frequency domain analysis

## License

Educational/Prototype Use Only - See SAFETY NOTICE above

## Contact

For questions about this educational implementation, please refer to the documentation in the `docs/` directory.
