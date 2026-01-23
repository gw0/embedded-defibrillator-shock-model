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

float estimate_signal_quality(const float *signal, size_t num_samples,
                              float sample_rate, const ekg_config_t *config)
{
    (void)sample_rate;  /* Currently unused, but kept for future enhancements */

    /* Use default config if NULL */
    if (config == NULL) {
        config = &EKG_DEFAULT_CONFIG;
    }

    if (signal == NULL || num_samples < 10) {
        return config->signal_quality_min;
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
    float quality = snr / config->snr_good_thresh;

    /* Clamp to [signal_quality_min, 1.0] */
    if (quality < config->signal_quality_min) {
        quality = config->signal_quality_min;
    }
    if (quality > 1.0f) {
        quality = 1.0f;
    }

    return quality;
}
