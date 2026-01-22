/**
 * EKG Signal Analyzer - Configuration Header
 *
 * Contains all constants, thresholds, and error codes for the EKG analyzer.
 *
 * SAFETY NOTICE: This is an educational/prototype implementation.
 * NOT for medical use. NOT certified for patient care.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#ifndef EKG_CONFIG_H
#define EKG_CONFIG_H

#include <stdint.h>

/* Sample rate */
#define EKG_SAMPLE_RATE       100    /* Hz */

/* Input constraints - per specification section 4.3 */
#define EKG_MIN_SAMPLES       300    /* 3 seconds minimum (spec requirement) */
#define EKG_MAX_SAMPLES       1000   /* 10 seconds maximum (spec requirement) */
#define EKG_ANALYSIS_WINDOW   500    /* 5 seconds recommended */

/* QRS Detection (Pan-Tompkins) */
#define QRS_REFRACTORY_MS     120    /* ms - allows detection of VT at up to 250 bpm */
#define QRS_REFRACTORY_SAMPLES (QRS_REFRACTORY_MS * EKG_SAMPLE_RATE / 1000)  /* 20 */
#define QRS_INTEGRATION_MS    150    /* ms */
#define QRS_INTEGRATION_SAMPLES (QRS_INTEGRATION_MS * EKG_SAMPLE_RATE / 1000)  /* 15 */
#define QRS_SEARCHBACK_MS     1660   /* ms (corresponding to 36 bpm) */
#define QRS_SEARCHBACK_SAMPLES (QRS_SEARCHBACK_MS * EKG_SAMPLE_RATE / 1000)  /* 166 */
#define QRS_INIT_SPKI_FACTOR  0.25f  /* Signal peak initial estimate */
#define QRS_INIT_NPKI_FACTOR  0.5f   /* Noise peak initial estimate */
#define QRS_THRESHOLD_FACTOR  0.25f  /* Threshold = NPKI + factor*(SPKI-NPKI) */
#define QRS_LEARNING_RATE     0.125f /* Learning rate for adaptive thresholds */
#define QRS_MAX_DETECTIONS    50     /* Maximum QRS complexes to detect */

/* VT Detection */
#define VT_HR_THRESHOLD       120.0f  /* bpm */
#define VT_QRS_WIDTH_MS       100.0f  /* ms */
#define VT_QRS_WIDTH_SAMPLES  (VT_QRS_WIDTH_MS * EKG_SAMPLE_RATE / 1000.0f)  /* 10 */
#define VT_REGULARITY_CV      25.0f   /* percent */
#define VT_MIN_CONSECUTIVE    3       /* Minimum consecutive wide QRS */
#define VT_CONFIDENCE_THRESHOLD 0.70f  /* Confidence threshold for VT */

/* VF Detection */
#define VF_FREQ_MIN           4.5f    /* Hz */
#define VF_FREQ_MAX           9.5f   /* Hz - extended for fast VF */
#define VF_ZCR_THRESHOLD      0.25f   /* Zero-crossing rate threshold */
#define VF_QRS_RATE_THRESHOLD 0.5f    /* QRS per second (below = absent) */
#define VF_CONFIDENCE_THRESHOLD 0.95f  /* Confidence threshold */
#define VF_MIN_AMPLITUDE      200.0f   /* Minimum amplitude - lowered to catch more VF */
#define VF_DOMINANCE_THRESHOLD 0.40f  /* VF is chaotic - no single freq dominates */
#define VF_FINE_AMPLITUDE_MAX 250.0f  /* Extended fine VF detection window */
#define VF_FINE_CONFIDENCE    0.60f   /* Lower confidence for fine VF detection */
#define VF_NOISE_FLOOR_MULT   3.0f    /* VF threshold = noise_floor * this multiplier */
#define VF_AMPLITUDE_THRESHOLD_RATIO 0.05f  /* Ratio of peak for threshold crossings */
#define VF_EVIDENCE_THRESHOLD 3.0f    /* Minimum evidence score for VF detection */

/* Goertzel frequencies for VF detection - 0.5 Hz resolution, extended range 2.5-12 Hz */
#define VF_NUM_TEST_FREQS     20
static const float VF_TEST_FREQS[VF_NUM_TEST_FREQS] = {
    2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.0f,
    7.5f, 8.0f, 8.5f, 9.0f, 9.5f, 10.0f, 10.5f, 11.0f, 11.5f, 12.0f
};

/* Bandpass filter coefficients (5-18 Hz at 100 Hz sample rate) */
/* High-pass 5 Hz (2nd order Butterworth) */
#define HP_B0  0.9150f
#define HP_B1 -1.8299f
#define HP_B2  0.9150f
#define HP_A1 -1.8227f
#define HP_A2  0.8372f

