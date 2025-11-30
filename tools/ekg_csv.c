/**
 * EKG CSV File Analyzer Tool
 *
 * Parses EKG signal data from CSV files and outputs analysis results.
 * Uses the EKG Signal Analyzer library for rhythm analysis.
 *
 * For educational and prototype purposes only - NOT for medical use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <getopt.h>

#include "../src/ekg_analyzer.h"
#include "../src/ekg_config.h"
#include "csv_utils.h"

/* Constants */
#define VERSION "1.0"
#define MAX_SAMPLES 100000
#define MAX_LINE_LENGTH 256
#define DEFAULT_WINDOW_SIZE 500

/* Exit codes */
#define EXIT_SUCCESS 0
#define EXIT_FILE_ERROR 1
#define EXIT_FORMAT_ERROR 2
#define EXIT_DATA_ERROR 3
#define EXIT_ANALYSIS_ERROR 4
#define EXIT_MEMORY_ERROR 5

/* Command-line options structure */
typedef struct {
    char filename[512];
    int verbose;
    int window_size;
    int offset;
    int step_size;    /* 0 = single window, >0 = rolling window */
} csv_options_t;

/* Window result storage for rolling window analysis */
typedef struct {
    uint32_t offset;           /* Starting sample index */
    uint32_t window_size;      /* Number of samples in this window */
    ekg_analysis_result_t result;  /* Analysis result from ekg_analyze() */
} window_result_t;

/* Signal statistics for verbose output */
typedef struct {
    int16_t min_val;
    int16_t max_val;
    float mean;
    int32_t peak_to_peak;
} signal_stats_t;

/* Function prototypes */
static void print_help(const char *program_name);
static void print_version(void);
static int parse_arguments(int argc, char *argv[], csv_options_t *options);
static const char *rhythm_type_to_string(uint8_t type);
static const char *rhythm_type_to_short_string(uint8_t type);
static int calculate_num_windows(uint32_t total_samples, int window_size,
                                 int offset, int step_size);
static void print_results(const window_result_t *results, int num_windows,
                                  uint32_t total_samples, const csv_options_t *options,
                                  const int16_t *samples);
static void calculate_signal_stats(const int16_t *samples, uint32_t count,
                                   signal_stats_t *stats);
static void print_error(const char *context);

/* Main function */
int main(int argc, char *argv[])
{
    int exit_code = EXIT_SUCCESS;
    int16_t *samples = NULL;
    csv_options_t options = {0};
    uint32_t total_samples = 0;

    /* Parse command-line arguments */
    if (parse_arguments(argc, argv, &options) != 0) {
        exit_code = EXIT_DATA_ERROR;
        goto cleanup;
    }

    /* Allocate buffer for samples */
    samples = (int16_t *)malloc(MAX_SAMPLES * sizeof(int16_t));
    if (samples == NULL) {
        print_error("Failed to allocate sample buffer");
        exit_code = EXIT_MEMORY_ERROR;
        goto cleanup;
    }

    /* Load CSV file */
    int load_result = load_csv_file(options.filename, samples, &total_samples, MAX_SAMPLES);
    if (load_result != 0) {
        fprintf(stderr, "Error: Failed to load CSV file\n");
        exit_code = EXIT_FILE_ERROR;
        goto cleanup;
    }

    /* Validate sample count */
    if (total_samples < EKG_MIN_SAMPLES) {
        fprintf(stderr, "Error: Too few samples (%u samples, minimum %d required)\n",
                total_samples, EKG_MIN_SAMPLES);
        exit_code = EXIT_DATA_ERROR;
        goto cleanup;
    }

    /* Validate offset */
    if ((uint32_t)options.offset >= total_samples) {
        fprintf(stderr, "Error: Offset (%d) exceeds total samples (%u)\n",
                options.offset, total_samples);
        exit_code = EXIT_DATA_ERROR;
        goto cleanup;
    }

    /* Validate window size fits available samples */
    uint32_t remaining = total_samples - options.offset;
    if (remaining < (uint32_t)options.window_size) {
        fprintf(stderr, "Error: Window size (%d) exceeds available samples (%u)\n",
                options.window_size, remaining);
        exit_code = EXIT_DATA_ERROR;
        goto cleanup;
    }

    /* Calculate number of windows */
    int num_windows;
    int step;

    if (options.step_size == 0) {
        /* Single window mode */
        num_windows = 1;
        step = 0;  /* Doesn't matter, loop runs once */
    } else {
        /* Rolling window mode */
        num_windows = calculate_num_windows(total_samples, options.window_size,
                                           options.offset, options.step_size);
        step = options.step_size;

        if (num_windows == 0) {
            fprintf(stderr, "Error: No complete windows available with given parameters\n");
            exit_code = EXIT_DATA_ERROR;
            goto cleanup;
        }
    }

    /* Allocate results array */
    window_result_t *results = malloc(num_windows * sizeof(window_result_t));
    if (results == NULL) {
        print_error("Failed to allocate results array");
        exit_code = EXIT_MEMORY_ERROR;
        goto cleanup;
    }

    /* Analyze each window */
    int actual_windows = 0;
    for (int i = 0; i < num_windows; i++) {
        uint32_t window_offset = options.offset + (i * step);

        /* Validate window doesn't exceed file bounds */
        if (window_offset + options.window_size > total_samples) {
            break;  /* Stop if we've run out of samples */
        }

        /* Analyze this window */
        int analysis_result = ekg_analyze(&samples[window_offset],
                                         (uint16_t)options.window_size,
                                         &results[i].result,
                                         NULL);  /* Use default config */

        if (analysis_result != EKG_SUCCESS) {
            /* Fail-fast: stop on first error */
            const char *error_msg;
            switch (analysis_result) {
                case EKG_ERROR_NULL_PTR:
                    error_msg = "Internal error (null pointer)";
                    break;
                case EKG_ERROR_INVALID_LEN:
                    error_msg = "Invalid sample count";
                    break;
                case EKG_ERROR_LOW_QUALITY:
                    error_msg = "Signal quality insufficient";
                    break;
                case EKG_ERROR_SATURATED:
                    error_msg = "Signal is saturated";
                    break;
                default:
                    error_msg = "Unknown error";
            }
            fprintf(stderr, "Error: Analysis failed at window %d (samples %u-%u) - %s\n",
                    i, window_offset, window_offset + options.window_size - 1, error_msg);
            free(results);
            exit_code = EXIT_ANALYSIS_ERROR;
            goto cleanup;
        }

        results[i].offset = window_offset;
        results[i].window_size = options.window_size;
        actual_windows++;
    }

    /* Print results - format adapts based on num_windows */
    print_results(results, actual_windows, total_samples, &options, samples);

    free(results);

cleanup:
    if (samples != NULL) {
        free(samples);
    }
    return exit_code;
}

