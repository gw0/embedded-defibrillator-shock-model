/**
 * EKG Signal Analyzer - QRS Detection Module
 *
 * Pan-Tompkins algorithm for detecting QRS complexes (ventricular depolarizations).
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#ifndef QRS_DETECTOR_H
#define QRS_DETECTOR_H

#include <stdint.h>

/**
 * QRS Detection Result Structure
 *
 * Contains detected QRS peak locations and R-R intervals.
 */
typedef struct {
    uint16_t *locations;     /* Array of QRS peak sample indices */
    uint8_t count;           /* Number of QRS peaks detected */
    uint16_t *rr_intervals;  /* R-R intervals in samples */
    uint8_t rr_count;        /* Number of R-R intervals */
} qrs_detection_result_t;

/**
 * Apply 5-point derivative to emphasize QRS slope
 *
 * @param input       Input filtered signal
 * @param output      Output derivative signal
 * @param num_samples Number of samples
 */
void derivative_filter(const float *input, float *output, uint16_t num_samples);

/**
 * Square the signal to emphasize high-frequency components
 *
 * @param input       Input derivative signal
 * @param output      Output squared signal
 * @param num_samples Number of samples
 */
void square_signal(const float *input, float *output, uint16_t num_samples);

/**
 * Apply moving window integration (150ms window at 100Hz = 15 samples)
 *
 * @param input       Input squared signal
 * @param output      Output integrated signal
 * @param num_samples Number of samples
 */
void moving_window_integrate(const float *input, float *output,
                             uint16_t num_samples);

/**
 * Detect QRS peaks using adaptive thresholding
 *
 * @param integrated  Integrated signal from moving window
 * @param num_samples Number of samples
 * @param result      Structure to store detected QRS locations
 */
void detect_qrs_peaks(const float *integrated, uint16_t num_samples,
                     qrs_detection_result_t *result);

/**
 * Calculate R-R intervals from detected QRS locations
 *
 * @param qrs_locs    Array of QRS peak sample indices
 * @param num_qrs     Number of QRS peaks
 * @param rr_intervals Output array for R-R intervals (in samples)
 * @return            Number of R-R intervals (num_qrs - 1)
 */
uint8_t calculate_rr_intervals(const uint16_t *qrs_locs, uint8_t num_qrs,
                               uint16_t *rr_intervals);

/**
 * Complete Pan-Tompkins QRS detection pipeline
 *
 * @param filtered    Bandpass filtered EKG signal (5-18 Hz)
 * @param num_samples Number of samples
 * @param result      Structure to store QRS detection results
 * @return            0 on success, negative on error
 */
int detect_qrs_complexes(const float *filtered, uint16_t num_samples,
                        qrs_detection_result_t *result);

#endif /* QRS_DETECTOR_H */