/* Low-pass 18 Hz (2nd order Butterworth) */
#define LP_B0  0.2452f
#define LP_B1  0.4905f
#define LP_B2  0.2452f
#define LP_A1 -0.2097f
#define LP_A2  0.1906f

/* Filter state size */
#define FILTER_STATE_SIZE 8

/* Error codes */
#define EKG_SUCCESS           0
#define EKG_ERROR_NULL_PTR   -1
#define EKG_ERROR_INVALID_LEN -2
#define EKG_ERROR_LOW_QUALITY -3
#define EKG_ERROR_SATURATED   -4

/* Mathematical constants */
#ifndef PI
#define PI 3.14159265358979323846f
#endif

/**
 * Runtime Configuration Structure
 *
 * Allows dynamic parameter tuning for training/optimization.
 * If NULL is passed to ekg_analyze(), EKG_DEFAULT_CONFIG is used.
 */
typedef struct {
    /* VT Detection Parameters */
    float vt_hr_threshold;         /* bpm */
    float vt_qrs_width_ms;         /* ms */
    float vt_regularity_cv;        /* percent */
    float vt_confidence_threshold; /* 0.0-1.0 */

    /* VF Detection Parameters */
    float vf_freq_min;             /* Hz */
    float vf_freq_max;             /* Hz */
    float vf_zcr_threshold;        /* zero-crossing rate */
    float vf_confidence_threshold; /* 0.0-1.0 */
    float vf_min_amplitude;        /* minimum amplitude */

    /* Fine VF Detection Parameters (for improved sensitivity) */
    float vf_dominance_threshold;  /* Freq dominance ratio (0.0-1.0) */
    float vf_fine_amplitude_max;   /* Max amplitude for fine VF path */
    float vf_fine_confidence;      /* Lower confidence for fine VF */
    float vf_noise_floor_mult;     /* Noise floor multiplier for adaptive threshold */
    float vf_amplitude_thresh_ratio; /* Ratio of peak for threshold crossings */

    /* Evidence Scoring Parameters */
    float vf_evidence_threshold;   /* Minimum evidence score for VF detection */

    /* Evidence Amplitude Thresholds (for scoring tiers) */
    float vf_amp_high_thresh;      /* Amplitude for high evidence (default: 300) */
    float vf_amp_medium_thresh;    /* Amplitude for medium evidence (default: 200) */
    float vf_amp_low_thresh;       /* Amplitude for low evidence (default: 100) */
    float vf_amp_vlow_thresh;      /* Amplitude for very low evidence (default: 50) */

    /* Irregularity Thresholds */
    float vf_zcr_high_thresh;      /* ZCR threshold for high irregularity (default: 0.25) */
    float vf_zcr_mod_thresh;       /* ZCR threshold for moderate irregularity (default: 0.15) */
    float vf_tcr_high_thresh;      /* TCR threshold for high irregularity (default: 0.5) */
    float vf_tcr_mod_thresh;       /* TCR threshold for moderate irregularity (default: 0.3) */

    /* Entropy Thresholds */
    float vf_entropy_high_thresh;  /* Entropy for high evidence (default: 0.6) */
    float vf_entropy_mod_thresh;   /* Entropy for moderate evidence (default: 0.4) */

    /* Organized Rhythm Detection */
    float vf_org_hr_min;           /* Min HR for organized tachycardia (default: 120) */
    float vf_org_hr_max;           /* Max HR for organized tachycardia (default: 220) */
    float vf_org_cv_thresh;        /* CV threshold for regular rhythm (default: 15) */

    /* QRS Rate Penalty */
    float vf_qrs_rate_penalty_thresh; /* QRS rate threshold for penalty (default: 0.8) */

    /* VT Bayesian Scoring Boundaries */
    float vt_hr_high_thresh;       /* HR for additional VT evidence (default: 150) */
    float vt_qrs_vwide_thresh;     /* QRS width for very wide (default: 140) */
    float vt_vregular_cv_thresh;   /* CV for very regular (default: 10) */
} ekg_config_t;

