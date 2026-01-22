/**
 * EKG Signal Analyzer - Signal Processing Module
 *
 * Implementation of signal preprocessing functions.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#include "signal_processing.h"
#include "ekg_config.h"
#include <stddef.h>
#include <stdint.h>

void remove_dc_offset(const int16_t *input, float *output, uint16_t num_samples)
{
    if (input == NULL || output == NULL || num_samples == 0) {
        return;
    }

    /* Calculate mean using int32_t to prevent overflow */
    int32_t sum = 0;
    for (uint16_t i = 0; i < num_samples; i++) {
        sum += input[i];
    }
    float mean = (float)sum / (float)num_samples;

    /* Subtract mean from each sample and convert to float */
    for (uint16_t i = 0; i < num_samples; i++) {
        output[i] = (float)input[i] - mean;
    }
}

void bandpass_filter(const float *input, float *output,
                     uint16_t num_samples, float *state)
{
    if (input == NULL || output == NULL || state == NULL || num_samples == 0) {
        return;
    }

    /*
     * Cascaded IIR Butterworth bandpass filter (5-18 Hz at 100 Hz sample rate)
     * Implemented as: High-pass (5 Hz) -> Low-pass (18 Hz)
     * Using Direct Form II for each 2nd-order section
     *
     * State array layout:
     *   state[0..1]: High-pass filter state [w1, w2]
     *   state[2..3]: Reserved for high-pass
     *   state[4..5]: Low-pass filter state [w1, w2]
     *   state[6..7]: Reserved for low-pass
     */

    /* High-pass filter state */
    float hp_w1 = state[0];
    float hp_w2 = state[1];

    /* Low-pass filter state */
    float lp_w1 = state[4];
    float lp_w2 = state[5];

    /* Process each sample through cascaded filters */
    for (uint16_t n = 0; n < num_samples; n++) {
        float x = input[n];
        float hp_out, lp_out;
        float w0;

        /* High-pass filter (5 Hz) - Direct Form II */
        w0 = x - HP_A1 * hp_w1 - HP_A2 * hp_w2;
        hp_out = HP_B0 * w0 + HP_B1 * hp_w1 + HP_B2 * hp_w2;
        hp_w2 = hp_w1;
        hp_w1 = w0;

        /* Low-pass filter (18 Hz) - Direct Form II */
        w0 = hp_out - LP_A1 * lp_w1 - LP_A2 * lp_w2;
        lp_out = LP_B0 * w0 + LP_B1 * lp_w1 + LP_B2 * lp_w2;
        lp_w2 = lp_w1;
        lp_w1 = w0;

        output[n] = lp_out;
    }

    /* Save filter state for next call */
    state[0] = hp_w1;
    state[1] = hp_w2;
    state[4] = lp_w1;
    state[5] = lp_w2;
}

void moving_average(const float *input, float *output,
                   uint16_t num_samples, uint8_t window_size)
{
    if (input == NULL || output == NULL || num_samples == 0 || window_size == 0) {
        return;
    }

    /*
     * Moving average filter
     * Window is centered on current sample
     */

    uint16_t half_window = window_size / 2;

    /* Process each sample */
    for (uint16_t i = 0; i < num_samples; i++) {
        float sum = 0.0f;
        uint16_t count = 0;

        /* Calculate window boundaries */
        int32_t start = (int32_t)i - (int32_t)half_window;
        int32_t end = (int32_t)i + (int32_t)half_window;

        /* Clamp to valid range */
        if (start < 0) {
            start = 0;
        }
        if (end >= (int32_t)num_samples) {
            end = (int32_t)num_samples - 1;
        }

        /* Sum values in window */
        for (int32_t j = start; j <= end; j++) {
            sum += input[j];
            count++;
        }

        /* Calculate average */
        if (count > 0) {
            output[i] = sum / (float)count;
        } else {
            output[i] = input[i];
        }
    }
}
