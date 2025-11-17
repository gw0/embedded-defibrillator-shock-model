/**
 * EKG Signal Analyzer - Configuration Header
 *
 * Contains all constants, thresholds, and error codes for the EKG analyzer.
 *
 * SAFETY NOTICE: This is an educational/prototype implementation.
 * NOT for medical use. NOT certified for patient care.
 */

#ifndef EKG_CONFIG_H
#define EKG_CONFIG_H

#include <stdint.h>

/* Sample rate */
#define EKG_SAMPLE_RATE       100    /* Hz */

/* Input constraints - per specification section 4.3 */
#define EKG_MIN_SAMPLES       300    /* 3 seconds minimum (spec requirement) */
#define EKG_MAX_SAMPLES       800    /* 8 seconds maximum (spec requirement) */
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
#define VT_QRS_WIDTH_MS       120.0f  /* ms */
#define VT_QRS_WIDTH_SAMPLES  (VT_QRS_WIDTH_MS * EKG_SAMPLE_RATE / 1000.0f)  /* 12 */
#define VT_REGULARITY_CV      20.0f   /* percent (CV < 20% is regular) */
#define VT_MIN_CONSECUTIVE    3       /* Minimum consecutive wide QRS */
#define VT_CONFIDENCE_THRESHOLD 0.7f  /* Confidence threshold for VT */

/* VF Detection */
#define VF_FREQ_MIN           4.0f    /* Hz */
#define VF_FREQ_MAX           10.0f   /* Hz */
#define VF_ZCR_THRESHOLD      0.17f   /* Zero-crossing rate threshold */
#define VF_MIN_AMPLITUDE      100.0f  /* Minimum amplitude (not asystole) */
#define VF_QRS_RATE_THRESHOLD 0.5f    /* QRS per second (below = absent) */
#define VF_CONFIDENCE_THRESHOLD 0.7f  /* Confidence threshold for VF */

/* Goertzel frequencies for VF detection */
#define VF_NUM_TEST_FREQS     7
static const float VF_TEST_FREQS[VF_NUM_TEST_FREQS] =
    {4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

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

#endif /* EKG_CONFIG_H */
