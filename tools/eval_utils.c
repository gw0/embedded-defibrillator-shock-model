/**
 * Evaluation Utilities Implementation
 *
 * Shared dataset loading, evaluation, and reporting functions.
 *
 * For educational and prototype purposes only - NOT for medical use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <dirent.h>

#include "eval_utils.h"
#include "csv_utils.h"
#include "../src/ekg_analyzer.h"

/* Internal constants */
#define MAX_FILES 500
#define MAX_SAMPLES 100000

/* ========================================
 * Static Helper Functions
 * ======================================== */

/* Parse rhythm type from filename */
static uint8_t parse_rhythm_type(const char *filename)
{
    if (strstr(filename, "VFib") != NULL) {
        return 3;  /* VF */
    } else if (strstr(filename, "VTach") != NULL) {
        return 2;  /* VT */
    } else if (strstr(filename, "Normal") != NULL) {
        return 1;  /* Normal */
    } else if (strstr(filename, "Asystole") != NULL ||
               strstr(filename, "Afib") != NULL ||
               strstr(filename, "AFib") != NULL ||
               strstr(filename, "AFlutter") != NULL ||
               strstr(filename, "ATach") != NULL) {
        return 4;  /* Other */
    }
    return 4;  /* Default: Other */
}

/* Scan a directory and add files to dataset */
static int scan_directory(const char *dir_path, uint8_t shock_label, dataset_t *dataset)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        fprintf(stderr, "Error: Cannot open directory '%s': %s\n",
                dir_path, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip non-CSV files */
        if (strstr(entry->d_name, ".csv") == NULL) {
            continue;
        }

        /* Check array bounds */
        if (dataset->count >= MAX_FILES) {
            fprintf(stderr, "Warning: Maximum file count reached (%d)\n", MAX_FILES);
            break;
        }

        /* Build full path */
        dataset_file_t *file = &dataset->files[dataset->count];
        snprintf(file->filepath, MAX_PATH_LENGTH, "%s/%s", dir_path, entry->d_name);

        /* Set ground truth labels */
        file->ground_truth_shock = shock_label;
        file->ground_truth_rhythm = parse_rhythm_type(entry->d_name);

        /* Update counters */
        dataset->count++;
        if (shock_label) {
            dataset->shock_count++;
        } else {
            dataset->noshock_count++;
        }
    }

    closedir(dir);
    return 0;
}

/* Calculate number of windows that fit in the data */
static int calculate_num_windows(uint32_t total_samples, int window_size, int step_size)
{
    if (total_samples < (uint32_t)window_size) {
        return 0;
    }
    return ((total_samples - window_size) / step_size) + 1;
}

/* ========================================
 * Dataset Loading Functions
 * ======================================== */

/* Load complete dataset from base path */
int load_dataset(const char *base_path, dataset_t *dataset)
{
    /* Allocate file array */
    dataset->files = malloc(MAX_FILES * sizeof(dataset_file_t));
    if (dataset->files == NULL) {
        fprintf(stderr, "Error: Failed to allocate dataset memory\n");
        return -1;
    }

    dataset->count = 0;
    dataset->shock_count = 0;
    dataset->noshock_count = 0;

    /* Build directory paths */
    char shock_path[MAX_PATH_LENGTH];
    char noshock_path[MAX_PATH_LENGTH];

    snprintf(shock_path, MAX_PATH_LENGTH, "%s/SHOCK", base_path);
    snprintf(noshock_path, MAX_PATH_LENGTH, "%s/NOSHOCK", base_path);

    /* Scan both directories */
    if (scan_directory(shock_path, 1, dataset) != 0) {
        free(dataset->files);
        return -1;
    }

    if (scan_directory(noshock_path, 0, dataset) != 0) {
        free(dataset->files);
        return -1;
    }

    if (dataset->count == 0) {
        fprintf(stderr, "Error: No files found in dataset\n");
        free(dataset->files);
        return -1;
    }

    return 0;
}

/* Free dataset */
void free_dataset(dataset_t *dataset)
{
    if (dataset->files != NULL) {
        free(dataset->files);
        dataset->files = NULL;
    }
}

/* ========================================
 * Evaluation Functions
 * ======================================== */

/* Calculate derived metrics from confusion matrix */
void calculate_metrics(metrics_t *metrics)
{
    uint32_t total = metrics->tp + metrics->tn + metrics->fp + metrics->fn;

    if (total == 0) {
        metrics->accuracy = 0.0f;
        metrics->sensitivity = 0.0f;
        metrics->specificity = 0.0f;
        metrics->precision = 0.0f;
        metrics->f1_score = 0.0f;
        return;
    }

    metrics->accuracy = (float)(metrics->tp + metrics->tn) / total;

    if (metrics->tp + metrics->fn > 0) {
        metrics->sensitivity = (float)metrics->tp / (metrics->tp + metrics->fn);
    } else {
        metrics->sensitivity = 0.0f;
    }

    if (metrics->tn + metrics->fp > 0) {
        metrics->specificity = (float)metrics->tn / (metrics->tn + metrics->fp);
    } else {
        metrics->specificity = 0.0f;
    }

    if (metrics->tp + metrics->fp > 0) {
        metrics->precision = (float)metrics->tp / (metrics->tp + metrics->fp);
    } else {
        metrics->precision = 0.0f;
    }

    if (metrics->precision + metrics->sensitivity > 0) {
        metrics->f1_score = 2.0f * (metrics->precision * metrics->sensitivity) /
                           (metrics->precision + metrics->sensitivity);
    } else {
        metrics->f1_score = 0.0f;
    }
}