/* Print help message */
static void print_help(const char *program_name)
{
    printf("EKG CSV File Analyzer Tool v%s\n\n", VERSION);
    printf("USAGE:\n");
    printf("    %s [OPTIONS] <filename.csv>\n\n", program_name);
    printf("DESCRIPTION:\n");
    printf("    Analyzes EKG signals from CSV files and provides shock recommendation\n");
    printf("    using the EKG Signal Analyzer library.\n\n");
    printf("ARGUMENTS:\n");
    printf("    <filename.csv>      Path to CSV file containing EKG samples\n");
    printf("                        Format: line_number→value (one sample per line)\n");
    printf("                        Sample rate: 100 Hz\n\n");
    printf("OPTIONS:\n");
    printf("    -v, --verbose       Enable verbose output mode\n");
    printf("    -w, --window <N>    Analysis window size in samples (%d-%d, default: %d)\n",
           EKG_MIN_SAMPLES, EKG_MAX_SAMPLES, DEFAULT_WINDOW_SIZE);
    printf("    -o, --offset <N>    Start offset in samples (default: 0)\n");
    printf("    -s, --step <N>      Step size for rolling window analysis (default: 0)\n");
    printf("                        When set, enables rolling window mode\n");
    printf("                        Common values: 250 (50%% overlap), 100 (high overlap)\n");
    printf("    -h, --help          Display this help message\n");
    printf("    --version           Display version information\n\n");
    printf("CSV FILE FORMAT:\n");
    printf("    Single-column format with line numbers:\n");
    printf("      1→1004\n");
    printf("      2→975\n");
    printf("      3→962\n");
    printf("      ...\n\n");
    printf("EXIT CODES:\n");
    printf("    0    Success\n");
    printf("    1    File error\n");
    printf("    2    Format error\n");
    printf("    3    Data error\n");
    printf("    4    Analysis error\n");
    printf("    5    Memory error\n\n");
    printf("EXAMPLES:\n");
    printf("    # Basic usage (single window)\n");
    printf("    %s data/VFibCoarse.integer.csv\n\n", program_name);
    printf("    # Verbose output\n");
    printf("    %s -v data/Normal100.integer.csv\n\n", program_name);
    printf("    # Custom window at specific offset\n");
    printf("    %s -o 5000 -w 800 data/long_recording.csv\n\n", program_name);
    printf("    # Rolling window with 50%% overlap\n");
    printf("    %s -s 250 data/VFibCoarse.integer.csv\n\n", program_name);
    printf("    # Rolling window with custom window and step\n");
    printf("    %s -w 800 -s 200 data/Normal100.integer.csv\n\n", program_name);
    printf("SAFETY NOTICE:\n");
    printf("    This is an educational/prototype tool. NOT for medical use.\n\n");
}

