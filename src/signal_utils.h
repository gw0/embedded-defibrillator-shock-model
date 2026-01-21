/**
 * EKG Signal Analyzer - Signal Utility Functions
 *
 * Helper functions for signal quality estimation and
 * Bayesian confidence calculation.
 */

#ifndef SIGNAL_UTILS_H
#define SIGNAL_UTILS_H

#include <stddef.h>

/**
 * Convert log-odds to probability.
 *
 * Uses the logistic function: P = 1 / (1 + exp(-log_odds))
 * Clamps extreme values to prevent overflow.
 *
 * @param log_odds Log-odds value (can be negative)
 * @return Probability in range [0.0, 1.0]
 */
float log_odds_to_probability(float log_odds);

/**
 * Estimate signal quality based on signal-to-noise ratio.
 *
 * Uses differencing to estimate high-frequency noise content.
 * Returns a quality factor to scale confidence values.
 *
 * @param signal Pointer to signal samples
 * @param num_samples Number of samples in signal
 * @param sample_rate Sampling rate in Hz
 * @return Quality factor in range [SIGNAL_QUALITY_MIN, 1.0]
 */
float estimate_signal_quality(const float *signal, size_t num_samples, float sample_rate);

#endif /* SIGNAL_UTILS_H */
