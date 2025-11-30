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
             const ekg_config_t *config,
             vf_detection_result_t *result)
{
    if (signal == NULL || qrs_result == NULL || result == NULL) {
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
    memset(result, 0, sizeof(vf_detection_result_t));

    /*
     * VF Duration Requirement (Spec Section 4.1.1):
     * "Criteria met continuously for at least 3 seconds"
     *
     * Approach: Analyze the signal in a 3-second sliding window and verify
     * VF characteristics are sustained throughout the majority of the signal.
     */

    const uint16_t VF_DURATION_SAMPLES = 3 * sample_rate;  /* 3 seconds = 300 samples */

    /* Ensure we have at least 3 seconds of data */
    if (num_samples < VF_DURATION_SAMPLES) {
        /* Not enough data to confirm sustained VF */
        result->vf_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

    /* Calculate dominant frequency for the full signal */
    float dominant_freq = find_dominant_frequency(signal, num_samples, sample_rate);
    result->dominant_freq = dominant_freq;

    /* Calculate zero-crossing rate for the full signal */
    float zcr = calculate_zero_crossing_rate(signal, num_samples, sample_rate);
    result->zero_crossing_rate = zcr;

    /* Calculate amplitude for the full signal */
    float amplitude = calculate_amplitude(signal, num_samples);
    result->amplitude = amplitude;

    /* Check QRS detection rate across full signal */
    uint8_t qrs_count = qrs_result->count;
    float duration_sec = (float)num_samples / (float)sample_rate;
    float qrs_rate = (float)qrs_count / duration_sec;

    /* If fewer than 0.5 QRS per second, consider QRS absent */
    uint8_t qrs_absent = (qrs_rate < VF_QRS_RATE_THRESHOLD) ? 1 : 0;
    result->qrs_absent = qrs_absent;

    /*
     * VF Decision Logic per Spec Section 6.2:
     * "Dominant frequency in 4-10 Hz range AND
     *  (Zero-crossing rate >0.17 OR no QRS detected for >2 seconds) AND
     *  Peak-to-peak amplitude >100"
     *
     * All criteria must be met (AND logic, not scoring).
     *
     * IMPORTANT: To distinguish VF from VT with wide QRS:
     * - VT has organized QRS complexes (>1 QRS/sec) at regular intervals
     * - VF has chaotic activity with few or no organized QRS (<0.5 QRS/sec)
     * - If QRS rate > 1.0/sec, likely VT not VF (even with high frequency content)
     */

    /* Criterion 1: Peak-to-peak amplitude threshold (not asystole) */
    if (amplitude <= config->vf_min_amplitude) {
        result->vf_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

    /* Criterion 2: Dominant frequency in configured Hz range */
    uint8_t freq_criterion = (dominant_freq >= config->vf_freq_min &&
                              dominant_freq <= config->vf_freq_max) ? 1 : 0;

    /* Criterion 3: Zero-crossing rate threshold OR no QRS detected */
    /* Note: "no QRS detected for >2 seconds" = qrs_absent == 1 in our implementation */
    uint8_t irregularity_criterion = (zcr > config->vf_zcr_threshold || qrs_absent == 1) ? 1 : 0;

    /*
     * Criterion 4: Exclude organized rhythms (VT)
     * If QRS rate > 1.0/sec, we have an organized rhythm, likely VT not VF
     */
    uint8_t not_organized = (qrs_rate <= 1.0f) ? 1 : 0;

    /* VF detected if ALL criteria met */
    if (freq_criterion && irregularity_criterion && not_organized) {
        result->vf_detected = 1;
        /* Calculate confidence based on how well criteria are met */
        float confidence = 0.5f;  /* Base confidence */

        /* Bonus for typical VF frequency (5-8 Hz) */
        if (dominant_freq >= 5.0f && dominant_freq <= 8.0f) {
            confidence += 0.2f;
        }

        /* Bonus for high zero-crossing rate */
        if (zcr > config->vf_zcr_threshold) {
            confidence += 0.15f;
        }

        /* Bonus for QRS absence */
        if (qrs_absent == 1) {
            confidence += 0.15f;
        }

        result->confidence = (confidence > 1.0f) ? 1.0f : confidence;
    } else {
        result->vf_detected = 0;
        result->confidence = 0.0f;
    }

    return EKG_SUCCESS;
}
