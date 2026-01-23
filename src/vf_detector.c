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

freq_result_t find_dominant_frequency(const float *signal, uint16_t num_samples,
                                      uint16_t sample_rate, float dominance_thresh)
{
    freq_result_t result = {0.0f, 0.0f};

    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return result;
    }

    /*
     * Find dominant frequency using enhanced resolution (0.5 Hz steps):
     * 1. Test frequencies from 2.5 to 12 Hz at 0.5 Hz resolution
     * 2. Use windowed Goertzel for better frequency estimation
     * 3. Calculate measurement confidence based on:
     *    - Peak prominence (max vs second max)
     *    - Power concentration (max / total)
     *    - VF band dominance (VF power vs out-of-band)
     */

    float magnitudes[VF_NUM_TEST_FREQS];
    float total_vf_power = 0.0f;
    float max_mag = 0.0f;
    float second_max = 0.0f;
    uint8_t max_idx = 0;

    /* Calculate magnitude at each test frequency using windowed Goertzel */
    for (uint8_t i = 0; i < VF_NUM_TEST_FREQS; i++) {
        magnitudes[i] = goertzel_magnitude(signal, num_samples,
                                            VF_TEST_FREQS[i], sample_rate);
        total_vf_power += magnitudes[i];

        if (magnitudes[i] > max_mag) {
            second_max = max_mag;
            max_mag = magnitudes[i];
            max_idx = i;
        } else if (magnitudes[i] > second_max) {
            second_max = magnitudes[i];
        }
    }

    /* Check power outside VF band (at 1 Hz and 20 Hz as references) */
    float low_freq_power = goertzel_magnitude(signal, num_samples, 1.0f, sample_rate);
    float high_freq_power = goertzel_magnitude(signal, num_samples, 20.0f, sample_rate);
    float out_of_band_power = low_freq_power + high_freq_power;

    /*
     * Calculate measurement confidence based on three factors:
     * 1. Peak prominence: How much does max peak exceed others?
     * 2. Power concentration: Is power concentrated or spread across spectrum?
     * 3. VF band dominance: Is VF band power > out-of-band power?
     */
    if (total_vf_power > 0.0f && max_mag > 0.0f) {
        /* Factor 1: Peak prominence (max vs second) */
        float prominence = (max_mag - second_max) / max_mag;  /* 0-1 */

        /* Factor 2: Power concentration (max / total) */
        float concentration = max_mag / total_vf_power;  /* 0-1 */

        /* Factor 3: VF band dominance vs out-of-band */
        float vf_ratio = total_vf_power / (total_vf_power + out_of_band_power + 0.001f);

        /* Combine factors with weights */
        result.confidence = (prominence * 0.4f + concentration * 0.3f + vf_ratio * 0.3f);

        /* Return frequency if conditions are met */
        if (vf_ratio > 0.6f ||
            max_mag >= dominance_thresh * total_vf_power ||
            max_mag > 1000.0f) {
            result.frequency = VF_TEST_FREQS[max_idx];
        } else {
            /* Low confidence that there's no dominant frequency */
            result.frequency = 0.0f;
            result.confidence = vf_ratio * 0.5f;  /* Scale down when no clear peak */
        }
    }

    return result;
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

