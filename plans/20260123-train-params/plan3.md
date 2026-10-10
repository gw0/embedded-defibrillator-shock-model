# Plan: Consolidate ekg_config.h Configuration

## Goal
Merge `#define` macros and `ekg_config_t` struct in `src/ekg_config.h` to use only the config struct for tunable parameters, preserving meaningful comments and organizing parameters logically.

## Approach
- Add evidence scoring weights and Bayesian LRs to config struct
- Remove unused config fields (`vf_min_amplitude`, `vf_fine_confidence`, `vf_zcr_threshold`)
- Preserve all meaningful comments from #defines
- Organize config struct into logical sections
- Update `ekg_train.c` to output struct format (per plan2.md)

---

## Keep as #defines (Non-tunable Spec/Algorithm Constants)

```c
/* ========================================
 * SPECIFICATION CONSTANTS
 * Per specification section 4.3
 * ======================================== */
#define EKG_SAMPLE_RATE       100    /* Hz */
#define EKG_MIN_SAMPLES       300    /* 3 seconds minimum (spec requirement) */
#define EKG_MAX_SAMPLES       1000   /* 10 seconds maximum (spec requirement) */
#define EKG_ANALYSIS_WINDOW   500    /* 5 seconds recommended */

/* ========================================
 * QRS DETECTION (Pan-Tompkins Algorithm)
 * Fixed algorithm parameters
 * ======================================== */
#define QRS_REFRACTORY_MS     120    /* ms - allows detection of VT at up to 250 bpm */
#define QRS_REFRACTORY_SAMPLES (QRS_REFRACTORY_MS * EKG_SAMPLE_RATE / 1000)
#define QRS_INTEGRATION_MS    150    /* ms */
#define QRS_INTEGRATION_SAMPLES (QRS_INTEGRATION_MS * EKG_SAMPLE_RATE / 1000)
#define QRS_SEARCHBACK_MS     1660   /* ms (corresponding to 36 bpm) */
#define QRS_SEARCHBACK_SAMPLES (QRS_SEARCHBACK_MS * EKG_SAMPLE_RATE / 1000)
#define QRS_INIT_SPKI_FACTOR  0.25f  /* Signal peak initial estimate */
#define QRS_INIT_NPKI_FACTOR  0.5f   /* Noise peak initial estimate */
#define QRS_THRESHOLD_FACTOR  0.25f  /* Threshold = NPKI + factor*(SPKI-NPKI) */
#define QRS_LEARNING_RATE     0.125f /* Learning rate for adaptive thresholds */
#define QRS_MAX_DETECTIONS    50     /* Maximum QRS complexes to detect */

/* ========================================
 * BANDPASS FILTER COEFFICIENTS
 * 5-18 Hz at 100 Hz sample rate
 * 2nd order Butterworth design - fixed
 * ======================================== */
/* High-pass 5 Hz */
#define HP_B0  0.9150f
#define HP_B1 -1.8299f
#define HP_B2  0.9150f
#define HP_A1 -1.8227f
#define HP_A2  0.8372f
/* Low-pass 18 Hz */
#define LP_B0  0.2452f
#define LP_B1  0.4905f
#define LP_B2  0.2452f
#define LP_A1 -0.2097f
#define LP_A2  0.1906f
#define FILTER_STATE_SIZE 8

/* ========================================
 * GOERTZEL FREQUENCIES FOR VF DETECTION
 * 0.5 Hz resolution, 2.5-12 Hz range
 * ======================================== */
#define VF_NUM_TEST_FREQS     20
static const float VF_TEST_FREQS[VF_NUM_TEST_FREQS] = {
    2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.0f,
    7.5f, 8.0f, 8.5f, 9.0f, 9.5f, 10.0f, 10.5f, 11.0f, 11.5f, 12.0f
};

/* Mathematical constants */
#ifndef PI
#define PI 3.14159265358979323846f
#endif

/* ========================================
 * ERROR CODES
 * ======================================== */
#define EKG_SUCCESS           0
#define EKG_ERROR_NULL_PTR   -1
#define EKG_ERROR_INVALID_LEN -2
#define EKG_ERROR_LOW_QUALITY -3
#define EKG_ERROR_SATURATED   -4
```

---

## New Config Struct Organization

Organized into logical sections with preserved comments:

```c
/**
 * Runtime Configuration Structure
 *
 * Allows dynamic parameter tuning for training/optimization.
 * If NULL is passed to ekg_analyze(), EKG_DEFAULT_CONFIG is used.
 */
typedef struct {
    /* ========================================
     * VT DETECTION PARAMETERS
     * ======================================== */
    float vt_hr_threshold;           /* bpm - heart rate threshold for tachycardia */
    float vt_qrs_width_ms;           /* ms - QRS width threshold for wide complex */
    float vt_regularity_cv;          /* percent - CV threshold for regular rhythm */
    float vt_confidence_threshold;   /* 0.0-1.0 - minimum confidence for VT diagnosis */
    uint8_t vt_min_consecutive;      /* minimum consecutive wide QRS for sustained VT */

    /* VT Bayesian Scoring Boundaries */
    float vt_hr_high_thresh;         /* bpm - HR threshold for additional VT evidence */
    float vt_qrs_vwide_thresh;       /* ms - very wide QRS threshold */
    float vt_vregular_cv_thresh;     /* percent - very regular rhythm threshold */

    /* ========================================
     * VT BAYESIAN LOG-LIKELIHOOD RATIOS
     * LR+ when feature present, LR- when absent
     * Values in log space for numerical stability
     * ======================================== */
    float lr_vt_hr_thresh_pos;       /* LR+ when HR > threshold */
    float lr_vt_hr_thresh_neg;       /* LR- when HR <= threshold */
    float lr_vt_hr_high_pos;         /* LR+ when HR > high threshold */
    float lr_vt_hr_high_neg;         /* LR- when HR <= high threshold */
    float lr_vt_qrs_wide_pos;        /* LR+ when QRS wide */
    float lr_vt_qrs_wide_neg;        /* LR- when QRS narrow */
    float lr_vt_qrs_vwide_pos;       /* LR+ when QRS very wide (>140ms) */
    float lr_vt_qrs_vwide_neg;       /* LR- when QRS not very wide */
    float lr_vt_regular_pos;         /* LR+ when rhythm regular */
    float lr_vt_regular_neg;         /* LR- when rhythm irregular */
    float lr_vt_vregular_pos;        /* LR+ when rhythm very regular (CV<10%) */
    float lr_vt_vregular_neg;        /* LR- when rhythm not very regular */
    float lr_vt_sustained_pos;       /* LR+ when sustained (consecutive beats) */
    float lr_vt_sustained_neg;       /* LR- when not sustained */

    /* ========================================
     * VF DETECTION PARAMETERS
     * ======================================== */
    /* Frequency Analysis */
    float vf_freq_min;               /* Hz - minimum VF frequency (typical: 3.5) */
    float vf_freq_max;               /* Hz - maximum VF frequency, extended for fast VF */
    float vf_dominance_threshold;    /* 0.0-1.0 - VF is chaotic, no single freq dominates */

    /* Amplitude Analysis */
    float vf_noise_floor_mult;       /* VF threshold = noise_floor * this multiplier */
    float vf_amplitude_thresh_ratio; /* ratio of peak for threshold crossings */

    /* Decision Thresholds */
    float vf_evidence_threshold;     /* minimum evidence score for VF detection */
    float vf_confidence_threshold;   /* minimum confidence for shock decision */
    float vf_qrs_rate_threshold;     /* QRS per second - below = QRS absent */

    /* Penalty Frequency Range (for QRS penalty exemption) */
    float vf_penalty_freq_min;       /* Hz - min freq for penalty exemption */
    float vf_penalty_freq_max;       /* Hz - max freq for penalty exemption */

    /* ========================================
     * VF EVIDENCE AMPLITUDE THRESHOLDS
     * Tiered scoring based on signal amplitude
     * ======================================== */
    float vf_amp_high_thresh;        /* amplitude threshold for high evidence */
    float vf_amp_medium_thresh;      /* amplitude threshold for medium evidence */
    float vf_amp_low_thresh;         /* amplitude threshold for low evidence */
    float vf_amp_vlow_thresh;        /* amplitude threshold for very low evidence */

    /* ========================================
     * VF IRREGULARITY THRESHOLDS
     * Zero-crossing rate (ZCR) and threshold crossing rate (TCR)
     * ======================================== */
    float vf_zcr_high_thresh;        /* ZCR threshold for high irregularity */
    float vf_zcr_mod_thresh;         /* ZCR threshold for moderate irregularity */
    float vf_tcr_high_thresh;        /* TCR threshold for high irregularity */
    float vf_tcr_mod_thresh;         /* TCR threshold for moderate irregularity */

    /* ========================================
     * VF ENTROPY THRESHOLDS
     * Higher entropy indicates more chaotic signal
     * ======================================== */
    float vf_entropy_high_thresh;    /* entropy threshold for high evidence */
    float vf_entropy_mod_thresh;     /* entropy threshold for moderate evidence */

    /* ========================================
     * VF SPECTRAL FLATNESS THRESHOLDS
     * VF is spectrally flat (chaotic)
     * Organized rhythms have peaked spectrum
     * ======================================== */
    float vf_flatness_high_thresh;   /* high flatness - clearly chaotic */
    float vf_flatness_mod_thresh;    /* moderate flatness */

    /* ========================================
     * VF ORGANIZED RHYTHM DETECTION
     * Prevents false positives on organized tachycardias
     * ======================================== */
    float vf_org_hr_min;             /* min HR for organized tachycardia detection */
    float vf_org_hr_max;             /* max HR for organized tachycardia detection */
    float vf_org_cv_thresh;          /* CV threshold for regular rhythm detection */
    float vf_qrs_rate_penalty_thresh;/* QRS rate threshold for applying penalty */

    /* ========================================
     * VF EVIDENCE SCORING WEIGHTS
     * Positive values add evidence for VF
     * Negative values (penalties) reduce evidence
     * ======================================== */
    /* Amplitude weights */
    float vf_ev_amp_high;            /* weight when amplitude > high_thresh */
    float vf_ev_amp_medium;          /* weight when amplitude > medium_thresh */
    float vf_ev_amp_low;             /* weight when amplitude > low_thresh */
    float vf_ev_amp_vlow;            /* weight when amplitude > vlow_thresh */

    /* Frequency weights */
    float vf_ev_freq_optimal;        /* weight for optimal VF range (5-8 Hz) */
    float vf_ev_freq_good;           /* weight for good VF range (configurable) */
    float vf_ev_freq_edge;           /* weight for edge VF range (2.5-12 Hz) */

    /* Irregularity weights */
    float vf_ev_qrs_absent;          /* weight when no QRS detected */
    float vf_ev_high_irreg;          /* weight for high irregularity */
    float vf_ev_mod_irreg;           /* weight for moderate irregularity */

    /* Entropy weights */
    float vf_ev_entropy_high;        /* weight for high entropy */
    float vf_ev_entropy_mod;         /* weight for moderate entropy */

    /* Spectral flatness weights - kept low to avoid overwhelming on NSR/SVT */
    float vf_ev_flatness_high;       /* weight for high spectral flatness */
    float vf_ev_flatness_mod;        /* weight for moderate spectral flatness */

    /* Organized rhythm penalties */
    float vf_ev_organized_penalty;   /* penalty when organized tachycardia detected */
    float vf_ev_qrs_freq_penalty;    /* penalty when QRS present + freq outside VF range */
    float vf_ev_qrs_regular_penalty; /* penalty when QRS present + regular RR intervals */

    /* ========================================
     * SIGNAL QUALITY PARAMETERS
     * ======================================== */
    float snr_good_threshold;        /* SNR threshold for good signal quality */
    float signal_quality_min;        /* minimum acceptable signal quality (0.0-1.0) */

} ekg_config_t;
```

