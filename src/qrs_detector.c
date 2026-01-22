/**
 * EKG Signal Analyzer - QRS Detection Module
 *
 * Implementation of Pan-Tompkins QRS detection algorithm.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#include "qrs_detector.h"
#include "ekg_config.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void derivative_filter(const float *input, float *output, uint16_t num_samples)
{
    if (input == NULL || output == NULL || num_samples == 0) {
        return;
    }

    /*
     * 5-point derivative filter:
     * y[n] = (1/8T) * (-x[n-2] - 2*x[n-1] + 2*x[n+1] + x[n+2])
     * At 100 Hz, T = 0.01s, so 1/8T = 12.5
     * Simplified: y[n] = 0.125 * (-x[n-2] - 2*x[n-1] + 2*x[n+1] + x[n+2])
     */

    for (uint16_t n = 0; n < num_samples; n++) {
        float xn_minus2, xn_minus1, xn_plus1, xn_plus2;

        /* Handle boundaries by padding with edge values */
        xn_minus2 = (n >= 2) ? input[n - 2] : input[0];
        xn_minus1 = (n >= 1) ? input[n - 1] : input[0];
        xn_plus1 = (n < num_samples - 1) ? input[n + 1] : input[num_samples - 1];
        xn_plus2 = (n < num_samples - 2) ? input[n + 2] : input[num_samples - 1];

        /* Apply 5-point derivative formula */
        output[n] = 0.125f * (-xn_minus2 - 2.0f * xn_minus1 + 2.0f * xn_plus1 + xn_plus2);
    }
}

void square_signal(const float *input, float *output, uint16_t num_samples)
{
    if (input == NULL || output == NULL || num_samples == 0) {
        return;
    }

    /* Square each sample to emphasize high-frequency components */
    for (uint16_t i = 0; i < num_samples; i++) {
        output[i] = input[i] * input[i];
    }
}

void moving_window_integrate(const float *input, float *output,
                             uint16_t num_samples)
{
    if (input == NULL || output == NULL || num_samples == 0) {
        return;
    }

    /*
     * Moving window integration (150ms window at 100Hz = 15 samples)
     * output[i] = (1/N) * Σ(input[i-j]) for j = 0 to N-1
     */

    const uint16_t window_size = QRS_INTEGRATION_SAMPLES;  /* 15 samples */

    /* For efficiency, use sliding window technique */
    float sum = 0.0f;

    /* Initialize sum with first window */
    uint16_t initial_window = (window_size < num_samples) ? window_size : num_samples;
    for (uint16_t i = 0; i < initial_window; i++) {
        sum += input[i];
    }
    output[0] = sum / (float)window_size;

    /* Slide the window through the signal */
    for (uint16_t i = 1; i < num_samples; i++) {
        /* Add new sample entering the window */
        if (i + window_size - 1 < num_samples) {
            sum += input[i + window_size - 1];
        }

        /* Remove old sample leaving the window */
        sum -= input[i - 1];

        output[i] = sum / (float)window_size;
    }
}

