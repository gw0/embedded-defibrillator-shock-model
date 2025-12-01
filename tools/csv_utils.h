/**
 * CSV Utilities - Shared CSV file loading functions
 *
 * Provides common CSV parsing functionality for EKG tools.
 */

#ifndef CSV_UTILS_H
#define CSV_UTILS_H

#include <stdint.h>

/**
 * Load CSV file and parse EKG samples
 *
 * Supports two formats:
 *   - With line numbers: "1→1004" or "1,1004"
 *   - Without line numbers: "1004"
 *
 * @param filename  Path to CSV file
 * @param samples   Buffer to store samples (must be pre-allocated)
 * @param count     Output: number of samples loaded
 * @return          0 on success, -1 on error
 */
int load_csv_file(const char *filename, int16_t *samples, uint32_t *count,
                  uint32_t max_samples);

/**
 * Downsample signal from source rate to 100 Hz
 *
 * @param input         Input samples at source_rate
 * @param input_count   Number of input samples
 * @param source_rate   Source sampling rate (Hz)
 * @param output        Output buffer (pre-allocated)
 * @param output_count  Output: samples written
 * @return              0 on success, -1 on error
 */
int downsample_to_100hz(const int16_t *input, uint32_t input_count,
                        uint32_t source_rate,
                        int16_t *output, uint32_t *output_count);

#endif /* CSV_UTILS_H */
