/**
 * EKG Signal Analyzer - Main Analysis Module
 *
 * Implementation of the main analysis function that integrates all modules.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#include "ekg_analyzer.h"
#include "ekg_config.h"
#include "signal_processing.h"
#include "qrs_detector.h"
#include "heart_rate.h"
#include "vt_detector.h"
#include "vf_detector.h"
#include <string.h>
#include <stdlib.h>

int ekg_analyze(const int16_t *samples,
                uint16_t num_samples,
                ekg_analysis_result_t *result,
                const ekg_config_t *config)
{
    /* Input validation */
    if (samples == NULL || result == NULL) {
        return EKG_ERROR_NULL_PTR;
    }

    if (num_samples < EKG_MIN_SAMPLES || num_samples > EKG_MAX_SAMPLES) {
        return EKG_ERROR_INVALID_LEN;
    }

    /* Use default config if NULL */
    if (config == NULL) {
        config = &EKG_DEFAULT_CONFIG;
    }

    /* Check for saturated signal */
    uint16_t saturated_count = 0;
    for (uint16_t i = 0; i < num_samples; i++) {
        if (samples[i] == -32768 || samples[i] == 32767) {
            saturated_count++;
        }
    }
    if (saturated_count > (uint16_t)(num_samples * 0.9f)) {
        return EKG_ERROR_SATURATED;
    }

    /* Initialize result structure */
    memset(result, 0, sizeof(ekg_analysis_result_t));

    /*
     * Complete analysis pipeline:
     * 1. Preprocessing (DC offset removal, bandpass filtering)
     * 2. QRS detection
     * 3. VT detection
     * 4. VF detection
     * 5. Decision logic
     */

    /* Step 1: Preprocessing */

    /* Allocate working buffers */
    float *signal = (float *)malloc(num_samples * sizeof(float));
    float *filtered = (float *)malloc(num_samples * sizeof(float));

    if (signal == NULL || filtered == NULL) {
        free(signal);
        free(filtered);
        return EKG_ERROR_NULL_PTR;
    }

    /* Remove DC offset */
    remove_dc_offset(samples, signal, num_samples);

    /* Apply bandpass filter (5-18 Hz) */
    float filter_state[8] = {0};
    bandpass_filter(signal, filtered, num_samples, filter_state);

    /* Step 2: QRS Detection */

    qrs_detection_result_t qrs_result;
    qrs_result.locations = (uint16_t *)malloc(50 * sizeof(uint16_t));
    qrs_result.rr_intervals = (uint16_t *)malloc(50 * sizeof(uint16_t));

    if (qrs_result.locations == NULL || qrs_result.rr_intervals == NULL) {
        free(signal);
        free(filtered);
        free(qrs_result.locations);
        free(qrs_result.rr_intervals);
        return EKG_ERROR_NULL_PTR;
    }

    int ret = detect_qrs_complexes(filtered, num_samples, &qrs_result);
    if (ret < 0) {
        /* QRS detection failed, but continue (might be VF) */
        qrs_result.count = 0;
        qrs_result.rr_count = 0;
    }

    /* Step 3: VT Detection */

    vt_detection_result_t vt_result;
    detect_vt(filtered, signal, num_samples, &qrs_result, EKG_SAMPLE_RATE, config, &vt_result);

    /* Step 4: VF Detection */

    vf_detection_result_t vf_result;
    detect_vf(signal, num_samples, &qrs_result, EKG_SAMPLE_RATE, config, &vf_result);

    /* Step 5: Decision Logic */

    /*
     * Priority: VF first (most critical), then VT
     * Default to no shock when uncertain
     */

    if (vf_result.vf_detected == 1 && vf_result.confidence >= config->vf_confidence_threshold) {
        /* VF detected */
        result->shock_advised = 1;
        result->rhythm_type = 3;  /* VF */
        result->confidence = (uint8_t)(vf_result.confidence * 100.0f);
        result->dominant_freq = vf_result.dominant_freq;
        result->heart_rate = 0;  /* Cannot measure in VF */
        result->num_qrs_detected = qrs_result.count;

    } else if (vt_result.vt_detected == 1 && vt_result.confidence >= config->vt_confidence_threshold) {
        /* VT detected */
        result->shock_advised = 1;
        result->rhythm_type = 2;  /* VT */
        result->confidence = (uint8_t)(vt_result.confidence * 100.0f);
        result->heart_rate = (uint16_t)vt_result.heart_rate;
        result->dominant_freq = 0.0f;
        result->num_qrs_detected = qrs_result.count;

    } else {
        /* Non-shockable rhythm */
        result->shock_advised = 0;

        /* Determine rhythm type for diagnostics */
        if (qrs_result.count > 0 && qrs_result.rr_count > 0) {
            float hr = calculate_heart_rate(qrs_result.rr_intervals,
                                           qrs_result.rr_count,
                                           EKG_SAMPLE_RATE);
            result->heart_rate = (uint16_t)hr;

            if (hr >= 60.0f && hr <= 100.0f) {
                result->rhythm_type = 1;  /* Normal sinus rhythm */
                result->confidence = 50;  /* Medium confidence */
            } else {
                result->rhythm_type = 4;  /* Other (non-shockable) */
                result->confidence = 50;
            }

            result->num_qrs_detected = qrs_result.count;
        } else {
            result->rhythm_type = 0;  /* Unknown/indeterminate */
            result->confidence = 0;
        }
    }

    /* Step 6: Cleanup */
    free(signal);
    free(filtered);
    free(qrs_result.locations);
    free(qrs_result.rr_intervals);

    return EKG_SUCCESS;
}