/* Print version information */
static void print_version(void)
{
    printf("EKG CSV File Analyzer Tool v%s\n", VERSION);
    printf("Educational/Prototype Implementation\n");
}

/* Parse command-line arguments */
static int parse_arguments(int argc, char *argv[], csv_options_t *options)
{
    /* Set defaults */
    options->verbose = 0;
    options->window_size = DEFAULT_WINDOW_SIZE;
    options->offset = 0;
    options->step_size = 0;  /* Default: single window mode */
    options->filename[0] = '\0';

    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"window", required_argument, 0, 'w'},
        {"offset", required_argument, 0, 'o'},
        {"step", required_argument, 0, 's'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "vw:o:s:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                options->verbose = 1;
                break;
            case 'w':
                options->window_size = atoi(optarg);
                if (options->window_size < EKG_MIN_SAMPLES ||
                    options->window_size > EKG_MAX_SAMPLES) {
                    fprintf(stderr, "Error: Window size must be between %d and %d\n",
                            EKG_MIN_SAMPLES, EKG_MAX_SAMPLES);
                    return -1;
                }
                break;
            case 'o':
                options->offset = atoi(optarg);
                if (options->offset < 0) {
                    fprintf(stderr, "Error: Offset must be non-negative\n");
                    return -1;
                }
                break;
            case 's':
                options->step_size = atoi(optarg);
                if (options->step_size < 0) {
                    fprintf(stderr, "Error: Step size must be non-negative\n");
                    return -1;
                }
                break;
            case 'h':
                print_help(argv[0]);
                exit(EXIT_SUCCESS);
            case 'V':
                print_version();
                exit(EXIT_SUCCESS);
            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return -1;
        }
    }

    /* Get filename (should be the only remaining argument) */
    if (optind < argc) {
        strncpy(options->filename, argv[optind], sizeof(options->filename) - 1);
        options->filename[sizeof(options->filename) - 1] = '\0';
    } else {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return -1;
    }

    return 0;
}

/* Convert rhythm type to string */
static const char *rhythm_type_to_string(uint8_t type)
{
    switch (type) {
        case 0: return "Unknown";
        case 1: return "Normal Sinus Rhythm";
        case 2: return "Ventricular Tachycardia (VT)";
        case 3: return "Ventricular Fibrillation (VF)";
        case 4: return "Other";
        default: return "Invalid";
    }
}

/* Convert rhythm type to short string for table display */
static const char *rhythm_type_to_short_string(uint8_t type)
{
    switch (type) {
        case 0: return "Unknown";
        case 1: return "Normal";
        case 2: return "VT";
        case 3: return "VF";
        case 4: return "Other";
        default: return "Invalid";
    }
}

/* Calculate number of windows that fit in the data */
static int calculate_num_windows(uint32_t total_samples, int window_size,
                                 int offset, int step_size)
{
    if ((uint32_t)offset >= total_samples) {
        return 0;
    }

    uint32_t remaining = total_samples - offset;
    if (remaining < (uint32_t)window_size) {
        return 0;
    }

    return ((remaining - window_size) / step_size) + 1;
}

