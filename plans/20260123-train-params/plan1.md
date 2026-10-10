# Plan: Consolidate ekg_config.h Configuration

## Goal
Merge `#define` macros and `ekg_config_t` struct in `src/ekg_config.h` to use only the config struct for tunable parameters, while preserving meaningful comments.

## Approach: Full Refactor

Add evidence scoring weights and Bayesian LRs to config struct for training optimization. Remove unused config fields.

---

## Keep as #defines (Non-tunable Constants)

```c
/* Sample rate & input constraints - per specification */
EKG_SAMPLE_RATE, EKG_MIN_SAMPLES, EKG_MAX_SAMPLES, EKG_ANALYSIS_WINDOW

/* QRS Detection algorithm constants */
QRS_REFRACTORY_MS/SAMPLES, QRS_INTEGRATION_MS/SAMPLES, QRS_SEARCHBACK_MS/SAMPLES
QRS_MAX_DETECTIONS

/* Filter coefficients (fixed Butterworth design) */
HP_B0, HP_B1, HP_B2, HP_A1, HP_A2, LP_B0, LP_B1, LP_B2, LP_A1, LP_A2
FILTER_STATE_SIZE

/* Mathematical & algorithm constants */
PI, VF_NUM_TEST_FREQS, VF_TEST_FREQS[] array

/* Error codes */
EKG_SUCCESS, EKG_ERROR_*
```

---

## Remove These #defines (Move Values Inline to Config Struct)

**VT Parameters:**
- `VT_HR_THRESHOLD`, `VT_QRS_WIDTH_MS`, `VT_QRS_WIDTH_SAMPLES`
- `VT_REGULARITY_CV`, `VT_MIN_CONSECUTIVE`, `VT_CONFIDENCE_THRESHOLD`

**VF Detection Parameters:**
- `VF_FREQ_MIN`, `VF_FREQ_MAX`, `VF_ZCR_THRESHOLD`, `VF_QRS_RATE_THRESHOLD`
- `VF_MIN_AMPLITUDE`, `VF_DOMINANCE_THRESHOLD`, `VF_FINE_AMPLITUDE_MAX`
- `VF_FINE_CONFIDENCE`, `VF_NOISE_FLOOR_MULT`, `VF_AMPLITUDE_THRESHOLD_RATIO`
- `VF_EVIDENCE_THRESHOLD`, `VF_FLATNESS_HIGH_THRESH`, `VF_FLATNESS_MOD_THRESH`

**Evidence Scoring Weights (currently hardcoded, will add to config):**
- `VF_EV_AMP_HIGH`, `VF_EV_AMP_MEDIUM`, `VF_EV_AMP_LOW`, `VF_EV_AMP_VLOW`
- `VF_EV_FREQ_OPTIMAL`, `VF_EV_FREQ_GOOD`, `VF_EV_FREQ_EDGE`
- `VF_EV_QRS_ABSENT`, `VF_EV_HIGH_IRREG`, `VF_EV_MOD_IRREG`
- `VF_EV_ENTROPY_HIGH`, `VF_EV_ENTROPY_MOD`
- `VF_EV_FLATNESS_HIGH`, `VF_EV_FLATNESS_MOD`
- `VF_EV_ORGANIZED_PENALTY`, `VF_EV_QRS_FREQ_PENALTY`, `VF_EV_QRS_REGULAR_PENALTY`

**Bayesian Log-LRs (currently hardcoded, will add to config):**
- `LR_VT_HR_THRESH_POS/NEG`, `LR_VT_HR_HIGH_POS/NEG`
- `LR_VT_QRS_WIDE_POS/NEG`, `LR_VT_QRS_VWIDE_POS/NEG`
- `LR_VT_REGULAR_POS/NEG`, `LR_VT_VREGULAR_POS/NEG`
- `LR_VT_SUSTAINED_POS/NEG`

**Signal Quality:**
- `SNR_GOOD_THRESHOLD`, `SIGNAL_QUALITY_MIN`

---

## Remove Unused Config Fields

These are initialized but never accessed in code:
- `vf_min_amplitude`
- `vf_fine_confidence`
- `vf_zcr_threshold`

---

## New Config Struct Organization

