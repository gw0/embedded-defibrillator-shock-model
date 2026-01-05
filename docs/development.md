# Development Guidelines

## Embedded System Constraints

- **RAM**: <10 KB target
- **Flash**: <20 KB target
- **Sample rate**: 100 Hz fixed
- **Input range**: 300-1000 samples (3-10 seconds)
- **Recommended analysis window**: 500 samples (5 seconds)
- **Data type**: 16-bit signed integers (`int16_t`)

## Medical Context

Algorithm follows ACLS (Advanced Cardiac Life Support) guidelines:

**Shockable rhythms:**
- VFib (all types: coarse, fine)
- Pulseless VT (polymorphic and fast monomorphic >200 bpm)

**Non-shockable rhythms:**
- Asystole
- Stable VT with pulse (120-165 bpm)
- Supraventricular arrhythmias
- Premature ventricular contractions (PVCs)

**Design Priority**: Sensitivity prioritized over specificity (false positive acceptable, false negative dangerous)

## Code Organization

- Modular architecture with clear separation of concerns
- Each component has dedicated `.c` and `.h` files
- Error codes defined in `ekg_config.h`:
  - `EKG_SUCCESS` (0) - Analysis completed successfully
  - `EKG_ERROR_NULL_PTR` (-1) - NULL pointer provided
  - `EKG_ERROR_INVALID_LEN` (-2) - Invalid number of samples
  - `EKG_ERROR_LOW_QUALITY` (-3) - Signal quality too low
  - `EKG_ERROR_SATURATED` (-4) - Signal is saturated
- Maximum QRS detections per analysis: 50 (defined by `QRS_MAX_DETECTIONS`)

## Parameter Modification Workflow

When modifying detection parameters:

1. Update defaults in `src/ekg_config.h`
2. Test with `make test` using integration test suite
3. Optionally run training tool to optimize new parameter space
4. Document medical/algorithmic rationale for changes
