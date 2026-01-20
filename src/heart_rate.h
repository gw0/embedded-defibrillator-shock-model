/**
 * EKG Signal Analyzer - Heart Rate Analysis Module
 *
 * Functions for calculating heart rate and R-R interval regularity.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#ifndef HEART_RATE_H
#define HEART_RATE_H

#include <stdint.h>

/**
 * Calculate heart rate from R-R intervals
 *
 * @param rr_intervals Array of R-R intervals (in samples)
 * @param num_intervals Number of R-R intervals
 * @param sample_rate  Sample rate in Hz (100)
 * @return             Heart rate in bpm, or 0 if cannot calculate
 */
float calculate_heart_rate(const uint16_t *rr_intervals, uint8_t num_intervals,
                          uint16_t sample_rate);

/**
 * Calculate R-R interval regularity (coefficient of variation)
 *
 * @param rr_intervals Array of R-R intervals (in samples)
 * @param num_intervals Number of R-R intervals
 * @return             Coefficient of variation as percentage (0-100+)
 */
float calculate_rr_regularity(const uint16_t *rr_intervals,
                              uint8_t num_intervals);

#endif /* HEART_RATE_H */