crossing_result_t calculate_threshold_crossing_rate(const float *signal, uint16_t num_samples,
                                                    uint16_t sample_rate, float threshold)
{
    crossing_result_t result = {0.0f, 0.0f};

    if (signal == NULL || num_samples == 0 || sample_rate == 0 || threshold <= 0.0f) {
        return result;
    }

    /*
     * Calculate threshold-based crossing rate:
     * Count transitions where signal crosses the threshold level (positive or negative)
     * This is more robust to noise than zero-crossing rate
     *
     * Confidence based on:
     * - Sample count: More samples = more reliable rate estimate
     * - Number of crossings: Very few crossings = less reliable rate
     */

    uint16_t crossings = 0;
    uint8_t above_pos_threshold = (signal[0] > threshold) ? 1 : 0;
    uint8_t below_neg_threshold = (signal[0] < -threshold) ? 1 : 0;

    /* Track amplitude statistics for confidence calculation */
    float max_amp = 0.0f;
    for (uint16_t i = 0; i < num_samples; i++) {
        float abs_val = (signal[i] >= 0) ? signal[i] : -signal[i];
        if (abs_val > max_amp) max_amp = abs_val;
    }

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

    result.rate = (float)crossings / duration_sec;

    /* Confidence factors */
    /* Factor 1: Sample count - 300+ samples (3 sec at 100Hz) = full confidence */
    float sample_factor = (float)num_samples / 300.0f;
    if (sample_factor > 1.0f) sample_factor = 1.0f;

    /* Factor 2: Crossing count - need at least a few crossings for reliable rate */
    float crossing_factor = (crossings >= 6) ? 1.0f : (float)crossings / 6.0f;

    /* Factor 3: Amplitude headroom - threshold should be well below max amplitude */
    float headroom_factor = (max_amp > threshold * 2.0f) ? 1.0f :
                            (max_amp > threshold) ? (max_amp - threshold) / threshold : 0.0f;

    result.confidence = sample_factor * crossing_factor * headroom_factor;

    return result;
}

/* Calculate RR interval coefficient of variation (returns -1 if insufficient data) */
float calculate_rr_cv(const qrs_detection_result_t *qrs_result, uint16_t sample_rate)
{
    if (qrs_result == NULL || qrs_result->count < 4 || qrs_result->locations == NULL) {
        return -1.0f;
    }

    uint8_t num_intervals = qrs_result->count - 1;
    float sum_rr = 0.0f;
    float intervals[49];

    for (uint8_t i = 0; i < num_intervals && i < 49; i++) {
        intervals[i] = (float)(qrs_result->locations[i + 1] - qrs_result->locations[i])
                       / (float)sample_rate;
        sum_rr += intervals[i];
    }

    float mean_rr = sum_rr / (float)num_intervals;

    float sum_sq_diff = 0.0f;
    for (uint8_t i = 0; i < num_intervals && i < 49; i++) {
        float diff = intervals[i] - mean_rr;
        sum_sq_diff += diff * diff;
    }
    float std_dev = sqrtf(sum_sq_diff / (float)num_intervals);

    return (std_dev / mean_rr) * 100.0f;  /* Percent */
}

uint8_t check_organized_tachycardia(const qrs_detection_result_t *qrs_result,
                                     uint16_t sample_rate,
                                     const ekg_config_t *config)
{
    if (qrs_result == NULL || qrs_result->count < 4 || qrs_result->locations == NULL) {
        return 0;
    }

    if (config == NULL) {
        config = &EKG_DEFAULT_CONFIG;
    }

    /* Calculate heart rate from mean RR interval */
    uint8_t num_intervals = qrs_result->count - 1;
    float sum_rr = 0.0f;
    for (uint8_t i = 0; i < num_intervals && i < 49; i++) {
        sum_rr += (float)(qrs_result->locations[i + 1] - qrs_result->locations[i])
                  / (float)sample_rate;
    }
    float heart_rate = 60.0f / (sum_rr / (float)num_intervals);

    /* Must be in tachycardia range */
    if (heart_rate < config->vf_org_hr_min || heart_rate > config->vf_org_hr_max) {
        return 0;
    }

    /* Check regularity using CV */
    float cv = calculate_rr_cv(qrs_result, sample_rate);
    return (cv >= 0 && cv < config->vf_org_cv_thresh) ? 1 : 0;
}

