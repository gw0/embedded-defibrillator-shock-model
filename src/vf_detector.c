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

uint8_t check_organized_tachycardia(const qrs_detection_result_t *qrs_result,
                                     uint16_t sample_rate)
{
    if (qrs_result == NULL || qrs_result->count < 4 || qrs_result->locations == NULL) {
        return 0;  /* Need at least 4 QRS with valid locations to assess regularity */
    }

    /*
     * SVT/Organized tachycardia characteristics:
     * 1. Heart rate 120-220 bpm
     * 2. Regular RR intervals (CV < 15%)
     *
     * VF should NOT be detected when these conditions are met.
     */

    /* Calculate RR intervals */
    uint8_t num_intervals = qrs_result->count - 1;
    float sum_rr = 0.0f;
    float intervals[49];  /* Max QRS_MAX_DETECTIONS - 1 */

    for (uint8_t i = 0; i < num_intervals && i < 49; i++) {
        intervals[i] = (float)(qrs_result->locations[i + 1] - qrs_result->locations[i])
                       / (float)sample_rate;
        sum_rr += intervals[i];
    }

    float mean_rr = sum_rr / (float)num_intervals;

    /* Calculate heart rate */
    float heart_rate = 60.0f / mean_rr;

    /* Must be in tachycardia range (120-220 bpm) */
    if (heart_rate < 120.0f || heart_rate > 220.0f) {
        return 0;
    }

    /* Calculate coefficient of variation */
    float sum_sq_diff = 0.0f;
    for (uint8_t i = 0; i < num_intervals && i < 49; i++) {
        float diff = intervals[i] - mean_rr;
        sum_sq_diff += diff * diff;
    }
    float std_dev = sqrtf(sum_sq_diff / (float)num_intervals);
    float cv = (std_dev / mean_rr) * 100.0f;  /* Percent */

    /* If CV < 15%, rhythm is organized (likely SVT, not VF) */
    return (cv < 15.0f) ? 1 : 0;
}

