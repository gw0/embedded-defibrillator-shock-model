/**
 * EKG Signal Analyzer - Signal Utility Functions
 *
 * Implementation of helper functions for signal quality estimation
 * and Bayesian confidence calculation.
 */

#include "signal_utils.h"
#include "ekg_config.h"
#include <math.h>

float log_odds_to_probability(float log_odds)
{
    /* Clamp to prevent overflow */
    if (log_odds > 20.0f) return 1.0f;
    if (log_odds < -20.0f) return 0.0f;

    return 1.0f / (1.0f + expf(-log_odds));
}

float estimate_signal_quality(const float *signal, size_t num_samples, float sample_rate)
{
    (void)sample_rate;  /* Currently unused, but kept for future enhancements */

    if (signal == NULL || num_samples < 10) {
        return SIGNAL_QUALITY_MIN;
    }

    /* Estimate signal power (variance) */
    float mean = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        mean += signal[i];
    }
    mean /= (float)num_samples;

    float signal_var = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float diff = signal[i] - mean;
        signal_var += diff * diff;
    }
    signal_var /= (float)num_samples;

    /* Estimate noise via high-frequency residual (simple differencing) */
    float noise_var = 0.0f;
    for (size_t i = 1; i < num_samples; i++) {
        float diff = signal[i] - signal[i - 1];
        noise_var += diff * diff;
    }
    noise_var /= (2.0f * (float)(num_samples - 1));  /* Factor of 2 for diff variance */

    if (noise_var < 1e-10f) {
        return 1.0f;  /* Perfect signal */
    }

    float snr = signal_var / noise_var;
    float quality = snr / SNR_GOOD_THRESHOLD;

    /* Clamp to [SIGNAL_QUALITY_MIN, 1.0] */
    if (quality < SIGNAL_QUALITY_MIN) {
        quality = SIGNAL_QUALITY_MIN;
    }
    if (quality > 1.0f) {
        quality = 1.0f;
    }

    return quality;
}