/* Default configuration (used when NULL passed to ekg_analyze) */
static const ekg_config_t EKG_DEFAULT_CONFIG = {
    .vt_hr_threshold = VT_HR_THRESHOLD,
    .vt_qrs_width_ms = VT_QRS_WIDTH_MS,
    .vt_regularity_cv = VT_REGULARITY_CV,
    .vt_confidence_threshold = VT_CONFIDENCE_THRESHOLD,
    .vf_freq_min = VF_FREQ_MIN,
    .vf_freq_max = VF_FREQ_MAX,
    .vf_zcr_threshold = VF_ZCR_THRESHOLD,
    .vf_confidence_threshold = VF_CONFIDENCE_THRESHOLD,
    .vf_min_amplitude = VF_MIN_AMPLITUDE,
    .vf_dominance_threshold = VF_DOMINANCE_THRESHOLD,
    .vf_fine_amplitude_max = VF_FINE_AMPLITUDE_MAX,
    .vf_fine_confidence = VF_FINE_CONFIDENCE,
    .vf_noise_floor_mult = VF_NOISE_FLOOR_MULT,
    .vf_amplitude_thresh_ratio = VF_AMPLITUDE_THRESHOLD_RATIO,
    .vf_evidence_threshold = VF_EVIDENCE_THRESHOLD,

    /* Evidence Amplitude Thresholds */
    .vf_amp_high_thresh = 300.0f,
    .vf_amp_medium_thresh = 200.0f,
    .vf_amp_low_thresh = 100.0f,
    .vf_amp_vlow_thresh = 50.0f,

    /* Irregularity Thresholds */
    .vf_zcr_high_thresh = 0.25f,
    .vf_zcr_mod_thresh = 0.15f,
    .vf_tcr_high_thresh = 0.5f,
    .vf_tcr_mod_thresh = 0.3f,

    /* Entropy Thresholds */
    .vf_entropy_high_thresh = 0.6f,
    .vf_entropy_mod_thresh = 0.4f,

    /* Organized Rhythm Detection */
    .vf_org_hr_min = 120.0f,
    .vf_org_hr_max = 220.0f,
    .vf_org_cv_thresh = 15.0f,

    /* QRS Rate Penalty */
    .vf_qrs_rate_penalty_thresh = 0.8f,

    /* VT Bayesian Boundaries */
    .vt_hr_high_thresh = 150.0f,
    .vt_qrs_vwide_thresh = 140.0f,
    .vt_vregular_cv_thresh = 10.0f
};

/* Log-likelihood ratios for Bayesian confidence calculation */
/* LR+ when feature present, LR- when absent (in log space) */

/* VT detection log-LRs */
#define LR_VT_HR_THRESH_POS       1.10f
#define LR_VT_HR_THRESH_NEG      -1.20f
#define LR_VT_HR_HIGH_POS         0.69f   /* HR > 150 */
#define LR_VT_HR_HIGH_NEG        -0.36f
#define LR_VT_QRS_WIDE_POS        1.39f
#define LR_VT_QRS_WIDE_NEG       -1.61f
#define LR_VT_QRS_VWIDE_POS       0.92f   /* QRS > 140ms */
#define LR_VT_QRS_VWIDE_NEG      -0.51f
#define LR_VT_REGULAR_POS         1.10f
#define LR_VT_REGULAR_NEG        -0.92f
#define LR_VT_VREGULAR_POS        0.69f   /* CV < 10% */
#define LR_VT_VREGULAR_NEG       -0.36f
#define LR_VT_SUSTAINED_POS       0.92f
#define LR_VT_SUSTAINED_NEG      -0.69f

/* Signal quality */
#define SNR_GOOD_THRESHOLD       10.0f
#define SIGNAL_QUALITY_MIN        0.5f

/* ========================================
 * Evidence Scoring Weights
 * ======================================== */

/* Amplitude weights */
#define VF_EV_AMP_HIGH            2.0f   /* amplitude > 300 */
#define VF_EV_AMP_MEDIUM          1.5f   /* amplitude > 200 */
#define VF_EV_AMP_LOW             1.0f   /* amplitude > 100 */
#define VF_EV_AMP_VLOW            0.5f   /* amplitude > 50 */

/* Frequency weights */
#define VF_EV_FREQ_OPTIMAL        2.0f   /* 5-8 Hz */
#define VF_EV_FREQ_GOOD           1.5f   /* 3-10.5 Hz (configurable range) */
#define VF_EV_FREQ_EDGE           0.5f   /* 2.5-12 Hz */

/* Irregularity weights */
#define VF_EV_QRS_ABSENT          2.0f   /* no QRS detected */
#define VF_EV_HIGH_IRREG          1.5f   /* ZCR > 0.25 or TCR > 0.5 */
#define VF_EV_MOD_IRREG           0.5f   /* ZCR > 0.15 or TCR > 0.3 */

/* Entropy weights */
#define VF_EV_ENTROPY_HIGH        1.5f   /* entropy > 0.6 */
#define VF_EV_ENTROPY_MOD         0.5f   /* entropy > 0.4 */

/* Organized rhythm penalties */
#define VF_EV_ORGANIZED_PENALTY  -3.0f   /* is_organized_tachy */
#define VF_EV_QRS_PRESENT_PENALTY -3.0f  /* qrs_rate > 0.8 */

#endif /* EKG_CONFIG_H */
