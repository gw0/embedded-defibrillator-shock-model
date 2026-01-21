/**
 * EKG Signal Analyzer - VF Detection Module
 *
 * Implementation of Ventricular Fibrillation detection.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#include "vf_detector.h"
#include "ekg_config.h"
#include "signal_utils.h"
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
     * Goertzel algorithm with Hanning window to reduce spectral leakage:
     * 1. Apply Hanning window to input
     * 2. Calculate coefficient from target frequency
     * 3. Process all windowed samples through the Goertzel filter
     * 4. Calculate magnitude from final state
     */

    /* Calculate normalized frequency bin */
    float k = 0.5f + ((float)num_samples * target_freq / (float)sample_rate);
    float omega = (2.0f * PI * k) / (float)num_samples;
    float coeff = 2.0f * cosf(omega);

    /* Initialize state variables */
    float s_prev = 0.0f;
    float s_prev2 = 0.0f;

    /* Process all samples with Hanning window */
    for (uint16_t i = 0; i < num_samples; i++) {
        /* Hanning window: w(n) = 0.5 * (1 - cos(2*pi*n/(N-1))) */
        float window = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)(num_samples - 1)));
        float windowed_sample = signal[i] * window;

        float s = windowed_sample + coeff * s_prev - s_prev2;
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
                              uint16_t sample_rate, float dominance_thresh)
{
    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Find dominant frequency using enhanced resolution (0.5 Hz steps):
     * 1. Test frequencies from 3.5 to 10.5 Hz at 0.5 Hz resolution
     * 2. Use windowed Goertzel for better frequency estimation
     * 3. For VF detection, we check if VF band power is significant
     *
     * VF is chaotic by nature - no single frequency will strongly dominate.
     * Instead, we check that:
     * a) There's significant power in the VF band (vs noise)
     * b) The peak frequency is returned if power concentration is reasonable
     */

    float magnitudes[VF_NUM_TEST_FREQS];
    float total_vf_power = 0.0f;
    float max_mag = 0.0f;
    uint8_t max_idx = 0;

    /* Calculate magnitude at each test frequency using windowed Goertzel */
    for (uint8_t i = 0; i < VF_NUM_TEST_FREQS; i++) {
        magnitudes[i] = goertzel_magnitude(signal, num_samples,
                                            VF_TEST_FREQS[i], sample_rate);
        total_vf_power += magnitudes[i];

        if (magnitudes[i] > max_mag) {
            max_mag = magnitudes[i];
            max_idx = i;
        }
    }

    /* Check power outside VF band (at 1 Hz and 20 Hz as references) */
    float low_freq_power = goertzel_magnitude(signal, num_samples, 1.0f, sample_rate);
    float high_freq_power = goertzel_magnitude(signal, num_samples, 20.0f, sample_rate);
    float out_of_band_power = low_freq_power + high_freq_power;

    /*
     * VF detection criteria:
     * 1. VF band power should be significantly higher than out-of-band power
     * 2. OR the peak magnitude should meet dominance threshold within VF band
     * 3. OR the peak magnitude should be substantial (for strong VF signals)
     */
    if (total_vf_power > 0.0f) {
        /* Check if VF band dominates over out-of-band frequencies */
        float vf_ratio = total_vf_power / (total_vf_power + out_of_band_power + 0.001f);

        /* Return peak frequency if:
         * - VF band has >60% of total power (including out-of-band), OR
         * - Peak frequency meets dominance threshold within VF band, OR
         * - Peak magnitude is substantial (>1000) indicating strong VF signal
         */
        if (vf_ratio > 0.6f ||
            max_mag >= dominance_thresh * total_vf_power ||
            max_mag > 1000.0f) {
            return VF_TEST_FREQS[max_idx];
        }
    }

    return 0.0f;  /* No clear VF-range frequency detected */
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

float estimate_noise_floor(const float *signal, uint16_t num_samples)
{
    if (signal == NULL || num_samples == 0) {
        return 0.0f;
    }

    /*
     * Estimate noise floor using a simple approximation of MAD (Median Absolute Deviation)
     * Since we don't want to allocate for sorting, we use the mean absolute deviation
     * from zero as a proxy (signal is already DC-removed).
     */

    float sum_abs = 0.0f;
    for (uint16_t i = 0; i < num_samples; i++) {
        sum_abs += (signal[i] >= 0) ? signal[i] : -signal[i];
    }
    float mean_abs = sum_abs / (float)num_samples;

    /* Scale by 1.4826 to approximate MAD-based noise estimate */
    /* This is the standard conversion factor for normal distributions */
    return mean_abs * 0.6745f;  /* 1/1.4826 */
}