---

## Default Config Initialization

```c
static const ekg_config_t EKG_DEFAULT_CONFIG = {
    /* VT Detection */
    .vt_hr_threshold = 120.0f,          /* bpm */
    .vt_qrs_width_ms = 100.0f,          /* ms */
    .vt_regularity_cv = 25.0f,          /* percent */
    .vt_confidence_threshold = 0.70f,
    .vt_min_consecutive = 3,

    /* VT Bayesian Boundaries */
    .vt_hr_high_thresh = 150.0f,
    .vt_qrs_vwide_thresh = 140.0f,
    .vt_vregular_cv_thresh = 10.0f,

    /* VT Bayesian Log-Likelihood Ratios */
    .lr_vt_hr_thresh_pos = 1.10f,
    .lr_vt_hr_thresh_neg = -1.20f,
    .lr_vt_hr_high_pos = 0.69f,
    .lr_vt_hr_high_neg = -0.36f,
    .lr_vt_qrs_wide_pos = 1.39f,
    .lr_vt_qrs_wide_neg = -1.61f,
    .lr_vt_qrs_vwide_pos = 0.92f,
    .lr_vt_qrs_vwide_neg = -0.51f,
    .lr_vt_regular_pos = 1.10f,
    .lr_vt_regular_neg = -0.92f,
    .lr_vt_vregular_pos = 0.69f,
    .lr_vt_vregular_neg = -0.36f,
    .lr_vt_sustained_pos = 0.92f,
    .lr_vt_sustained_neg = -0.69f,

    /* VF Detection */
    .vf_freq_min = 3.5f,
    .vf_freq_max = 9.5f,
    .vf_dominance_threshold = 0.40f,
    .vf_noise_floor_mult = 3.0f,
    .vf_amplitude_thresh_ratio = 0.05f,
    .vf_evidence_threshold = 3.0f,
    .vf_confidence_threshold = 0.50f,
    .vf_qrs_rate_threshold = 0.5f,
    .vf_penalty_freq_min = 2.5f,
    .vf_penalty_freq_max = 12.0f,

    /* VF Amplitude Thresholds */
    .vf_amp_high_thresh = 300.0f,
    .vf_amp_medium_thresh = 200.0f,
    .vf_amp_low_thresh = 100.0f,
    .vf_amp_vlow_thresh = 50.0f,

    /* VF Irregularity Thresholds */
    .vf_zcr_high_thresh = 0.25f,
    .vf_zcr_mod_thresh = 0.15f,
    .vf_tcr_high_thresh = 0.5f,
    .vf_tcr_mod_thresh = 0.3f,

    /* VF Entropy Thresholds */
    .vf_entropy_high_thresh = 0.6f,
    .vf_entropy_mod_thresh = 0.4f,

    /* VF Spectral Flatness */
    .vf_flatness_high_thresh = 0.7f,
    .vf_flatness_mod_thresh = 0.5f,

    /* VF Organized Rhythm Detection */
    .vf_org_hr_min = 120.0f,
    .vf_org_hr_max = 220.0f,
    .vf_org_cv_thresh = 15.0f,
    .vf_qrs_rate_penalty_thresh = 0.70f,

    /* VF Evidence Weights - Amplitude */
    .vf_ev_amp_high = 2.0f,
    .vf_ev_amp_medium = 1.5f,
    .vf_ev_amp_low = 1.0f,
    .vf_ev_amp_vlow = 0.5f,

    /* VF Evidence Weights - Frequency */
    .vf_ev_freq_optimal = 2.0f,
    .vf_ev_freq_good = 1.5f,
    .vf_ev_freq_edge = 0.5f,

    /* VF Evidence Weights - Irregularity */
    .vf_ev_qrs_absent = 2.0f,
    .vf_ev_high_irreg = 1.5f,
    .vf_ev_mod_irreg = 0.5f,

    /* VF Evidence Weights - Entropy */
    .vf_ev_entropy_high = 1.5f,
    .vf_ev_entropy_mod = 0.5f,

    /* VF Evidence Weights - Spectral Flatness */
    .vf_ev_flatness_high = 0.5f,
    .vf_ev_flatness_mod = 0.25f,

    /* VF Evidence Weights - Penalties */
    .vf_ev_organized_penalty = -3.0f,
    .vf_ev_qrs_freq_penalty = -1.5f,
    .vf_ev_qrs_regular_penalty = -0.5f,

    /* Signal Quality */
    .snr_good_threshold = 10.0f,
    .signal_quality_min = 0.5f
};
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/ekg_config.h` | Reorganize: keep spec constants as #defines, add 34 new config fields, remove 3 unused, remove redundant #defines, preserve comments |
| `src/vf_detector.c` | Replace 18 `VF_EV_*` + 1 `VF_QRS_RATE_THRESHOLD` with config access |
| `src/vt_detector.c` | Replace 14 `LR_VT_*` + 1 `VT_MIN_CONSECUTIVE` with config access |
| `src/signal_utils.h` | Add config parameter to `estimate_signal_quality()` |
| `src/signal_utils.c` | Add config parameter, replace 2 defines with config access |
| `tools/ekg_train.c` | Update `write_best_config_header()` per plan2.md, add all new fields |
| `tests/test_vf_detector.c` | Update test assertions (lines 149, 265) to use config values |