/* Print results with unified format for both single and multi-window modes */
static void print_results(const window_result_t *results, int num_windows,
                          uint32_t total_samples, const csv_options_t *options,
                          const int16_t *samples)
{
    /* Phase 1: Summary Header & Table */
    printf("========================================\n");
    printf("EKG Analysis Results\n");
    printf("========================================\n");
    printf("File: %s\n", options->filename);
    printf("Total samples: %u (%.2f seconds @ 100 Hz)\n",
           total_samples, total_samples / 100.0f);
    printf("Window size: %d samples (%.2f seconds)\n",
           options->window_size, options->window_size / 100.0f);
    printf("Offset: %d\n", options->offset);

    if (num_windows > 1) {
        int overlap = options->window_size - options->step_size;
        printf("Step size: %d samples (overlap: %d samples)\n",
               options->step_size, overlap);
        printf("Windows analyzed: %d\n", num_windows);
    }

    printf("\n");
    printf("Window       | Shock | Conf | Rhythm    | HR\n");
    printf("-------------|-------|------|-----------|----\n");

    for (int i = 0; i < num_windows; i++) {
        const window_result_t *r = &results[i];

        /* Format window range as "from-to" */
        char window_range[32];
        uint32_t end_sample = r->offset + r->window_size - 1;
        snprintf(window_range, sizeof(window_range), "%u-%u", r->offset, end_sample);

        printf("%-12s | %-5s | %3d%% | %-9s | %3d\n",
               window_range,
               r->result.shock_advised ? "YES" : "NO",
               r->result.confidence,
               rhythm_type_to_short_string(r->result.rhythm_type),
               r->result.heart_rate);
    }

    printf("\n");

    /* Phase 2: Detailed Analysis */
    printf("========================================\n");
    if (num_windows == 1) {
        printf("Detailed Analysis\n");
    } else {
        printf("Detailed Analysis: Shock-Advised Windows\n");
    }
    printf("========================================\n\n");

    for (int i = 0; i < num_windows; i++) {
        const window_result_t *r = &results[i];

        /* In multi-window mode, only show shock-advised windows */
        if (num_windows > 1 && !r->result.shock_advised) {
            continue;
        }

        uint32_t end_sample = r->offset + r->window_size - 1;
        float start_time = r->offset / 100.0f;
        float end_time = end_sample / 100.0f;

        printf("Window %u-%u (%.2f-%.2f seconds)\n",
               r->offset, end_sample, start_time, end_time);
        printf("----------------------------------------\n");
        printf("SHOCK ADVISED: %s\n", r->result.shock_advised ? "YES" : "NO");
        printf("Confidence: %d%%\n", r->result.confidence);
        printf("Rhythm type: %s\n", rhythm_type_to_string(r->result.rhythm_type));
        printf("Heart rate: %d bpm", r->result.heart_rate);
        if (r->result.heart_rate == 0 && r->result.rhythm_type == 3) {
            printf(" (not measurable in VF)");
        }
        printf("\n");
        printf("QRS detected: %d\n", r->result.num_qrs_detected);
        printf("Dominant freq: %.2f Hz\n", r->result.dominant_freq);

        /* Verbose mode: signal statistics */
        if (options->verbose) {
            signal_stats_t stats;
            calculate_signal_stats(&samples[r->offset], r->window_size, &stats);

            printf("\n");
            printf("Signal Statistics:\n");
            printf("  Min value: %d\n", stats.min_val);
            printf("  Max value: %d\n", stats.max_val);
            printf("  Mean value: %.1f\n", stats.mean);
            printf("  Peak-to-peak: %d\n", stats.peak_to_peak);
            printf("  DC offset: %.1f\n", stats.mean);
        }

        /* Verbose mode: detection details */
        if (options->verbose) {
            printf("\n");
            printf("Detection Details:\n");

            if (r->result.rhythm_type == 3) {
                printf("  VF Detector: POSITIVE\n");
                printf("    - Dominant frequency: %.2f Hz (range: 4-10 Hz)\n",
                       r->result.dominant_freq);
                printf("    - QRS detected: %d (threshold: low for VF)\n",
                       r->result.num_qrs_detected);
            } else if (r->result.rhythm_type == 2) {
                printf("  VT Detector: POSITIVE\n");
                printf("    - Heart rate: %d bpm (threshold: >120 bpm)\n",
                       r->result.heart_rate);
                printf("    - QRS detected: %d\n", r->result.num_qrs_detected);
            } else if (r->result.rhythm_type == 1) {
                printf("  Normal Rhythm Detected\n");
                printf("    - Heart rate: %d bpm (normal range)\n",
                       r->result.heart_rate);
                printf("    - QRS detected: %d\n", r->result.num_qrs_detected);
            }
        }

        printf("----------------------------------------\n\n");
    }

    /* Phase 3: Overall Summary */
    int shock_count = 0;
    for (int i = 0; i < num_windows; i++) {
        if (results[i].result.shock_advised) {
            shock_count++;
        }
    }

    printf("========================================\n");
    printf("OVERALL RECOMMENDATION: %s\n", shock_count > 0 ? "SHOCK ADVISED" : "NO SHOCK");

    if (num_windows > 1) {
        printf("Shock advised in %d of %d windows (%.1f%%)\n",
               shock_count, num_windows, (100.0f * shock_count) / num_windows);

        if (shock_count > 0) {
            printf("Shock-advised ranges:");
            int first = 1;
            for (int i = 0; i < num_windows; i++) {
                if (results[i].result.shock_advised) {
                    uint32_t end_sample = results[i].offset + results[i].window_size - 1;
                    if (first) {
                        printf(" %u-%u", results[i].offset, end_sample);
                        first = 0;
                    } else {
                        printf(", %u-%u", results[i].offset, end_sample);
                    }
                }
            }
            printf("\n");
        }
    }

    printf("========================================\n");
}

/* Calculate signal statistics */
static void calculate_signal_stats(const int16_t *samples, uint32_t count,
                                   signal_stats_t *stats)
{
    int32_t sum = 0;
    int16_t min = 32767;
    int16_t max = -32768;

    for (uint32_t i = 0; i < count; i++) {
        if (samples[i] < min) min = samples[i];
        if (samples[i] > max) max = samples[i];
        sum += samples[i];
    }

    stats->min_val = min;
    stats->max_val = max;
    stats->mean = (float)sum / (float)count;
    stats->peak_to_peak = (int32_t)max - (int32_t)min;
}

/* Print error message */
static void print_error(const char *context)
{
    fprintf(stderr, "Error: %s\n", context);
}
