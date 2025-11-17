/**
 * EKG Signal Analyzer - Heart Rate Analysis Module
 *
 * Implementation of heart rate and R-R interval analysis functions.
 */

#include "heart_rate.h"
#include <stddef.h>
#include <math.h>

float calculate_heart_rate(const uint16_t *rr_intervals, uint8_t num_intervals,
                          uint16_t sample_rate)
{
    if (rr_intervals == NULL || num_intervals == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Calculate heart rate from R-R intervals:
     * 1. Calculate mean R-R interval (in samples)
     * 2. Convert to time (seconds)
     * 3. Convert to heart rate (bpm)
     */

    /* Calculate mean R-R interval */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < num_intervals; i++) {
        sum += rr_intervals[i];
    }
    float mean_rr = (float)sum / (float)num_intervals;

    /* Prevent division by zero */
    if (mean_rr == 0.0f) {
        return 0.0f;
    }

    /* Convert to time in seconds */
    float mean_rr_sec = mean_rr / (float)sample_rate;

    /* Convert to heart rate in bpm */
    float heart_rate = 60.0f / mean_rr_sec;

    return heart_rate;
}

float calculate_rr_regularity(const uint16_t *rr_intervals,
                              uint8_t num_intervals)
{
    if (rr_intervals == NULL || num_intervals == 0) {
        return 0.0f;
    }

    /*
     * Calculate coefficient of variation (CV):
     * CV = (std_dev / mean) × 100%
     *
     * Interpretation:
     *   CV < 5%  : Very regular (normal sinus rhythm)
     *   CV 5-10% : Mostly regular (possibly VT)
     *   CV 10-20%: Irregular
     *   CV > 50% : Very irregular (VF)
     */

    /* Calculate mean */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < num_intervals; i++) {
        sum += rr_intervals[i];
    }
    float mean = (float)sum / (float)num_intervals;

    /* Prevent division by zero */
    if (mean == 0.0f) {
        return 0.0f;
    }

    /* Calculate variance */
    float variance = 0.0f;
    for (uint8_t i = 0; i < num_intervals; i++) {
        float diff = (float)rr_intervals[i] - mean;
        variance += diff * diff;
    }
    variance /= (float)num_intervals;

    /* Calculate standard deviation */
    float std_dev = sqrtf(variance);

    /* Calculate coefficient of variation as percentage */
    float cv = (std_dev / mean) * 100.0f;

    return cv;
}