/* Evaluate configuration on dataset subset */
void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
                    const uint32_t *file_indices, uint32_t num_files,
                    metrics_t *metrics, uint32_t source_sample_rate)
{
    int16_t *raw_samples = NULL;
    uint32_t num_raw_samples = 0;
    int16_t *samples = NULL;
    uint32_t num_samples = 0;

    /* Reset metrics */
    memset(metrics, 0, sizeof(metrics_t));

    /* Allocate buffers */
    raw_samples = (int16_t *)malloc(MAX_SAMPLES * sizeof(int16_t));
    if (raw_samples == NULL) {
        return;
    }

    if (source_sample_rate != 100) {
        samples = (int16_t *)malloc(MAX_SAMPLES * sizeof(int16_t));
        if (samples == NULL) {
            free(raw_samples);
            return;
        }
    }

    /* Evaluate each file */
    for (uint32_t i = 0; i < num_files; i++) {
        uint32_t file_idx = file_indices ? file_indices[i] : i;
        if (file_idx >= dataset->count) {
            continue;
        }

        const dataset_file_t *file = &dataset->files[file_idx];

        /* Load CSV */
        if (load_csv_file(file->filepath, raw_samples, &num_raw_samples, MAX_SAMPLES) != 0) {
            continue;
        }

        /* Downsample if needed */
        if (source_sample_rate != 100) {
            if (downsample_to_100hz(raw_samples, num_raw_samples, source_sample_rate,
                                    samples, &num_samples) != 0) {
                continue;
            }
        } else {
            samples = raw_samples;
            num_samples = num_raw_samples;
        }

        /* Skip if too few samples for even one window */
        if (num_samples < WINDOW_SIZE) {
            continue;
        }

        /* Calculate number of windows */
        int num_windows = calculate_num_windows(num_samples, WINDOW_SIZE, WINDOW_STEP);
        if (num_windows == 0) {
            continue;
        }

        /* Any-positive aggregation: file classified as SHOCK if ANY window advises shock */
        uint8_t file_shock_advised = 0;

        /* Analyze each window */
        for (int w = 0; w < num_windows; w++) {
            uint32_t window_offset = w * WINDOW_STEP;

            /* Boundary check */
            if (window_offset + WINDOW_SIZE > num_samples) {
                break;
            }

            /* Analyze this window */
            ekg_analysis_result_t result;
            int ret = ekg_analyze(&samples[window_offset], WINDOW_SIZE, &result, config);

            if (ret != EKG_SUCCESS) {
                continue;
            }

            /* Any-positive: if any window advises shock, mark file as shock */
            if (result.shock_advised) {
                file_shock_advised = 1;
                break;  /* Early exit optimization */
            }
        }

        /* Update confusion matrix based on aggregated file-level result */
        if (file_shock_advised && file->ground_truth_shock) {
            metrics->tp++;
        } else if (!file_shock_advised && !file->ground_truth_shock) {
            metrics->tn++;
        } else if (file_shock_advised && !file->ground_truth_shock) {
            metrics->fp++;
        } else if (!file_shock_advised && file->ground_truth_shock) {
            metrics->fn++;
        }
    }

    free(raw_samples);
    if (source_sample_rate != 100) {
        free(samples);
    }

    /* Calculate derived metrics */
    calculate_metrics(metrics);
}

/* ========================================
 * Reporting Functions
 * ======================================== */

/* Print metrics */
void print_metrics(const metrics_t *metrics)
{
    printf("  Accuracy:    %.1f%%\n", metrics->accuracy * 100.0f);
    printf("  Sensitivity: %.1f%%\n", metrics->sensitivity * 100.0f);
    printf("  Specificity: %.1f%%\n", metrics->specificity * 100.0f);
    printf("  Precision:   %.1f%%\n", metrics->precision * 100.0f);
    printf("  F1-Score:    %.1f%%\n", metrics->f1_score * 100.0f);
    printf("  Confusion: TP=%u TN=%u FP=%u FN=%u\n",
           metrics->tp, metrics->tn, metrics->fp, metrics->fn);
}

/* Print configuration parameters */
void print_config_params(const ekg_config_t *config)
{
    printf("Configuration:\n");
    printf("  VT_HR_THRESHOLD:      %.1f bpm\n", config->vt_hr_threshold);
    printf("  VT_QRS_WIDTH:         %.1f ms\n", config->vt_qrs_width_ms);
    printf("  VT_REGULARITY_CV:     %.1f%%\n", config->vt_regularity_cv);
    printf("  VT_CONFIDENCE:        %.2f\n", config->vt_confidence_threshold);
    printf("  VF_FREQ_MIN:          %.1f Hz\n", config->vf_freq_min);
    printf("  VF_FREQ_MAX:          %.1f Hz\n", config->vf_freq_max);
    printf("  VF_ZCR_THRESHOLD:     %.2f\n", config->vf_zcr_threshold);
    printf("  VF_CONFIDENCE:        %.2f\n", config->vf_confidence_threshold);
}
