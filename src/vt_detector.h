/**
 * EKG Signal Analyzer - VT Detection Module
 *
 * Ventricular Tachycardia detection based on QRS analysis.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#ifndef VT_DETECTOR_H
#define VT_DETECTOR_H

#include <stdint.h>
#include "qrs_detector.h"
#include "ekg_config.h"

/**
 * VT Detection Result Structure
 *
 * Contains VT detection status and diagnostic information.
 */
typedef struct {
    uint8_t vt_detected;      /* 1 if VT detected, 0 otherwise */
    float confidence;         /* 0.0 to 1.0 */
    float heart_rate;         /* Heart rate in bpm */
    float mean_qrs_width;     /* Mean QRS width in ms */
    float rr_regularity;      /* CV in percentage */
    uint8_t wide_qrs_count;   /* Number of consecutive wide QRS */
} vt_detection_result_t;

/**
 * Measure QRS complex width
 *
 * @param signal      Filtered EKG signal
 * @param qrs_loc     QRS peak location (sample index)
 * @param num_samples Total number of samples
 * @param sample_rate Sample rate in Hz
 * @return            QRS width in milliseconds
 */
float measure_qrs_width(const float *signal, uint16_t qrs_loc,
                       uint16_t num_samples, uint16_t sample_rate);

/**
 * Detect Ventricular Tachycardia
 *
 * @param filtered     Filtered EKG signal (for detection)
 * @param raw          Raw/DC-removed EKG signal (for width measurement)
 * @param num_samples  Number of samples
 * @param qrs_result   QRS detection results
 * @param sample_rate  Sample rate in Hz
 * @param config       Configuration parameters (NULL = use defaults)
 * @param result       Structure to store VT detection results
 * @return             0 on success, negative on error
 */
int detect_vt(const float *filtered, const float *raw, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             const ekg_config_t *config,
             vt_detection_result_t *result);

#endif /* VT_DETECTOR_H */