---

## Function Signature Change

### `estimate_signal_quality()` in signal_utils.c/h

**Current:**
```c
float estimate_signal_quality(const float *samples, size_t num_samples, float sample_rate);
```

**New:**
```c
float estimate_signal_quality(const float *samples, size_t num_samples,
                              float sample_rate, const ekg_config_t *config);
```

**Call site (vf_detector.c:694):**
```c
signal_quality = estimate_signal_quality(samples, num_samples, sample_rate, config);
```

---

## Update ekg_train.c Output Format

Replace `write_best_config_header()` (lines 933-998) to output a config struct to `best_config.h`:

```c
/* Write best config header file */
static int write_best_config_header(const char *filename, const ekg_config_t *config,
                                    const metrics_t *metrics)
{
    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        return -1;
    }

    fprintf(f, "/* Generated by ekg_train */\n");
    fprintf(f, "/*\n");
    fprintf(f, " * PERFORMANCE METRICS:\n");
    fprintf(f, " *   Accuracy:    %.1f%%\n", metrics->accuracy * 100.0f);
    fprintf(f, " *   Sensitivity: %.1f%%\n", metrics->sensitivity * 100.0f);
    fprintf(f, " *   Specificity: %.1f%%\n", metrics->specificity * 100.0f);
    fprintf(f, " *   Precision:   %.1f%%\n", metrics->precision * 100.0f);
    fprintf(f, " *   F1-Score:    %.1f%%\n", metrics->f1_score * 100.0f);
    fprintf(f, " *   F2-Score:    %.1f%%\n", metrics->f2_score * 100.0f);
    fprintf(f, " *   Confusion:   TP=%u TN=%u FP=%u FN=%u\n",
            metrics->tp, metrics->tn, metrics->fp, metrics->fn);
    fprintf(f, " */\n\n");
    fprintf(f, "#ifndef EKG_OPTIMIZED_CONFIG_H\n");
    fprintf(f, "#define EKG_OPTIMIZED_CONFIG_H\n\n");
    fprintf(f, "#include \"ekg_config.h\"\n\n");
    fprintf(f, "static const ekg_config_t EKG_OPTIMIZED_CONFIG = {\n");

    /* VT Detection */
    fprintf(f, "    /* VT Detection */\n");
    fprintf(f, "    .vt_hr_threshold = %.1ff,\n", config->vt_hr_threshold);
    fprintf(f, "    .vt_qrs_width_ms = %.1ff,\n", config->vt_qrs_width_ms);
    fprintf(f, "    .vt_regularity_cv = %.1ff,\n", config->vt_regularity_cv);
    fprintf(f, "    .vt_confidence_threshold = %.2ff,\n", config->vt_confidence_threshold);
    fprintf(f, "    .vt_min_consecutive = %u,\n", config->vt_min_consecutive);

    /* VT Bayesian Boundaries */
    fprintf(f, "\n    /* VT Bayesian Boundaries */\n");
    fprintf(f, "    .vt_hr_high_thresh = %.1ff,\n", config->vt_hr_high_thresh);
    fprintf(f, "    .vt_qrs_vwide_thresh = %.1ff,\n", config->vt_qrs_vwide_thresh);
    fprintf(f, "    .vt_vregular_cv_thresh = %.1ff,\n", config->vt_vregular_cv_thresh);

    /* VT Bayesian Log-Likelihood Ratios */
    fprintf(f, "\n    /* VT Bayesian Log-Likelihood Ratios */\n");
    fprintf(f, "    .lr_vt_hr_thresh_pos = %.2ff,\n", config->lr_vt_hr_thresh_pos);
    fprintf(f, "    .lr_vt_hr_thresh_neg = %.2ff,\n", config->lr_vt_hr_thresh_neg);
    fprintf(f, "    .lr_vt_hr_high_pos = %.2ff,\n", config->lr_vt_hr_high_pos);
    fprintf(f, "    .lr_vt_hr_high_neg = %.2ff,\n", config->lr_vt_hr_high_neg);
    fprintf(f, "    .lr_vt_qrs_wide_pos = %.2ff,\n", config->lr_vt_qrs_wide_pos);
    fprintf(f, "    .lr_vt_qrs_wide_neg = %.2ff,\n", config->lr_vt_qrs_wide_neg);
    fprintf(f, "    .lr_vt_qrs_vwide_pos = %.2ff,\n", config->lr_vt_qrs_vwide_pos);
    fprintf(f, "    .lr_vt_qrs_vwide_neg = %.2ff,\n", config->lr_vt_qrs_vwide_neg);
    fprintf(f, "    .lr_vt_regular_pos = %.2ff,\n", config->lr_vt_regular_pos);
    fprintf(f, "    .lr_vt_regular_neg = %.2ff,\n", config->lr_vt_regular_neg);
    fprintf(f, "    .lr_vt_vregular_pos = %.2ff,\n", config->lr_vt_vregular_pos);
    fprintf(f, "    .lr_vt_vregular_neg = %.2ff,\n", config->lr_vt_vregular_neg);
    fprintf(f, "    .lr_vt_sustained_pos = %.2ff,\n", config->lr_vt_sustained_pos);
    fprintf(f, "    .lr_vt_sustained_neg = %.2ff,\n", config->lr_vt_sustained_neg);

    /* VF Detection */
    fprintf(f, "\n    /* VF Detection */\n");
    fprintf(f, "    .vf_freq_min = %.1ff,\n", config->vf_freq_min);
    fprintf(f, "    .vf_freq_max = %.1ff,\n", config->vf_freq_max);
    fprintf(f, "    .vf_dominance_threshold = %.2ff,\n", config->vf_dominance_threshold);
    fprintf(f, "    .vf_noise_floor_mult = %.1ff,\n", config->vf_noise_floor_mult);
    fprintf(f, "    .vf_amplitude_thresh_ratio = %.2ff,\n", config->vf_amplitude_thresh_ratio);
    fprintf(f, "    .vf_evidence_threshold = %.1ff,\n", config->vf_evidence_threshold);
    fprintf(f, "    .vf_confidence_threshold = %.2ff,\n", config->vf_confidence_threshold);
    fprintf(f, "    .vf_qrs_rate_threshold = %.2ff,\n", config->vf_qrs_rate_threshold);
    fprintf(f, "    .vf_penalty_freq_min = %.1ff,\n", config->vf_penalty_freq_min);
    fprintf(f, "    .vf_penalty_freq_max = %.1ff,\n", config->vf_penalty_freq_max);

    /* VF Amplitude Thresholds */
    fprintf(f, "\n    /* VF Amplitude Thresholds */\n");
    fprintf(f, "    .vf_amp_high_thresh = %.1ff,\n", config->vf_amp_high_thresh);
    fprintf(f, "    .vf_amp_medium_thresh = %.1ff,\n", config->vf_amp_medium_thresh);
    fprintf(f, "    .vf_amp_low_thresh = %.1ff,\n", config->vf_amp_low_thresh);
    fprintf(f, "    .vf_amp_vlow_thresh = %.1ff,\n", config->vf_amp_vlow_thresh);

    /* VF Irregularity Thresholds */
    fprintf(f, "\n    /* VF Irregularity Thresholds */\n");
    fprintf(f, "    .vf_zcr_high_thresh = %.2ff,\n", config->vf_zcr_high_thresh);
    fprintf(f, "    .vf_zcr_mod_thresh = %.2ff,\n", config->vf_zcr_mod_thresh);
    fprintf(f, "    .vf_tcr_high_thresh = %.2ff,\n", config->vf_tcr_high_thresh);
    fprintf(f, "    .vf_tcr_mod_thresh = %.2ff,\n", config->vf_tcr_mod_thresh);

    /* VF Entropy Thresholds */
    fprintf(f, "\n    /* VF Entropy Thresholds */\n");
    fprintf(f, "    .vf_entropy_high_thresh = %.2ff,\n", config->vf_entropy_high_thresh);
    fprintf(f, "    .vf_entropy_mod_thresh = %.2ff,\n", config->vf_entropy_mod_thresh);

    /* VF Spectral Flatness */
    fprintf(f, "\n    /* VF Spectral Flatness */\n");
    fprintf(f, "    .vf_flatness_high_thresh = %.2ff,\n", config->vf_flatness_high_thresh);
    fprintf(f, "    .vf_flatness_mod_thresh = %.2ff,\n", config->vf_flatness_mod_thresh);

    /* VF Organized Rhythm Detection */
    fprintf(f, "\n    /* VF Organized Rhythm Detection */\n");
    fprintf(f, "    .vf_org_hr_min = %.1ff,\n", config->vf_org_hr_min);
    fprintf(f, "    .vf_org_hr_max = %.1ff,\n", config->vf_org_hr_max);
    fprintf(f, "    .vf_org_cv_thresh = %.1ff,\n", config->vf_org_cv_thresh);
    fprintf(f, "    .vf_qrs_rate_penalty_thresh = %.2ff,\n", config->vf_qrs_rate_penalty_thresh);

    /* VF Evidence Weights - Amplitude */
    fprintf(f, "\n    /* VF Evidence Weights - Amplitude */\n");
    fprintf(f, "    .vf_ev_amp_high = %.2ff,\n", config->vf_ev_amp_high);
    fprintf(f, "    .vf_ev_amp_medium = %.2ff,\n", config->vf_ev_amp_medium);
    fprintf(f, "    .vf_ev_amp_low = %.2ff,\n", config->vf_ev_amp_low);
    fprintf(f, "    .vf_ev_amp_vlow = %.2ff,\n", config->vf_ev_amp_vlow);

    /* VF Evidence Weights - Frequency */
    fprintf(f, "\n    /* VF Evidence Weights - Frequency */\n");
    fprintf(f, "    .vf_ev_freq_optimal = %.2ff,\n", config->vf_ev_freq_optimal);
    fprintf(f, "    .vf_ev_freq_good = %.2ff,\n", config->vf_ev_freq_good);
    fprintf(f, "    .vf_ev_freq_edge = %.2ff,\n", config->vf_ev_freq_edge);

    /* VF Evidence Weights - Irregularity */
    fprintf(f, "\n    /* VF Evidence Weights - Irregularity */\n");
    fprintf(f, "    .vf_ev_qrs_absent = %.2ff,\n", config->vf_ev_qrs_absent);
    fprintf(f, "    .vf_ev_high_irreg = %.2ff,\n", config->vf_ev_high_irreg);
    fprintf(f, "    .vf_ev_mod_irreg = %.2ff,\n", config->vf_ev_mod_irreg);

    /* VF Evidence Weights - Entropy */
    fprintf(f, "\n    /* VF Evidence Weights - Entropy */\n");
    fprintf(f, "    .vf_ev_entropy_high = %.2ff,\n", config->vf_ev_entropy_high);
    fprintf(f, "    .vf_ev_entropy_mod = %.2ff,\n", config->vf_ev_entropy_mod);

    /* VF Evidence Weights - Spectral Flatness */
    fprintf(f, "\n    /* VF Evidence Weights - Spectral Flatness */\n");
    fprintf(f, "    .vf_ev_flatness_high = %.2ff,\n", config->vf_ev_flatness_high);
    fprintf(f, "    .vf_ev_flatness_mod = %.2ff,\n", config->vf_ev_flatness_mod);

    /* VF Evidence Weights - Penalties */
    fprintf(f, "\n    /* VF Evidence Weights - Penalties */\n");
    fprintf(f, "    .vf_ev_organized_penalty = %.2ff,\n", config->vf_ev_organized_penalty);
    fprintf(f, "    .vf_ev_qrs_freq_penalty = %.2ff,\n", config->vf_ev_qrs_freq_penalty);
    fprintf(f, "    .vf_ev_qrs_regular_penalty = %.2ff,\n", config->vf_ev_qrs_regular_penalty);

    /* Signal Quality */
    fprintf(f, "\n    /* Signal Quality */\n");
    fprintf(f, "    .snr_good_threshold = %.1ff,\n", config->snr_good_threshold);
    fprintf(f, "    .signal_quality_min = %.2ff\n", config->signal_quality_min);

    fprintf(f, "};\n\n");
    fprintf(f, "#endif /* EKG_OPTIMIZED_CONFIG_H */\n");

    fclose(f);
    return 0;
}
```

### Usage After Change

The output file `best_config.h` can be:
1. Included and used directly: `ekg_analyze(samples, len, &EKG_OPTIMIZED_CONFIG, &result)`
2. Contents copied into `EKG_DEFAULT_CONFIG` in `ekg_config.h`

---

## Verification

1. `make clean && make` - Ensure compilation succeeds
2. `make test` - Run test suite to verify no regressions
3. `./build/ekg_eval --dataset test/data/combined.csv` - Verify accuracy unchanged
4. `./build/ekg_train -n 10 data/` - Verify training outputs struct format in `best_config.h`