void detect_qrs_peaks(const float *integrated, uint16_t num_samples,
                     qrs_detection_result_t *result)
{
    if (integrated == NULL || result == NULL || num_samples == 0) {
        return;
    }

    /* Initialize result */
    result->count = 0;
    result->rr_count = 0;

    if (num_samples < 200) {
        return;  /* Need at least 2 seconds for initialization */
    }

    /*
     * Pan-Tompkins adaptive thresholding algorithm
     * Initialization phase: analyze first 2 seconds (200 samples)
     */

    /* Find max in first 200 samples for SPKI initialization */
    float max_val = integrated[0];
    for (uint16_t i = 1; i < 200; i++) {
        if (integrated[i] > max_val) {
            max_val = integrated[i];
        }
    }

    /* Calculate mean for NPKI initialization */
    float sum = 0.0f;
    for (uint16_t i = 0; i < 200; i++) {
        sum += integrated[i];
    }
    float mean_val = sum / 200.0f;

    /* Initialize adaptive thresholds */
    float SPKI = max_val * QRS_INIT_SPKI_FACTOR;       /* Signal peak estimate */
    float NPKI = mean_val * QRS_INIT_NPKI_FACTOR;      /* Noise peak estimate */
    float THRESHOLD_I1 = NPKI + QRS_THRESHOLD_FACTOR * (SPKI - NPKI);
    float THRESHOLD_I2 = 0.5f * THRESHOLD_I1;

    /* Minimum threshold to avoid detecting pure noise */
    /* Require significant signal energy - if max is low, likely just noise/asystole */
    const float MIN_SIGNAL_ENERGY = 100.0f;
    if (max_val < MIN_SIGNAL_ENERGY) {
        /* Signal too weak, likely noise/asystole - return no detections */
        result->count = 0;
        result->rr_count = 0;
        return;
    }

    const float MIN_DETECTION_THRESHOLD = 80.0f;
    if (THRESHOLD_I1 < MIN_DETECTION_THRESHOLD) {
        THRESHOLD_I1 = MIN_DETECTION_THRESHOLD;
        THRESHOLD_I2 = 0.5f * THRESHOLD_I1;
    }

    uint16_t last_qrs_location = 0;
    const float learning_rate = QRS_LEARNING_RATE;

    /* Detection loop starting after initialization period */
    for (uint16_t i = 200; i < num_samples; i++) {
        uint8_t is_peak = 0;

        /* Check if current sample is a local maximum */
        if (i > 0 && i < num_samples - 1) {
            if (integrated[i] > integrated[i - 1] && integrated[i] > integrated[i + 1]) {
                is_peak = 1;
            }
        }

        /* Check if peak exceeds threshold and respects refractory period */
        if (is_peak && integrated[i] > THRESHOLD_I1) {
            if ((i - last_qrs_location) > QRS_REFRACTORY_SAMPLES) {
                /* Valid QRS detection */
                if (result->count < 50) {  /* Max 50 QRS peaks */
                    result->locations[result->count] = i;
                    result->count++;
                }

                /* Update signal peak estimate */
                SPKI = learning_rate * integrated[i] + (1.0f - learning_rate) * SPKI;

                last_qrs_location = i;

                /* Update thresholds */
                THRESHOLD_I1 = NPKI + QRS_THRESHOLD_FACTOR * (SPKI - NPKI);
                THRESHOLD_I2 = 0.5f * THRESHOLD_I1;
            }
        } else if (!is_peak || integrated[i] <= THRESHOLD_I1) {
            /* Update noise peak estimate for non-peaks */
            if (is_peak && integrated[i] > NPKI) {
                NPKI = learning_rate * integrated[i] + (1.0f - learning_rate) * NPKI;

                /* Update thresholds */
                THRESHOLD_I1 = NPKI + QRS_THRESHOLD_FACTOR * (SPKI - NPKI);
                THRESHOLD_I2 = 0.5f * THRESHOLD_I1;
            }
        }

        /* Search-back if no QRS detected for too long (>1.66 seconds = 166 samples) */
        if ((i - last_qrs_location) > QRS_SEARCHBACK_SAMPLES) {
            /* Search back from last_qrs + refractory to current - refractory */
            uint16_t search_start = last_qrs_location + QRS_REFRACTORY_SAMPLES;
            uint16_t search_end = (i > QRS_REFRACTORY_SAMPLES) ? (i - QRS_REFRACTORY_SAMPLES) : i;

            float max_peak = 0.0f;
            uint16_t max_loc = 0;

            for (uint16_t j = search_start; j < search_end && j < num_samples - 1; j++) {
                /* Check for local maximum above THRESHOLD_I2 */
                if (integrated[j] > integrated[j - 1] && integrated[j] > integrated[j + 1]) {
                    if (integrated[j] > THRESHOLD_I2 && integrated[j] > max_peak) {
                        max_peak = integrated[j];
                        max_loc = j;
                    }
                }
            }

            /* If found a peak in search-back */
            if (max_peak > 0.0f && result->count < 50) {
                result->locations[result->count] = max_loc;
                result->count++;

                /* Update SPKI and thresholds */
                SPKI = learning_rate * max_peak + (1.0f - learning_rate) * SPKI;
                THRESHOLD_I1 = NPKI + QRS_THRESHOLD_FACTOR * (SPKI - NPKI);
                THRESHOLD_I2 = 0.5f * THRESHOLD_I1;

                last_qrs_location = max_loc;
            }
        }
    }
}

uint8_t calculate_rr_intervals(const uint16_t *qrs_locs, uint8_t num_qrs,
                               uint16_t *rr_intervals)
{
    if (qrs_locs == NULL || rr_intervals == NULL || num_qrs < 2) {
        return 0;
    }

    /* Calculate R-R intervals as differences between consecutive QRS locations */
    for (uint8_t i = 0; i < num_qrs - 1; i++) {
        rr_intervals[i] = qrs_locs[i + 1] - qrs_locs[i];
    }

    return num_qrs - 1;
}

int detect_qrs_complexes(const float *filtered, uint16_t num_samples,
                        qrs_detection_result_t *result)
{
    if (filtered == NULL || result == NULL) {
        return EKG_ERROR_NULL_PTR;
    }

    if (num_samples < EKG_MIN_SAMPLES || num_samples > EKG_MAX_SAMPLES) {
        return EKG_ERROR_INVALID_LEN;
    }

    /*
     * Complete Pan-Tompkins QRS detection pipeline:
     * 1. Derivative filter
     * 2. Squaring
     * 3. Moving window integration
     * 4. Adaptive threshold peak detection
     * 5. Calculate R-R intervals
     */

    /* Allocate temporary buffers for processing stages */
    float *derivative = (float *)malloc(num_samples * sizeof(float));
    float *squared = (float *)malloc(num_samples * sizeof(float));
    float *integrated = (float *)malloc(num_samples * sizeof(float));

    if (derivative == NULL || squared == NULL || integrated == NULL) {
        /* Memory allocation failed */
        free(derivative);
        free(squared);
        free(integrated);
        return EKG_ERROR_NULL_PTR;
    }

    /* Step 1: Apply derivative filter */
    derivative_filter(filtered, derivative, num_samples);

    /* Step 2: Square the signal */
    square_signal(derivative, squared, num_samples);

    /* Step 3: Apply moving window integration */
    moving_window_integrate(squared, integrated, num_samples);

    /* Step 4: Detect QRS peaks with adaptive thresholding */
    detect_qrs_peaks(integrated, num_samples, result);

    /* Step 5: Calculate R-R intervals */
    if (result->count >= 2) {
        result->rr_count = calculate_rr_intervals(result->locations,
                                                   result->count,
                                                   result->rr_intervals);
    } else {
        result->rr_count = 0;
    }

    /* Free temporary buffers */
    free(derivative);
    free(squared);
    free(integrated);

    return EKG_SUCCESS;
}
