/**
 * EKG Signal Analyzer - VT Detection Module
 *
 * Implementation of Ventricular Tachycardia detection.
 */

#include "vt_detector.h"
#include "ekg_config.h"
#include "heart_rate.h"
#include <stddef.h>
#include <string.h>
#include <math.h>

float measure_qrs_width(const float *signal, uint16_t qrs_loc,
                       uint16_t num_samples, uint16_t sample_rate)
{
    if (signal == NULL || qrs_loc >= num_samples || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Measure QRS complex width:
     * 1. Find QRS peak amplitude
     * 2. Calculate threshold (20% of peak)
     * 3. Search backward for QRS onset
     * 4. Search forward for QRS end
     * 5. Calculate width in milliseconds
     */

    /* Find peak amplitude at QRS location */
    float peak_amp = fabsf(signal[qrs_loc]);

    /* Calculate threshold (20% of peak) */
    float threshold = 0.2f * peak_amp;

    /* Search backward for QRS onset */
    uint16_t onset = qrs_loc;
    while (onset > 0 && fabsf(signal[onset]) > threshold) {
        onset--;
    }

    /* Search forward for QRS end */
    uint16_t end = qrs_loc;
    while (end < num_samples - 1 && fabsf(signal[end]) > threshold) {
        end++;
    }

    /* Calculate width in samples */
    uint16_t width_samples = end - onset;

    /* Convert to milliseconds */
    float width_ms = ((float)width_samples / (float)sample_rate) * 1000.0f;

    return width_ms;
}

int detect_vt(const float *filtered, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             vt_detection_result_t *result)
{
    if (filtered == NULL || qrs_result == NULL || result == NULL) {
        return EKG_ERROR_NULL_PTR;
    }

    if (num_samples < EKG_MIN_SAMPLES || num_samples > EKG_MAX_SAMPLES) {
        return EKG_ERROR_INVALID_LEN;
    }

    /* Initialize result structure */
    memset(result, 0, sizeof(vt_detection_result_t));

    /*
     * VT Duration Requirement (Spec Section 4.1.2):
     * "Criteria met continuously for at least 2 seconds"
     *
     * Approach: Verify that consecutive wide QRS complexes span at least 2 seconds.
     * At minimum heart rate of 120 bpm for VT, we expect at least 4 beats in 2 seconds.
     */

    const uint16_t VT_DURATION_SAMPLES = 2 * sample_rate;  /* 2 seconds = 200 samples */

    /* Check if we have enough QRS complexes to analyze */
    if (qrs_result->count == 0 || qrs_result->rr_count == 0) {
        result->vt_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

    /* Need at least 4 QRS complexes for 2-second VT (at 120 bpm) */
    if (qrs_result->count < 4) {
        result->vt_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

    /* Calculate heart rate */
    float heart_rate = calculate_heart_rate(qrs_result->rr_intervals,
                                            qrs_result->rr_count,
                                            sample_rate);
    result->heart_rate = heart_rate;

    /* If heart rate <= 120 bpm, not VT */
    if (heart_rate <= VT_HR_THRESHOLD) {
        result->vt_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

    /* Measure QRS width for each detected QRS */
    float qrs_widths[50];
    uint8_t num_qrs = qrs_result->count < 50 ? qrs_result->count : 50;

    for (uint8_t i = 0; i < num_qrs; i++) {
        qrs_widths[i] = measure_qrs_width(filtered,
                                          qrs_result->locations[i],
                                          num_samples,
                                          sample_rate);
    }

    /* Calculate mean QRS width */
    float sum_width = 0.0f;
    for (uint8_t i = 0; i < num_qrs; i++) {
        sum_width += qrs_widths[i];
    }
    float mean_qrs_width = sum_width / (float)num_qrs;
    result->mean_qrs_width = mean_qrs_width;

    /* Calculate R-R regularity (coefficient of variation) */
    float rr_cv = calculate_rr_regularity(qrs_result->rr_intervals,
                                         qrs_result->rr_count);
    result->rr_regularity = rr_cv;

    /* Count consecutive wide QRS complexes (>120ms) and verify duration */
    uint8_t max_consecutive = 0;
    uint8_t current_consecutive = 0;
    uint16_t current_run_start = 0;
    uint16_t max_run_duration_samples = 0;

    for (uint8_t i = 0; i < num_qrs; i++) {
        if (qrs_widths[i] > VT_QRS_WIDTH_MS) {
            if (current_consecutive == 0) {
                /* Start of a new run */
                current_run_start = qrs_result->locations[i];
            }
            current_consecutive++;
        } else {
            if (current_consecutive > 0) {
                /* End of a run - calculate duration */
                uint16_t run_end = qrs_result->locations[i - 1];
                uint16_t run_duration = run_end - current_run_start;

                if (current_consecutive > max_consecutive) {
                    max_consecutive = current_consecutive;
                    max_run_duration_samples = run_duration;
                }
            }
            current_consecutive = 0;
        }
    }

    /* Check final run */
    if (current_consecutive > 0) {
        uint16_t run_end = qrs_result->locations[num_qrs - 1];
        uint16_t run_duration = run_end - current_run_start;

        if (current_consecutive > max_consecutive) {
            max_consecutive = current_consecutive;
            max_run_duration_samples = run_duration;
        }
    }

    result->wide_qrs_count = max_consecutive;

    /* Verify duration: consecutive wide QRS must span >= 2 seconds */
    uint8_t duration_met = (max_run_duration_samples >= VT_DURATION_SAMPLES) ? 1 : 0;

    /*
     * VT Decision Logic with scoring:
     * - Heart rate criterion (0-0.4)
     * - QRS width criterion (0-0.4)
     * - Regularity criterion (0-0.3)
     * - Sustained criterion (0-0.2)
     */

    float vt_score = 0.0f;

    /* Criterion 1: Heart rate */
    if (heart_rate > VT_HR_THRESHOLD) {
        vt_score += 0.3f;
    }
    if (heart_rate > 150.0f) {
        vt_score += 0.1f;
    }

    /* Criterion 2: QRS width */
    if (mean_qrs_width > VT_QRS_WIDTH_MS) {
        vt_score += 0.3f;
    }
    if (mean_qrs_width > 140.0f) {
        vt_score += 0.1f;
    }

    /* Criterion 3: Regularity */
    if (rr_cv < VT_REGULARITY_CV) {
        vt_score += 0.2f;
    }
    if (rr_cv < 10.0f) {
        vt_score += 0.1f;
    }

    /* Criterion 4: Sustained (≥3 consecutive wide QRS spanning ≥2 seconds) */
    if (max_consecutive >= VT_MIN_CONSECUTIVE && duration_met) {
        vt_score += 0.2f;
    }

    /* VT detected if score >= threshold AND duration requirement met */
    if (vt_score >= VT_CONFIDENCE_THRESHOLD && duration_met) {
        result->vt_detected = 1;
        result->confidence = vt_score;
    } else {
        result->vt_detected = 0;
        result->confidence = vt_score;
    }

    return EKG_SUCCESS;
}
