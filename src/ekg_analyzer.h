/**
 * EKG Signal Analyzer - Main API
 *
 * Primary interface for EKG signal analysis and shock recommendation.
 *
 * SAFETY NOTICE: This is an educational/prototype implementation.
 * NOT for medical use. NOT certified for patient care.
 */

#ifndef EKG_ANALYZER_H
#define EKG_ANALYZER_H

#include <stdint.h>
#include "ekg_config.h"

/**
 * EKG Analysis Result Structure
 *
 * Contains the analysis output including shock recommendation,
 * detected rhythm type, and diagnostic information.
 */
typedef struct {
    uint8_t shock_advised;     /* 0 = no shock, 1 = shock advised */
    uint8_t rhythm_type;       /* 0=unknown, 1=normal, 2=VT, 3=VF, 4=other */
    uint8_t confidence;        /* 0-100% confidence in detection */
    uint16_t heart_rate;       /* Beats per minute (0 if not detectable) */
    float dominant_freq;       /* Dominant frequency in Hz (for VF) */
    uint8_t num_qrs_detected;  /* Number of QRS complexes found */
} ekg_analysis_result_t;

/**
 * Analyze EKG signal and determine if defibrillator shock is recommended
 *
 * This is the main entry point for EKG analysis. It processes the input
 * signal through all detection stages and returns a comprehensive result.
 *
 * @param samples     Pointer to array of EKG samples (int16_t)
 * @param num_samples Number of samples in array (300-1000)
 * @param result      Pointer to structure to store analysis results
 * @param config      Optional config (NULL = use defaults from EKG_DEFAULT_CONFIG)
 * @return            0 on success, negative error code on failure
 *
 * Error codes:
 *   EKG_SUCCESS (0)          - Analysis completed successfully
 *   EKG_ERROR_NULL_PTR (-1)  - NULL pointer provided
 *   EKG_ERROR_INVALID_LEN (-2) - Invalid number of samples
 *   EKG_ERROR_LOW_QUALITY (-3) - Signal quality too low
 *   EKG_ERROR_SATURATED (-4)   - Signal is saturated
 */
int ekg_analyze(const int16_t *samples,
                uint16_t num_samples,
                ekg_analysis_result_t *result,
                const ekg_config_t *config);

#endif /* EKG_ANALYZER_H */
