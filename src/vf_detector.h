/**
 * EKG Signal Analyzer - VF Detection Module
 *
 * Ventricular Fibrillation detection using frequency and time domain analysis.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#ifndef VF_DETECTOR_H
#define VF_DETECTOR_H

#include <stdint.h>
#include "qrs_detector.h"
#include "ekg_config.h"

/**
 * VF Detection Result Structure
 *
 * Contains VF detection status and diagnostic information.
 */
typedef struct {
    uint8_t vf_detected;      /* 1 if VF detected, 0 otherwise */
    uint8_t is_fine_vf;       /* 1 if detected via fine VF path */
    float confidence;         /* 0.0 to 1.0 */
    float dominant_freq;      /* Dominant frequency in Hz */
    float zero_crossing_rate; /* Zero crossings per second */
    float threshold_crossing_rate; /* Threshold-based crossings per second */
    float amplitude;          /* Peak-to-peak amplitude */
    float noise_floor;        /* Estimated noise floor */
    float adaptive_threshold; /* Computed adaptive amplitude threshold */
    uint8_t qrs_absent;       /* 1 if no QRS detected */
} vf_detection_result_t;

/**
 * Calculate magnitude at specific frequency using Goertzel algorithm with Hanning window
 *
 * Applies Hanning window to reduce spectral leakage before Goertzel.
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @param target_freq Target frequency in Hz
 * @param sample_rate Sample rate in Hz
 * @return            Magnitude at target frequency
 */
float goertzel_magnitude(const float *signal, uint16_t num_samples,
                        float target_freq, uint16_t sample_rate);

/**
 * Find dominant frequency in VF range with configurable resolution
 *
 * Uses 0.5 Hz resolution and windowed Goertzel for better frequency estimation.
 *
 * @param signal            Input signal
 * @param num_samples       Number of samples
 * @param sample_rate       Sample rate in Hz
 * @param dominance_thresh  Dominance threshold (0.0-1.0)
 * @return                  Dominant frequency in Hz, or 0 if not dominant enough
 */
float find_dominant_frequency(const float *signal, uint16_t num_samples,
                              uint16_t sample_rate, float dominance_thresh);

/**
 * Calculate zero-crossing rate
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @param sample_rate Sample rate in Hz
 * @return            Zero crossings per second
 */
float calculate_zero_crossing_rate(const float *signal, uint16_t num_samples,
                                   uint16_t sample_rate);

/**
 * Calculate peak-to-peak amplitude
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @return            Peak-to-peak amplitude
 */
float calculate_amplitude(const float *signal, uint16_t num_samples);

/**
 * Estimate noise floor using median absolute deviation
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @return            Estimated noise floor
 */
float estimate_noise_floor(const float *signal, uint16_t num_samples);

/**
 * Calculate threshold-based crossing rate
 *
 * Counts crossings where amplitude exceeds threshold (noise-robust alternative to ZCR)
 *
 * @param signal      Input signal
 * @param num_samples Number of samples
 * @param sample_rate Sample rate in Hz
 * @param threshold   Amplitude threshold (e.g., 10% of peak)
 * @return            Threshold crossings per second
 */
float calculate_threshold_crossing_rate(const float *signal, uint16_t num_samples,
                                        uint16_t sample_rate, float threshold);

/**
 * Detect Ventricular Fibrillation
 *
 * @param signal       Original (DC-removed) EKG signal
 * @param num_samples  Number of samples
 * @param qrs_result   QRS detection results
 * @param sample_rate  Sample rate in Hz
 * @param config       Configuration parameters (NULL = use defaults)
 * @param result       Structure to store VF detection results
 * @return             0 on success, negative on error
 */
int detect_vf(const float *signal, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             const ekg_config_t *config,
             vf_detection_result_t *result);

#endif /* VF_DETECTOR_H */
