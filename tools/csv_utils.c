/**
 * CSV Utilities - Implementation
 *
 * Extracted from ekg_csv.c for code reuse.
 */

#include "csv_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#define MAX_LINE_LENGTH 256

/* Forward declarations */
static int parse_line(const char *line, int16_t *value);
static char find_separator(const char *line);

/* Find separator in line (arrow or comma) */
static char find_separator(const char *line)
{
    /* Check for UTF-8 arrow (→) which is 0xE2 0x86 0x92 */
    if (strstr(line, "\xE2\x86\x92") != NULL) {
        return '\xE2';  /* Return first byte as marker */
    }

    /* Check for comma */
    if (strchr(line, ',') != NULL) {
        return ',';
    }

    return '\0';  /* No separator found */
}

/* Parse a single CSV line */
static int parse_line(const char *line, int16_t *value)
{
    char sep = find_separator(line);

    if (sep != '\0') {
        /* Format: line_number<sep>value */
        const char *value_str = strchr(line, sep);
        if (value_str == NULL) {
            return -1;
        }

        /* Skip separator (handle UTF-8 arrow which is 3 bytes) */
        if (sep == '\xE2') {
            /* UTF-8 arrow: skip 3 bytes */
            value_str += 3;
        } else {
            /* Comma: skip 1 byte */
            value_str += 1;
        }

        /* Parse value */
        char *endptr;
        double parsed_value = strtod(value_str, &endptr);

        if (endptr == value_str) {
            return -1;  /* No valid number */
        }

        /* Round and check range */
        int32_t rounded = (int32_t)round(parsed_value);
        if (rounded < -32768 || rounded > 32767) {
            return -1;
        }

        *value = (int16_t)rounded;
    } else {
        /* No separator - treat entire line as value */
        char *endptr;
        double parsed_value = strtod(line, &endptr);

        if (endptr == line) {
            return -1;  /* No valid number */
        }

        /* Round and check range */
        int32_t rounded = (int32_t)round(parsed_value);
        if (rounded < -32768 || rounded > 32767) {
            return -1;
        }

        *value = (int16_t)rounded;
    }

    return 0;
}

/* Load CSV file and parse samples */
int load_csv_file(const char *filename, int16_t *samples, uint32_t *count,
                  uint32_t max_samples)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    *count = 0;

    while (fgets(line, sizeof(line), file) != NULL && *count < max_samples) {
        /* Skip empty lines */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue;
        }

        /* Parse line */
        int16_t value;
        int result = parse_line(line, &value);

        if (result != 0) {
            continue;  /* Skip invalid lines */
        }

        samples[*count] = value;
        (*count)++;
    }

    fclose(file);

    if (*count == 0) {
        return -1;
    }

    return 0;
}
