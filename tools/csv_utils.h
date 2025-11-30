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

#endif /* CSV_UTILS_H */
