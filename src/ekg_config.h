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

/* ========================================
 * SPECIFICATION CONSTANTS
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
    float vt_hr_thresh;           /* bpm - heart rate threshold for tachycardia */
    float vt_qrs_width_ms;           /* ms - QRS width threshold for wide complex */
    float vt_regularity_cv;          /* percent - CV threshold for regular rhythm */
    float vt_confidence_thresh;   /* 0.0-1.0 - minimum confidence for VT diagnosis */
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
    float vf_dominance_thresh;    /* 0.0-1.0 - VF is chaotic, no single freq dominates */

    /* Amplitude Analysis */
    float vf_noise_floor_mult;       /* VF threshold = noise_floor * this multiplier */
    float vf_tcr_thresh_ratio; /* ratio of peak for threshold crossings */

    /* Decision Thresholds */
    float vf_evidence_thresh;     /* minimum evidence score for VF detection */
    float vf_confidence_thresh;   /* minimum confidence for shock decision */
    float vf_qrs_rate_thresh;     /* QRS per second - below = QRS absent */

    /* Fine VF Detection */
    float vf_fine_amplitude_max;     /* Max amplitude for fine VF path */

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
    float vf_ev_freq_good;           /* weight for good VF range (configurable range) */
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
    float snr_good_thresh;        /* SNR threshold for good signal quality */
    float signal_quality_min;        /* minimum acceptable signal quality (0.0-1.0) */

} ekg_config_t;

/* Default configuration (used when NULL passed to ekg_analyze) */
static const ekg_config_t EKG_DEFAULT_CONFIG = {
    /* VT Detection */
    .vt_hr_thresh = 120.0f,          /* bpm */
    .vt_qrs_width_ms = 100.0f,          /* ms */
    .vt_regularity_cv = 30.0f,          /* percent */
    .vt_confidence_thresh = 0.70f,
    .vt_min_consecutive = 3,

    /* VT Bayesian Boundaries */
    .vt_hr_high_thresh = 150.0f,
    .vt_qrs_vwide_thresh = 130.0f,
    .vt_vregular_cv_thresh = 12.5f,

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
    .vf_freq_min = 2.5f,
    .vf_freq_max = 9.5f,
    .vf_dominance_thresh = 0.35f,
    .vf_noise_floor_mult = 2.0f,
    .vf_tcr_thresh_ratio = 0.05f,
    .vf_evidence_thresh = 3.0f,
    .vf_confidence_thresh = 0.50f,
    .vf_qrs_rate_thresh = 0.5f,
    .vf_fine_amplitude_max = 300.0f,
    .vf_penalty_freq_min = 2.5f,
    .vf_penalty_freq_max = 12.0f,

    /* VF Amplitude Thresholds */
    .vf_amp_high_thresh = 300.0f,
    .vf_amp_medium_thresh = 175.0f,
    .vf_amp_low_thresh = 150.0f,
    .vf_amp_vlow_thresh = 75.0f,

    /* VF Irregularity Thresholds */
    .vf_zcr_high_thresh = 0.30f,
    .vf_zcr_mod_thresh = 0.25f,
    .vf_tcr_high_thresh = 0.30f,
    .vf_tcr_mod_thresh = 0.30f,

    /* VF Entropy Thresholds */
    .vf_entropy_high_thresh = 0.70f,
    .vf_entropy_mod_thresh = 0.30f,

    /* VF Spectral Flatness */
    .vf_flatness_high_thresh = 0.80f,
    .vf_flatness_mod_thresh = 0.40f,

    /* VF Organized Rhythm Detection */
    .vf_org_hr_min = 100.0f,
    .vf_org_hr_max = 250.0f,
    .vf_org_cv_thresh = 20.0f,
    .vf_qrs_rate_penalty_thresh = 0.60f,

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
    .snr_good_thresh = 10.0f,
    .signal_quality_min = 0.5f
};

#endif /* EKG_CONFIG_H */