float calculate_spectral_entropy(const float *signal, uint16_t num_samples,
                                 uint16_t sample_rate)
{
    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Spectral entropy calculation:
     * 1. Calculate power at each test frequency using Goertzel
     * 2. Normalize powers to create probability distribution
     * 3. Calculate Shannon entropy: H = -sum(p * log(p))
     * 4. Normalize by max entropy (log(N)) to get 0-1 range
     *
     * High entropy = chaotic signal (VF)
     * Low entropy = organized signal (NSR/SVT)
     */

    float powers[VF_NUM_TEST_FREQS];
    float total_power = 0.0f;

    /* Calculate power at each frequency */
    for (uint8_t i = 0; i < VF_NUM_TEST_FREQS; i++) {
        float mag = goertzel_magnitude(signal, num_samples,
                                       VF_TEST_FREQS[i], sample_rate);
        powers[i] = mag * mag;  /* Power = magnitude squared */
        total_power += powers[i];
    }

    /* Avoid division by zero */
    if (total_power < 1e-10f) {
        return 0.0f;
    }

    /* Calculate Shannon entropy */
    float entropy = 0.0f;
    for (uint8_t i = 0; i < VF_NUM_TEST_FREQS; i++) {
        float p = powers[i] / total_power;
        if (p > 1e-10f) {
            entropy -= p * logf(p);
        }
    }

    /* Normalize by maximum entropy (uniform distribution) */
    float max_entropy = logf((float)VF_NUM_TEST_FREQS);
    float normalized_entropy = entropy / max_entropy;

    return normalized_entropy;
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
     * Check for organized tachycardia (SVT discrimination)
     * If rhythm is regular with tachycardia, it's likely SVT not VF
     */
    uint8_t is_organized_tachy = check_organized_tachycardia(qrs_result, sample_rate);

    /* Calculate spectral entropy for VF/organized rhythm discrimination */
    float spectral_entropy = calculate_spectral_entropy(signal, num_samples, sample_rate);

    /* Weighted evidence scoring - each criterion contributes to cumulative score */

#ifdef DIAG_ENABLED
    result->diag_fail_reason = DIAG_FAIL_NONE;
#endif

    float evidence_score = 0.0f;

    /* Amplitude contribution (soft threshold) */
    if (amplitude > 300.0f) {
        evidence_score += VF_EV_AMP_HIGH;
    } else if (amplitude > 200.0f) {
        evidence_score += VF_EV_AMP_MEDIUM;
    } else if (amplitude > 100.0f) {
        evidence_score += VF_EV_AMP_LOW;
    } else if (amplitude > 50.0f) {
        evidence_score += VF_EV_AMP_VLOW;
    }

    /* Frequency contribution */
    if (dominant_freq >= 5.0f && dominant_freq <= 8.0f) {
        evidence_score += VF_EV_FREQ_OPTIMAL;
    } else if (dominant_freq >= config->vf_freq_min &&
               dominant_freq <= config->vf_freq_max) {
        evidence_score += VF_EV_FREQ_GOOD;
    } else if (dominant_freq >= 2.5f && dominant_freq <= 12.0f) {
        evidence_score += VF_EV_FREQ_EDGE;
    }

    /* Irregularity contribution */
    if (qrs_absent) {
        evidence_score += VF_EV_QRS_ABSENT;
    } else if (zcr > 0.25f || tcr > 0.5f) {
        evidence_score += VF_EV_HIGH_IRREG;
    } else if (zcr > 0.15f || tcr > 0.3f) {
        evidence_score += VF_EV_MOD_IRREG;
    }

    /* Entropy contribution */
    if (spectral_entropy > 0.6f) {
        evidence_score += VF_EV_ENTROPY_HIGH;
    } else if (spectral_entropy > 0.4f) {
        evidence_score += VF_EV_ENTROPY_MOD;
    }

    /* Organized rhythm penalties */
    if (is_organized_tachy) {
        evidence_score += VF_EV_ORGANIZED_PENALTY;
    } else if (qrs_rate > 0.8f) {
        evidence_score += VF_EV_QRS_PRESENT_PENALTY;
    }

    /* Detection decision based on evidence threshold */
    /* Require minimum amplitude above noise floor to avoid false positives */
    if (evidence_score >= config->vf_evidence_threshold && amplitude > adaptive_threshold) {
        result->vf_detected = 1;
        result->is_fine_vf = (amplitude <= config->vf_fine_amplitude_max) ? 1 : 0;

        /* Confidence based on margin above threshold using tanh scaling */
        float margin = evidence_score - config->vf_evidence_threshold;
        result->confidence = 0.5f + 0.5f * tanhf(margin / 2.0f);  /* 0.5-1.0 range */

        /* Scale by signal quality */
        float signal_quality = estimate_signal_quality(signal, num_samples, (float)sample_rate);
        result->confidence *= signal_quality;

        return EKG_SUCCESS;
    }

    /* No VF detected */
    result->vf_detected = 0;
    result->confidence = 0.0f;

#ifdef DIAG_ENABLED
    /* Track which criteria contributed below "good" levels */
    if (amplitude <= 200.0f)  /* < VF_EV_AMP_MEDIUM */
        result->diag_fail_reason |= DIAG_FAIL_AMPLITUDE;
    if (dominant_freq < 5.0f || dominant_freq > 8.0f)  /* < VF_EV_FREQ_OPTIMAL */
        result->diag_fail_reason |= DIAG_FAIL_FREQUENCY;
    if (!qrs_absent && zcr <= 0.25f && tcr <= 0.5f)  /* < VF_EV_HIGH_IRREG */
        result->diag_fail_reason |= DIAG_FAIL_IRREGULARITY;
    if (is_organized_tachy)
        result->diag_fail_reason |= DIAG_FAIL_ORGANIZED;
    if (evidence_score >= config->vf_evidence_threshold - 1.0f)
        result->diag_fail_reason |= DIAG_FAIL_CONFIDENCE;
#endif

    return EKG_SUCCESS;
}