float calculate_threshold_crossing_rate(const float *signal, uint16_t num_samples,
                                        uint16_t sample_rate, float threshold)
{
    if (signal == NULL || num_samples == 0 || sample_rate == 0 || threshold <= 0.0f) {
        return 0.0f;
    }

    /*
     * Calculate threshold-based crossing rate:
     * Count transitions where signal crosses the threshold level (positive or negative)
     * This is more robust to noise than zero-crossing rate
     */

    uint16_t crossings = 0;
    uint8_t above_pos_threshold = (signal[0] > threshold) ? 1 : 0;
    uint8_t below_neg_threshold = (signal[0] < -threshold) ? 1 : 0;

    for (uint16_t i = 1; i < num_samples; i++) {
        uint8_t now_above_pos = (signal[i] > threshold) ? 1 : 0;
        uint8_t now_below_neg = (signal[i] < -threshold) ? 1 : 0;

        /* Count transition from below to above positive threshold */
        if (now_above_pos && !above_pos_threshold) {
            crossings++;
        }
        /* Count transition from above to below negative threshold */
        if (now_below_neg && !below_neg_threshold) {
            crossings++;
        }

        above_pos_threshold = now_above_pos;
        below_neg_threshold = now_below_neg;
    }

    /* Calculate duration in seconds */
    float duration_sec = (float)num_samples / (float)sample_rate;

    return (float)crossings / duration_sec;
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
     */

    const uint16_t VF_DURATION_SAMPLES = 3 * sample_rate;  /* 3 seconds = 300 samples */

    /* Ensure we have at least 3 seconds of data */
    if (num_samples < VF_DURATION_SAMPLES) {
        result->vf_detected = 0;
        result->confidence = 0.0f;
        return EKG_SUCCESS;
    }

    /* Calculate amplitude for the full signal */
    float amplitude = calculate_amplitude(signal, num_samples);
    result->amplitude = amplitude;

    /* Estimate noise floor for adaptive thresholds */
    float noise_floor = estimate_noise_floor(signal, num_samples);
    result->noise_floor = noise_floor;

    /* Calculate adaptive amplitude threshold */
    float min_amplitude = config->vf_min_amplitude;
    float adaptive_threshold = noise_floor * config->vf_noise_floor_mult;
    if (adaptive_threshold < 50.0f) {
        adaptive_threshold = 50.0f;  /* Minimum threshold */
    }
    result->adaptive_threshold = adaptive_threshold;

    /* Use frequency detection with configurable dominance threshold */
    float dominant_freq = find_dominant_frequency(signal, num_samples,
                                                   sample_rate,
                                                   config->vf_dominance_threshold);
    result->dominant_freq = dominant_freq;

    /* Calculate both zero-crossing rate and threshold-based crossing rate */
    float zcr = calculate_zero_crossing_rate(signal, num_samples, sample_rate);
    result->zero_crossing_rate = zcr;

    float threshold_for_crossing = amplitude * config->vf_amplitude_thresh_ratio;
    float tcr = calculate_threshold_crossing_rate(signal, num_samples, sample_rate,
                                                   threshold_for_crossing);
    result->threshold_crossing_rate = tcr;

    /* Check QRS detection rate across full signal */
    uint8_t qrs_count = qrs_result->count;
    float duration_sec = (float)num_samples / (float)sample_rate;
    float qrs_rate = (float)qrs_count / duration_sec;

    /* If fewer than 0.5 QRS per second, consider QRS absent */
    uint8_t qrs_absent = (qrs_rate < VF_QRS_RATE_THRESHOLD) ? 1 : 0;
    result->qrs_absent = qrs_absent;

    /*
     * VF Detection Strategy:
     *
     * 1. Standard VF Detection Path:
     *    - Amplitude > min_amplitude (100)
     *    - Dominant frequency in VF range
     *    - High irregularity (ZCR or threshold crossing rate)
     *    - No organized QRS
     *
     * 2. Fine VF Detection Path (for low amplitude VF):
     *    - Amplitude > adaptive_threshold (noise_floor * 3)
     *    - Amplitude < fine_amplitude_max (200)
     *    - No QRS detected (must be truly absent)
     *    - Dominant frequency in VF band
     *    - High threshold crossing rate (noise-robust)
     *    - Lower confidence requirement
     */

    /*
     * Check if rhythm is organized (likely VT or NSR, not VF)
     * If we detect more than ~0.5 QRS per second, rhythm is organized
     * VF should have very few or no detectable QRS complexes
     */
    uint8_t not_organized = (qrs_rate < 0.6f) ? 1 : 0;

    /* Standard VF Detection Path */
    if (amplitude > min_amplitude) {
        /* Criterion: Dominant frequency in configured Hz range */
        uint8_t freq_criterion = (dominant_freq >= config->vf_freq_min &&
                                  dominant_freq <= config->vf_freq_max) ? 1 : 0;

        /* Criterion: Irregularity (ZCR or threshold crossing rate) OR QRS absent */
        uint8_t irregularity_criterion = (zcr > config->vf_zcr_threshold ||
                                          tcr > config->vf_zcr_threshold * 2.0f ||
                                          qrs_absent == 1) ? 1 : 0;

        /* VF detected if ALL criteria met */
        if (freq_criterion && irregularity_criterion && not_organized) {
            result->vf_detected = 1;
            result->is_fine_vf = 0;

            /* Calculate confidence using log-odds (Bayesian approach) */
            float log_odds = 0.0f;  /* Prior: 50% */

            /* Frequency criterion contribution */
            if (dominant_freq >= 5.0f && dominant_freq <= 8.0f) {
                log_odds += LR_VF_FREQ_OPTIMAL_POS;
            } else if (dominant_freq >= config->vf_freq_min && dominant_freq <= config->vf_freq_max) {
                log_odds += LR_VF_FREQ_BASIC_POS;
            } else {
                log_odds += LR_VF_FREQ_BASIC_NEG;
            }

            /* ZCR contribution */
            log_odds += (zcr > config->vf_zcr_threshold) ? LR_VF_ZCR_HIGH_POS : LR_VF_ZCR_HIGH_NEG;

            /* TCR contribution */
            log_odds += (tcr > config->vf_zcr_threshold * 2.0f) ? LR_VF_TCR_HIGH_POS : LR_VF_TCR_HIGH_NEG;

            /* QRS absence contribution */
            log_odds += (qrs_absent == 1) ? LR_VF_QRS_ABSENT_POS : LR_VF_QRS_ABSENT_NEG;

            /* Convert to probability and scale by signal quality */
            float signal_quality = estimate_signal_quality(signal, num_samples, (float)sample_rate);
            result->confidence = log_odds_to_probability(log_odds) * signal_quality;
            return EKG_SUCCESS;
        }
    }

    /*
     * Fine VF Detection Path:
     * This path is specifically designed to detect low-amplitude VF that
     * would be missed by the standard path.
     *
     * Fine VF characteristics:
     * - Lower amplitude than coarse VF but above noise floor
     * - Still has chaotic electrical activity (frequency in VF band)
     * - No organized QRS complexes
     */
    if (amplitude > adaptive_threshold &&
        amplitude <= config->vf_fine_amplitude_max &&
        qrs_absent == 1 &&
        not_organized) {

        /* For fine VF, we need frequency in VF band */
        /* Try detection even if standard failed */
        float fine_vf_freq = dominant_freq;
        if (fine_vf_freq == 0.0f) {
            /* Try with lower dominance threshold for fine VF */
            fine_vf_freq = find_dominant_frequency(signal, num_samples,
                                                    sample_rate, 0.30f);
        }

        uint8_t fine_freq_criterion = (fine_vf_freq >= config->vf_freq_min &&
                                       fine_vf_freq <= config->vf_freq_max) ? 1 : 0;

        /* For fine VF, threshold crossing rate is more reliable than ZCR */
        uint8_t fine_irregularity = (tcr > config->vf_zcr_threshold * 1.5f) ? 1 : 0;

        if (fine_freq_criterion && fine_irregularity) {
            result->vf_detected = 1;
            result->is_fine_vf = 1;
            result->dominant_freq = fine_vf_freq;

            /* Fine VF: log-odds calculation with penalty */
            float log_odds = 0.0f;  /* Prior: 50% */

            /* Frequency contribution */
            if (fine_vf_freq >= 5.0f && fine_vf_freq <= 8.0f) {
                log_odds += LR_VF_FREQ_OPTIMAL_POS;
            } else {
                log_odds += LR_VF_FREQ_BASIC_POS;
            }

            /* Fine VF penalty (lower certainty due to low amplitude) */
            log_odds += LR_VF_FINE_PENALTY;

            /* Convert to probability and scale by signal quality */
            float signal_quality = estimate_signal_quality(signal, num_samples, (float)sample_rate);
            result->confidence = log_odds_to_probability(log_odds) * signal_quality;
            return EKG_SUCCESS;
        }
    }

    /* No VF detected */
    result->vf_detected = 0;
    result->confidence = 0.0f;
    return EKG_SUCCESS;
}