```c
typedef struct {
    /* === VT Detection Parameters === */
    float vt_hr_threshold;           /* bpm - heart rate threshold */
    float vt_qrs_width_ms;           /* ms - QRS width for wide complex */
    float vt_regularity_cv;          /* percent - CV threshold for regular rhythm */
    float vt_confidence_threshold;   /* 0.0-1.0 */
    float vt_hr_high_thresh;         /* bpm - HR for additional evidence */
    float vt_qrs_vwide_thresh;       /* ms - very wide QRS threshold */
    float vt_vregular_cv_thresh;     /* percent - very regular threshold */

    /* === VT Bayesian Log-Likelihood Ratios === */
    float lr_vt_hr_thresh_pos;       /* LR+ when HR > threshold */
    float lr_vt_hr_thresh_neg;       /* LR- when HR <= threshold */
    float lr_vt_hr_high_pos;         /* LR+ when HR > high threshold */
    float lr_vt_hr_high_neg;
    float lr_vt_qrs_wide_pos;        /* LR+ when QRS wide */
    float lr_vt_qrs_wide_neg;
    float lr_vt_qrs_vwide_pos;       /* LR+ when QRS very wide */
    float lr_vt_qrs_vwide_neg;
    float lr_vt_regular_pos;         /* LR+ when rhythm regular */
    float lr_vt_regular_neg;
    float lr_vt_vregular_pos;        /* LR+ when rhythm very regular */
    float lr_vt_vregular_neg;
    float lr_vt_sustained_pos;       /* LR+ when sustained */
    float lr_vt_sustained_neg;

    /* === VF Detection Parameters === */
    float vf_freq_min;               /* Hz - minimum VF frequency */
    float vf_freq_max;               /* Hz - maximum VF frequency */
    float vf_dominance_threshold;    /* 0.0-1.0 - freq dominance ratio */
    float vf_noise_floor_mult;       /* noise floor multiplier */
    float vf_amplitude_thresh_ratio; /* ratio of peak for threshold crossings */
    float vf_evidence_threshold;     /* minimum evidence score for VF */
    float vf_confidence_threshold;   /* minimum confidence for shock */
    float vf_penalty_freq_min;       /* Hz - min freq for penalty exemption */
    float vf_penalty_freq_max;       /* Hz - max freq for penalty exemption */

    /* === VF Amplitude Thresholds === */
    float vf_amp_high_thresh;        /* amplitude for high evidence */
    float vf_amp_medium_thresh;      /* amplitude for medium evidence */
    float vf_amp_low_thresh;         /* amplitude for low evidence */
    float vf_amp_vlow_thresh;        /* amplitude for very low evidence */

    /* === VF Irregularity Thresholds === */
    float vf_zcr_high_thresh;        /* ZCR for high irregularity */
    float vf_zcr_mod_thresh;         /* ZCR for moderate irregularity */
    float vf_tcr_high_thresh;        /* TCR for high irregularity */
    float vf_tcr_mod_thresh;         /* TCR for moderate irregularity */

    /* === VF Entropy Thresholds === */
    float vf_entropy_high_thresh;    /* entropy for high evidence */
    float vf_entropy_mod_thresh;     /* entropy for moderate evidence */

    /* === VF Spectral Flatness Thresholds === */
    float vf_flatness_high_thresh;   /* high flatness (chaotic) */
    float vf_flatness_mod_thresh;    /* moderate flatness */

    /* === VF Organized Rhythm Detection === */
    float vf_org_hr_min;             /* min HR for organized tachycardia */
    float vf_org_hr_max;             /* max HR for organized tachycardia */
    float vf_org_cv_thresh;          /* CV for regular rhythm */
    float vf_qrs_rate_penalty_thresh;/* QRS rate threshold for penalty */

    /* === VF Evidence Scoring Weights === */
    float vf_ev_amp_high;            /* weight for amplitude > high_thresh */
    float vf_ev_amp_medium;          /* weight for amplitude > medium_thresh */
    float vf_ev_amp_low;             /* weight for amplitude > low_thresh */
    float vf_ev_amp_vlow;            /* weight for amplitude > vlow_thresh */
    float vf_ev_freq_optimal;        /* weight for 5-8 Hz */
    float vf_ev_freq_good;           /* weight for configurable range */
    float vf_ev_freq_edge;           /* weight for 2.5-12 Hz edge */
    float vf_ev_qrs_absent;          /* weight when no QRS detected */
    float vf_ev_high_irreg;          /* weight for high irregularity */
    float vf_ev_mod_irreg;           /* weight for moderate irregularity */
    float vf_ev_entropy_high;        /* weight for high entropy */
    float vf_ev_entropy_mod;         /* weight for moderate entropy */
    float vf_ev_flatness_high;       /* weight for high spectral flatness */
    float vf_ev_flatness_mod;        /* weight for moderate flatness */
    float vf_ev_organized_penalty;   /* penalty for organized tachycardia */
    float vf_ev_qrs_freq_penalty;    /* penalty for QRS + freq outside VF */
    float vf_ev_qrs_regular_penalty; /* penalty for QRS + regular RR */

    /* === Signal Quality === */
    float snr_good_threshold;        /* SNR threshold for good quality */
    float signal_quality_min;        /* minimum acceptable signal quality */
} ekg_config_t;
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/ekg_config.h` | Remove redundant defines, add new config fields, reorganize |
| `src/vf_detector.c` | Change `VF_EV_*` defines to `config->vf_ev_*` access |
| `src/vt_detector.c` | Change `LR_VT_*` defines to `config->lr_vt_*` access |
| `src/signal_utils.c` | Change `SNR_GOOD_THRESHOLD`, `SIGNAL_QUALITY_MIN` to config access |
| `tools/ekg_train.c` | Add new config fields to training parameter space (if applicable) |

---

## Verification

1. `make clean && make` - Ensure compilation succeeds
2. `make test` - Run test suite to verify no regressions
3. `./build/ekg_eval --dataset test/data/combined.csv` - Evaluate accuracy unchanged
