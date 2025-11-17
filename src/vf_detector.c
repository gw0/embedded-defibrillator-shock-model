/**
 * EKG Signal Analyzer - VF Detection Module
 *
 * Implementation of Ventricular Fibrillation detection.
 */

#include "vf_detector.h"
#include "ekg_config.h"
#include <stddef.h>
#include <string.h>
#include <math.h>

float goertzel_magnitude(const float *signal, uint16_t num_samples,
                        float target_freq, uint16_t sample_rate)
{
    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Goertzel algorithm for calculating magnitude at specific frequency:
     * 1. Calculate coefficient from target frequency
     * 2. Process all samples through the Goertzel filter
     * 3. Calculate magnitude from final state
     */

    /* Calculate normalized frequency bin */
    float k = 0.5f + ((float)num_samples * target_freq / (float)sample_rate);
    float omega = (2.0f * PI * k) / (float)num_samples;
    float coeff = 2.0f * cosf(omega);

    /* Initialize state variables */
    float s_prev = 0.0f;
    float s_prev2 = 0.0f;

    /* Process all samples */
    for (uint16_t i = 0; i < num_samples; i++) {
        float s = signal[i] + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }

    /* Calculate magnitude */
    float real = s_prev - s_prev2 * cosf(omega);
    float imag = s_prev2 * sinf(omega);
    float magnitude = sqrtf(real * real + imag * imag);

    return magnitude;
}

