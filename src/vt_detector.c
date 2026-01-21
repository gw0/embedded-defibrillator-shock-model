/**
 * EKG Signal Analyzer - VT Detection Module
 *
 * Implementation of Ventricular Tachycardia detection.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#include "vt_detector.h"
#include "ekg_config.h"
#include "heart_rate.h"
#include "signal_utils.h"
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
     * Measure QRS complex width using derivative-based method:
     * 1. Find QRS peak amplitude
     * 2. Search backward for onset (where signal starts rising rapidly)
     * 3. Search forward for end (where signal returns to baseline)
     * 4. Limit search window to realistic QRS duration (20-200ms)
     * 5. Calculate width in milliseconds
     *
     * This method is more robust to bandpass filter effects than simple threshold.
     */

    /* Maximum search window: 200ms = 20 samples at 100Hz */
    const uint16_t MAX_QRS_SAMPLES = (200 * sample_rate) / 1000;

    /* Minimum baseline threshold (to avoid noise) */
    const float BASELINE_THRESHOLD = 50.0f;

    /* Find peak amplitude at QRS location */
    float peak_amp = fabsf(signal[qrs_loc]);

    /* Calculate onset threshold (30% of peak, higher than before for robustness) */
    float onset_threshold = 0.3f * peak_amp;
    if (onset_threshold < BASELINE_THRESHOLD) {
        onset_threshold = BASELINE_THRESHOLD;
    }

    /* Search backward for QRS onset (limit search to MAX_QRS_SAMPLES) */
    uint16_t onset = qrs_loc;
    uint16_t search_limit = (qrs_loc > MAX_QRS_SAMPLES) ? (qrs_loc - MAX_QRS_SAMPLES) : 0;

    while (onset > search_limit && fabsf(signal[onset]) > onset_threshold) {
        onset--;
    }

    /* Search forward for QRS end (limit search to MAX_QRS_SAMPLES) */
    uint16_t end = qrs_loc;
    uint16_t end_limit = (qrs_loc + MAX_QRS_SAMPLES < num_samples) ?
                         (qrs_loc + MAX_QRS_SAMPLES) : (num_samples - 1);

    while (end < end_limit && fabsf(signal[end]) > onset_threshold) {
        end++;
    }

    /* Calculate width in samples */
    uint16_t width_samples = end - onset;

    /* Sanity check: QRS width should be at least 20ms */
    const uint16_t MIN_QRS_SAMPLES = (20 * sample_rate) / 1000;
    if (width_samples < MIN_QRS_SAMPLES) {
        width_samples = MIN_QRS_SAMPLES;
    }

    /* Convert to milliseconds */
    float width_ms = ((float)width_samples / (float)sample_rate) * 1000.0f;

    return width_ms;
}

int detect_vt(const float *filtered, const float *raw, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             const ekg_config_t *config,
             vt_detection_result_t *result)
{
    if (filtered == NULL || raw == NULL || qrs_result == NULL || result == NULL) {
        return EKG_ERROR_NULL_PTR;
    }

    if (num_samples < EKG_MIN_SAMPLES || num_samples > EKG_MAX_SAMPLES) {
        return EKG_ERROR_INVALID_LEN;
    }

    /* Use default config if NULL */
    if (config == NULL) {
        config = &EKG_DEFAULT_CONFIG;
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

    /* Measure QRS width for each detected QRS (use raw signal, not filtered) */
    /* Do this BEFORE early return so diagnostic info is available */
    float qrs_widths[50];
    uint8_t num_qrs = qrs_result->count < 50 ? qrs_result->count : 50;

    for (uint8_t i = 0; i < num_qrs; i++) {
        qrs_widths[i] = measure_qrs_width(raw,
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

    /* If heart rate <= threshold bpm, not VT */
    if (heart_rate <= config->vt_hr_threshold) {
        result->vt_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

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
        if (qrs_widths[i] > config->vt_qrs_width_ms) {
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

    /* Calculate confidence using log-odds (Bayesian approach) */
    float log_odds = 0.0f;  /* Prior: 50% */

    /* Heart rate contribution */
    log_odds += (heart_rate > config->vt_hr_threshold) ? LR_VT_HR_THRESH_POS : LR_VT_HR_THRESH_NEG;
    log_odds += (heart_rate > 150.0f) ? LR_VT_HR_HIGH_POS : LR_VT_HR_HIGH_NEG;

    /* QRS width contribution */
    log_odds += (mean_qrs_width > config->vt_qrs_width_ms) ? LR_VT_QRS_WIDE_POS : LR_VT_QRS_WIDE_NEG;
    log_odds += (mean_qrs_width > 140.0f) ? LR_VT_QRS_VWIDE_POS : LR_VT_QRS_VWIDE_NEG;

    /* Regularity contribution */
    log_odds += (rr_cv < config->vt_regularity_cv) ? LR_VT_REGULAR_POS : LR_VT_REGULAR_NEG;
    log_odds += (rr_cv < 10.0f) ? LR_VT_VREGULAR_POS : LR_VT_VREGULAR_NEG;

    /* Sustained duration contribution */
    log_odds += (max_consecutive >= VT_MIN_CONSECUTIVE && duration_met) ? LR_VT_SUSTAINED_POS : LR_VT_SUSTAINED_NEG;

    /* Convert to probability */
    float confidence = log_odds_to_probability(log_odds);

    /* VT detected if confidence >= threshold AND duration requirement met */
    if (confidence >= config->vt_confidence_threshold && duration_met) {
        result->vt_detected = 1;
    } else {
        result->vt_detected = 0;
    }
    result->confidence = confidence;

    return EKG_SUCCESS;
}
