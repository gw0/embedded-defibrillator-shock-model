/**
 * EKG Signal Analyzer - Heart Rate Analysis Module
 *
 * Implementation of heart rate and R-R interval analysis functions.
 */

#include "heart_rate.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

/* Helper: comparison for qsort */
static int compare_uint16(const void *a, const void *b)
{
    return (int)(*(uint16_t*)a) - (int)(*(uint16_t*)b);
}

float calculate_heart_rate(const uint16_t *rr_intervals, uint8_t num_intervals,
                          uint16_t sample_rate)
{
    if (rr_intervals == NULL || num_intervals == 0 || sample_rate == 0) {
        return 0.0f;
    }

    /*
     * Calculate heart rate from R-R intervals with outlier filtering:
     * 1. Sort R-R intervals and find median
     * 2. Filter outliers (beyond 30% of median)
     * 3. Use median of filtered intervals for heart rate
     * More robust to edge effects than simple mean
     */

    /* Copy and sort intervals */
    uint16_t sorted[50];
    uint8_t n = (num_intervals < 50) ? num_intervals : 50;
    for (uint8_t i = 0; i < n; i++) {
        sorted[i] = rr_intervals[i];
    }
    qsort(sorted, n, sizeof(uint16_t), compare_uint16);

    /* Calculate median */
    float median_rr = (n % 2 == 0) ?
        (sorted[n/2 - 1] + sorted[n/2]) / 2.0f : sorted[n/2];

    if (median_rr == 0.0f) {
        return 0.0f;
    }

    /* Filter outliers: keep within 30% of median */
    float lower = median_rr * 0.7f;
    float upper = median_rr * 1.3f;
    uint16_t filtered[50];
    uint8_t count = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (sorted[i] >= lower && sorted[i] <= upper) {
            filtered[count++] = sorted[i];
        }
    }

    /* Use median of filtered intervals */
    float final_rr = (count > 0) ?
        ((count % 2 == 0) ? (filtered[count/2 - 1] + filtered[count/2]) / 2.0f
                          : filtered[count/2]) : median_rr;

    /* Convert to bpm */
    float heart_rate = 60.0f * (float)sample_rate / final_rr;

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