float find_dominant_frequency(const float *signal, uint16_t num_samples,
                              uint16_t sample_rate)
{
    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Find dominant frequency in VF range (4-10 Hz):
     * 1. Test frequencies at 4, 5, 6, 7, 8, 9, 10 Hz
     * 2. Calculate magnitude at each frequency using Goertzel
     * 3. Find frequency with maximum magnitude
     * 4. Check if it's significantly dominant (>60% of total VF band power)
     */

    const float test_freqs[] = {4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    const uint8_t num_freqs = 7;
    float magnitudes[7];

    /* Calculate magnitude at each test frequency */
    for (uint8_t i = 0; i < num_freqs; i++) {
        magnitudes[i] = goertzel_magnitude(signal, num_samples,
                                          test_freqs[i], sample_rate);
    }

    /* Find frequency with maximum magnitude */
    float max_mag = magnitudes[0];
    uint8_t max_idx = 0;
    for (uint8_t i = 1; i < num_freqs; i++) {
        if (magnitudes[i] > max_mag) {
            max_mag = magnitudes[i];
            max_idx = i;
        }
    }

    /* Calculate total power in VF band */
    float vf_power = 0.0f;
    for (uint8_t i = 0; i < num_freqs; i++) {
        vf_power += magnitudes[i];
    }

    /* Check if dominant frequency is significant (>60% of VF band power) */
    if (vf_power > 0.0f && max_mag > 0.6f * vf_power) {
        return test_freqs[max_idx];
    } else {
        return 0.0f;  /* No clear dominant frequency in VF range */
    }
}

float calculate_zero_crossing_rate(const float *signal, uint16_t num_samples,
                                   uint16_t sample_rate)
{
    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Calculate zero-crossing rate:
     * Count transitions where signal crosses zero
     * Return crossings per second
     */

    uint16_t crossings = 0;

    /* Count zero crossings */
    for (uint16_t i = 1; i < num_samples; i++) {
        /* Check for sign change (zero crossing) */
        if ((signal[i - 1] >= 0.0f && signal[i] < 0.0f) ||
            (signal[i - 1] < 0.0f && signal[i] >= 0.0f)) {
            crossings++;
        }
    }

    /* Calculate duration in seconds */
    float duration_sec = (float)num_samples / (float)sample_rate;

    /* Calculate rate (crossings per second) */
    float zcr = (float)crossings / duration_sec;

    return zcr;
}

float calculate_amplitude(const float *signal, uint16_t num_samples)
{
    if (signal == NULL || num_samples == 0) {
        return 0.0f;
    }

    /*
     * Calculate peak-to-peak amplitude:
     * Find minimum and maximum values
     * Return difference
     */

    float min_val = signal[0];
    float max_val = signal[0];

    /* Find min and max */
    for (uint16_t i = 1; i < num_samples; i++) {
        if (signal[i] < min_val) {
            min_val = signal[i];
        }
        if (signal[i] > max_val) {
            max_val = signal[i];
        }
    }

    /* Calculate peak-to-peak amplitude */
    float amplitude = max_val - min_val;

    return amplitude;
}

int detect_vf(const float *signal, uint16_t num_samples,
             const qrs_detection_result_t *qrs_result,
             uint16_t sample_rate,
             vf_detection_result_t *result)
{
    if (signal == NULL || qrs_result == NULL || result == NULL) {
        return EKG_ERROR_NULL_PTR;
    }

    if (num_samples < EKG_MIN_SAMPLES || num_samples > EKG_MAX_SAMPLES) {
        return EKG_ERROR_INVALID_LEN;
    }

    /* Initialize result structure */
    memset(result, 0, sizeof(vf_detection_result_t));

    /* Calculate dominant frequency in VF range */
    float dominant_freq = find_dominant_frequency(signal, num_samples, sample_rate);
    result->dominant_freq = dominant_freq;

    /* Calculate zero-crossing rate */
    float zcr = calculate_zero_crossing_rate(signal, num_samples, sample_rate);
    result->zero_crossing_rate = zcr;

    /* Calculate amplitude */
    float amplitude = calculate_amplitude(signal, num_samples);
    result->amplitude = amplitude;

    /* Check QRS detection rate */
    uint8_t qrs_count = qrs_result->count;
    float duration_sec = (float)num_samples / (float)sample_rate;
    float qrs_rate = (float)qrs_count / duration_sec;

    /* If fewer than 0.5 QRS per second, consider QRS absent */
    uint8_t qrs_absent = (qrs_rate < VF_QRS_RATE_THRESHOLD) ? 1 : 0;
    result->qrs_absent = qrs_absent;

    /*
     * VF Decision Logic with scoring:
     * - Dominant frequency criterion (0-0.5)
     * - Zero-crossing rate criterion (0-0.3)
     * - QRS absence criterion (0-0.3)
     * - Amplitude criterion (0-0.1, or -infinite if too low)
     */

    float vf_score = 0.0f;

    /* Criterion 1: Dominant frequency in VF range (4-10 Hz) */
    if (dominant_freq >= VF_FREQ_MIN && dominant_freq <= VF_FREQ_MAX) {
        vf_score += 0.4f;

        /* Bonus for typical VF frequency (5-8 Hz) */
        if (dominant_freq >= 5.0f && dominant_freq <= 8.0f) {
            vf_score += 0.1f;
        }
    }

    /* Criterion 2: High zero-crossing rate */
    if (zcr > VF_ZCR_THRESHOLD) {
        vf_score += 0.2f;
    }
    if (zcr > 0.25f) {
        vf_score += 0.1f;
    }

    /* Criterion 3: No regular QRS complexes */
    if (qrs_absent == 1) {
        vf_score += 0.3f;
    } else if (qrs_count < 5 && duration_sec >= 5.0f) {
        vf_score += 0.1f;  /* Very few QRS (possibly coarse VF) */
    }

    /* Criterion 4: Adequate amplitude (not asystole) */
    if (amplitude > VF_MIN_AMPLITUDE) {
        vf_score += 0.1f;
    } else {
        /* Too low amplitude, likely asystole, NOT VF */
        vf_score = 0.0f;
    }

    /* VF detected if score >= threshold */
    if (vf_score >= VF_CONFIDENCE_THRESHOLD) {
        result->vf_detected = 1;
        result->confidence = vf_score;
    } else {
        result->vf_detected = 0;
        result->confidence = vf_score;
    }

    return EKG_SUCCESS;
}
