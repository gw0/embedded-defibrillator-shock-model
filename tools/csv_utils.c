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

/* Anti-aliasing filter coefficients for downsampling */
/* 2nd-order Butterworth low-pass, 45Hz cutoff */

/* 200 Hz sampling rate */
#define AA_200HZ_B0  0.2483410790f
#define AA_200HZ_B1  0.4966821579f
#define AA_200HZ_B2  0.2483410790f
#define AA_200HZ_A1  -0.1842138031f
#define AA_200HZ_A2  0.1775781189f

/* 500 Hz sampling rate */
#define AA_500HZ_B0  0.0564484623f
#define AA_500HZ_B1  0.1128969245f
#define AA_500HZ_B2  0.0564484623f
#define AA_500HZ_A1  -1.2246515810f
#define AA_500HZ_A2  0.4504454301f

/* 1000 Hz sampling rate */
#define AA_1000HZ_B0  0.0165819317f
#define AA_1000HZ_B1  0.0331638633f
#define AA_1000HZ_B2  0.0165819317f
#define AA_1000HZ_A1  -1.6041301793f
#define AA_1000HZ_A2  0.6704579060f

/* 2000 Hz sampling rate */
#define AA_2000HZ_B0  0.0045362177f
#define AA_2000HZ_B1  0.0090724354f
#define AA_2000HZ_B2  0.0045362177f
#define AA_2000HZ_A1  -1.8006450571f
#define AA_2000HZ_A2  0.8187899280f

/* 5000 Hz sampling rate */
#define AA_5000HZ_B0  0.0007685117f
#define AA_5000HZ_B1  0.0015370233f
#define AA_5000HZ_B2  0.0007685117f
#define AA_5000HZ_A1  -1.9200682652f
#define AA_5000HZ_A2  0.9231423118f

/* 10000 Hz sampling rate */
#define AA_10000HZ_B0  0.0001959295f
#define AA_10000HZ_B1  0.0003918590f
#define AA_10000HZ_B2  0.0001959295f
#define AA_10000HZ_A1  -1.9600192244f
#define AA_10000HZ_A2  0.9608029424f

typedef struct {
    float b0, b1, b2;
    float a1, a2;
} aa_filter_t;

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

/* Downsample signal from source rate to 100 Hz */
int downsample_to_100hz(const int16_t *input, uint32_t input_count,
                        uint32_t source_rate,
                        int16_t *output, uint32_t *output_count)
{
    if (input == NULL || output == NULL || output_count == NULL) {
        return -1;
    }

    /* Passthrough for 100 Hz */
    if (source_rate == 100) {
        memcpy(output, input, input_count * sizeof(int16_t));
        *output_count = input_count;
        return 0;
    }

    /* Get filter coefficients */
    aa_filter_t filter;
    uint32_t decimation;

    switch (source_rate) {
    case 200:
        filter = (aa_filter_t){AA_200HZ_B0, AA_200HZ_B1, AA_200HZ_B2, AA_200HZ_A1, AA_200HZ_A2};
        decimation = 2;
        break;
    case 500:
        filter = (aa_filter_t){AA_500HZ_B0, AA_500HZ_B1, AA_500HZ_B2, AA_500HZ_A1, AA_500HZ_A2};
        decimation = 5;
        break;
    case 1000:
        filter = (aa_filter_t){AA_1000HZ_B0, AA_1000HZ_B1, AA_1000HZ_B2, AA_1000HZ_A1, AA_1000HZ_A2};
        decimation = 10;
        break;
    case 2000:
        filter = (aa_filter_t){AA_2000HZ_B0, AA_2000HZ_B1, AA_2000HZ_B2, AA_2000HZ_A1, AA_2000HZ_A2};
        decimation = 20;
        break;
    case 5000:
        filter = (aa_filter_t){AA_5000HZ_B0, AA_5000HZ_B1, AA_5000HZ_B2, AA_5000HZ_A1, AA_5000HZ_A2};
        decimation = 50;
        break;
    case 10000:
        filter = (aa_filter_t){AA_10000HZ_B0, AA_10000HZ_B1, AA_10000HZ_B2, AA_10000HZ_A1, AA_10000HZ_A2};
        decimation = 100;
        break;
    default:
        return -1;  /* Unsupported rate */
    }

    /* Allocate temporary buffer for filtered signal */
    float *filtered = (float *)malloc(input_count * sizeof(float));
    if (filtered == NULL) {
        return -1;
    }

    /* Apply anti-aliasing filter (Direct Form II IIR) */
    float w1 = 0.0f, w2 = 0.0f;
    for (uint32_t i = 0; i < input_count; i++) {
        float x = (float)input[i];
        float w0 = x - filter.a1 * w1 - filter.a2 * w2;
        filtered[i] = filter.b0 * w0 + filter.b1 * w1 + filter.b2 * w2;
        w2 = w1;
        w1 = w0;
    }

    /* Decimate */
    uint32_t out_idx = 0;
    for (uint32_t i = 0; i < input_count; i += decimation) {
        float val = roundf(filtered[i]);
        if (val > 32767.0f) val = 32767.0f;
        if (val < -32768.0f) val = -32768.0f;
        output[out_idx++] = (int16_t)val;
    }

    *output_count = out_idx;
    free(filtered);
    return 0;
}