entropy_result_t calculate_spectral_entropy(const float *signal, uint16_t num_samples,
                                            uint16_t sample_rate)
{
    entropy_result_t result = {0.0f, 0.0f};

    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return result;
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
     *
     * Confidence based on:
     * - Sample count: More samples = more reliable entropy estimate
     * - Power level: Very low power makes entropy unreliable
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
        return result;
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

    result.entropy = normalized_entropy;

    /* Confidence factors */
    /* Factor 1: Sample count - 500+ samples = full confidence */
    float sample_factor = (float)num_samples / 500.0f;
    if (sample_factor > 1.0f) sample_factor = 1.0f;

    /* Factor 2: Power level - reasonable power for reliable entropy */
    float power_factor = (total_power > 100.0f) ? 1.0f : total_power / 100.0f;

    result.confidence = sample_factor * power_factor;

    return result;
}

flatness_result_t calculate_spectral_flatness(const float *signal, uint16_t num_samples,
                                               uint16_t sample_rate)
{
    flatness_result_t result = {0.0f, 0.0f};

    if (signal == NULL || num_samples == 0 || sample_rate == 0) {
        return result;
    }

    /*
     * Spectral flatness calculation:
     * Flatness = geometric_mean(magnitudes) / arithmetic_mean(magnitudes)
     *
     * For numerical stability, compute log of geometric mean:
     * log(geometric_mean) = (1/N) * sum(log(mag_i))
     * Then: flatness = exp(log_geometric_mean) / arithmetic_mean
     *
     * VF is spectrally flat (energy distributed across frequencies): flatness ~0.7-1.0
     * Organized rhythms have peaked spectra (energy at few frequencies): flatness ~0.1-0.4
     *
     * This is a powerful discriminator for VF detection because:
     * - VF has no dominant frequency (chaotic, broad spectrum)
     * - Organized rhythms (NSR, VT, SVT) have clear spectral peaks
     */

    float magnitudes[VF_NUM_TEST_FREQS];
    float total_mag = 0.0f;
    float log_sum = 0.0f;
    uint8_t valid_count = 0;

    /* Calculate magnitude at each test frequency using Goertzel */
    for (uint8_t i = 0; i < VF_NUM_TEST_FREQS; i++) {
        magnitudes[i] = goertzel_magnitude(signal, num_samples,
                                           VF_TEST_FREQS[i], sample_rate);
        total_mag += magnitudes[i];

        /* Only include positive magnitudes in geometric mean */
        if (magnitudes[i] > 1e-10f) {
            log_sum += logf(magnitudes[i]);
            valid_count++;
        }
    }

    /* Need at least some valid magnitudes */
    if (valid_count < 3 || total_mag < 1e-10f) {
        return result;
    }

    /* Calculate arithmetic mean */
    float arithmetic_mean = total_mag / (float)VF_NUM_TEST_FREQS;

    /* Calculate geometric mean via log */
    float log_geometric_mean = log_sum / (float)valid_count;
    float geometric_mean = expf(log_geometric_mean);

    /* Flatness = geometric / arithmetic (range 0-1) */
    result.flatness = geometric_mean / arithmetic_mean;

    /* Clamp to [0, 1] for safety */
    if (result.flatness > 1.0f) result.flatness = 1.0f;
    if (result.flatness < 0.0f) result.flatness = 0.0f;

    /* Confidence factors */
    /* Factor 1: Sample count - 300+ samples = full confidence */
    float sample_factor = (float)num_samples / 300.0f;
    if (sample_factor > 1.0f) sample_factor = 1.0f;

    /* Factor 2: Power level - need reasonable power for reliable flatness */
    float power_factor = (total_mag > 50.0f) ? 1.0f : total_mag / 50.0f;

    /* Factor 3: Valid bin count - need most bins to have measurable signal */
    float bin_factor = (float)valid_count / (float)VF_NUM_TEST_FREQS;

    result.confidence = sample_factor * power_factor * bin_factor;

    return result;
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
    freq_result_t freq = find_dominant_frequency(signal, num_samples,
                                                  sample_rate,
                                                  config->vf_dominance_threshold);
    result->dominant_freq = freq.frequency;

    /* Calculate both zero-crossing rate and threshold-based crossing rate */
    float zcr = calculate_zero_crossing_rate(signal, num_samples, sample_rate);
    result->zero_crossing_rate = zcr;

    float threshold_for_crossing = amplitude * config->vf_amplitude_thresh_ratio;
    crossing_result_t tcr = calculate_threshold_crossing_rate(signal, num_samples, sample_rate,
                                                               threshold_for_crossing);
    result->threshold_crossing_rate = tcr.rate;

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
     *    - Amplitude > min_amplitude
     *    - Dominant frequency in VF range
     *    - High irregularity (ZCR or threshold crossing rate)
     *    - No organized QRS
     *
     * 2. Fine VF Detection Path (for low amplitude VF):
     *    - Amplitude > adaptive_threshold (noise_floor * 3)
     *    - Amplitude < fine_amplitude_max
     *    - No QRS detected (must be truly absent)
     *    - Dominant frequency in VF band
     *    - High threshold crossing rate (noise-robust)
     *    - Lower confidence requirement
     */

    /*
     * Check for organized tachycardia (SVT discrimination)
     * If rhythm is regular with tachycardia, it's likely SVT not VF
     */
    uint8_t is_organized_tachy = check_organized_tachycardia(qrs_result, sample_rate, config);

    /* Calculate spectral entropy for VF/organized rhythm discrimination */
    entropy_result_t ent = calculate_spectral_entropy(signal, num_samples, sample_rate);

    /* Calculate spectral flatness - key discriminator for VF (chaotic spectrum) */
    flatness_result_t flat = calculate_spectral_flatness(signal, num_samples, sample_rate);

    /*
     * Weighted evidence scoring with measurement confidence
     * Each criterion's contribution is scaled by measurement confidence.
     */

#ifdef DIAG_ENABLED
    result->diag_fail_reason = DIAG_FAIL_NONE;
#endif

    float evidence_score = 0.0f;

    /* Amplitude contribution (soft threshold) - amplitude is directly measurable, high confidence */
    if (amplitude > config->vf_amp_high_thresh) {
        evidence_score += VF_EV_AMP_HIGH;
    } else if (amplitude > config->vf_amp_medium_thresh) {
        evidence_score += VF_EV_AMP_MEDIUM;
    } else if (amplitude > config->vf_amp_low_thresh) {
        evidence_score += VF_EV_AMP_LOW;
    } else if (amplitude > config->vf_amp_vlow_thresh) {
        evidence_score += VF_EV_AMP_VLOW;
    }

    /* Frequency contribution - weighted by measurement confidence */
    if (freq.frequency >= 5.0f && freq.frequency <= 8.0f) {
        evidence_score += VF_EV_FREQ_OPTIMAL * freq.confidence;
    } else if (freq.frequency >= config->vf_freq_min &&
               freq.frequency <= config->vf_freq_max) {
        evidence_score += VF_EV_FREQ_GOOD * freq.confidence;
    } else if (freq.frequency >= config->vf_penalty_freq_min &&
               freq.frequency <= config->vf_penalty_freq_max) {
        evidence_score += VF_EV_FREQ_EDGE * freq.confidence;
    }

    /* Irregularity contribution - weighted by crossing rate confidence */
    /* Only meaningful when QRS is absent or at very low rate (not organized) */
    if (qrs_absent) {
        evidence_score += VF_EV_QRS_ABSENT;
    } else if (!is_organized_tachy && qrs_rate <= config->vf_qrs_rate_penalty_thresh) {
        /* QRS present but disorganized - check ZCR/TCR for VF characteristics */
        if (zcr > config->vf_zcr_high_thresh || tcr.rate > config->vf_tcr_high_thresh) {
            evidence_score += VF_EV_HIGH_IRREG * tcr.confidence;
        } else if (zcr > config->vf_zcr_mod_thresh || tcr.rate > config->vf_tcr_mod_thresh) {
            evidence_score += VF_EV_MOD_IRREG * tcr.confidence;
        }
    }

    /* Entropy contribution - weighted by measurement confidence */
    if (ent.entropy > config->vf_entropy_high_thresh) {
        evidence_score += VF_EV_ENTROPY_HIGH * ent.confidence;
    } else if (ent.entropy > config->vf_entropy_mod_thresh) {
        evidence_score += VF_EV_ENTROPY_MOD * ent.confidence;
    }

    /* Spectral flatness contribution - weighted by confidence, low weights to limit FP */
    if (flat.flatness > config->vf_flatness_high_thresh) {
        evidence_score += VF_EV_FLATNESS_HIGH * flat.confidence;
    } else if (flat.flatness > config->vf_flatness_mod_thresh) {
        evidence_score += VF_EV_FLATNESS_MOD * flat.confidence;
    }

    /* Organized rhythm penalties (not scaled - penalties are definitive) */
    float rr_cv = calculate_rr_cv(qrs_result, sample_rate);
    uint8_t is_regular_qrs = (rr_cv >= 0 && rr_cv < config->vf_org_cv_thresh);
    uint8_t freq_outside_vf = (freq.frequency < config->vf_penalty_freq_min ||
                               freq.frequency > config->vf_penalty_freq_max);

    if (is_organized_tachy) {
        evidence_score += VF_EV_ORGANIZED_PENALTY;
    } else if (qrs_rate > config->vf_qrs_rate_penalty_thresh) {
        if (freq_outside_vf) {
            /* Strong penalty: frequency outside VF range indicates organized rhythm */
            evidence_score += VF_EV_QRS_FREQ_PENALTY;
        } else if (is_regular_qrs) {
            /* Weak penalty: VF frequency but regular QRS - could be VF with false detections */
            evidence_score += VF_EV_QRS_REGULAR_PENALTY;
        }
    }

    /* Detection decision based on evidence threshold */
    /* Require minimum amplitude above noise floor to avoid false positives */
    if (evidence_score >= config->vf_evidence_threshold && amplitude > adaptive_threshold) {
        result->vf_detected = 1;
        result->is_fine_vf = (amplitude <= config->vf_fine_amplitude_max) ? 1 : 0;

        /* Confidence via sigmoid of margin above threshold: [0, 1] range */
        float margin = evidence_score - config->vf_evidence_threshold;
        result->confidence = 1.0f / (1.0f + expf(-margin));

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
    if (freq.frequency < config->vf_penalty_freq_min || freq.frequency > config->vf_penalty_freq_max)
        result->diag_fail_reason |= DIAG_FAIL_FREQUENCY;
    if (freq.frequency < 5.0f || freq.frequency > 8.0f)  /* Sub-optimal freq (not 5-8 Hz) */
        result->diag_fail_reason |= DIAG_FAIL_FREQ_SUBOPT;
    if (!qrs_absent && zcr <= 0.25f && tcr.rate <= 0.5f)  /* < VF_EV_HIGH_IRREG */
        result->diag_fail_reason |= DIAG_FAIL_IRREGULARITY;
    if (is_organized_tachy)
        result->diag_fail_reason |= DIAG_FAIL_ORGANIZED;
    if (qrs_rate > config->vf_qrs_rate_penalty_thresh)  /* QRS penalty applied */
        result->diag_fail_reason |= DIAG_FAIL_QRS_PENALTY;
    if (evidence_score >= config->vf_evidence_threshold - 1.0f)
        result->diag_fail_reason |= DIAG_FAIL_CONFIDENCE;
    if (evidence_score < config->vf_evidence_threshold - 1.0f)  /* Well below threshold */
        result->diag_fail_reason |= DIAG_FAIL_LOW_EVIDENCE;
#endif

    return EKG_SUCCESS;
}
