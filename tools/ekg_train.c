/**
 * EKG Training & Evaluation Tool
 *
 * Performs grid search optimization to find optimal EKG analyzer parameters.
 * Uses real-world dataset with ground truth labels.
 *
 * For educational and prototype purposes only - NOT for medical use.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../src/ekg_analyzer.h"
#include "../src/ekg_config.h"
#include "csv_utils.h"

/* Constants */
#define MAX_FILES 500
#define MAX_SAMPLES 100000
#define MAX_PATH_LENGTH 512

/* Rolling-window parameters */
#define WINDOW_SIZE 500    /* 5 seconds at 100 Hz */
#define WINDOW_STEP 250    /* 2.5 seconds (50% overlap) */

/* ========================================
 * Data Structures
 * ======================================== */

/* Dataset file entry */
typedef struct {
    char filepath[MAX_PATH_LENGTH];
    uint8_t ground_truth_shock;   /* 0=NOSHOCK, 1=SHOCK */
    uint8_t ground_truth_rhythm;  /* 0=Unknown, 1=Normal, 2=VT, 3=VF, 4=Other */
} dataset_file_t;

/* Dataset collection */
typedef struct {
    dataset_file_t *files;
    uint32_t count;
    uint32_t shock_count;
    uint32_t noshock_count;
} dataset_t;

/* Confusion matrix and metrics */
typedef struct {
    uint32_t tp, tn, fp, fn;
    float accuracy;
    float sensitivity;
    float specificity;
    float precision;
    float f1_score;
    uint32_t rhythm_confusion[5][5];
} metrics_t;

/* Parameter grid */
typedef struct {
    float *vt_hr_values;      uint32_t vt_hr_count;
    float *vt_qrs_values;     uint32_t vt_qrs_count;
    float *vt_reg_values;     uint32_t vt_reg_count;
    float *vt_conf_values;    uint32_t vt_conf_count;
    float *vf_fmin_values;    uint32_t vf_fmin_count;
    float *vf_fmax_values;    uint32_t vf_fmax_count;
    float *vf_zcr_values;     uint32_t vf_zcr_count;
    float *vf_conf_values;    uint32_t vf_conf_count;
} param_grid_t;

/* Progress tracker */
typedef struct {
    time_t start_time;
    uint64_t total_combinations;
    uint64_t completed;
    metrics_t best_metrics;
    ekg_config_t best_config;
    FILE *csv_file;  /* For writing results incrementally */
} progress_t;

/* ========================================
 * Function Prototypes
 * ======================================== */

/* Dataset loading */
static uint8_t parse_rhythm_type(const char *filename);
static int load_dataset(const char *base_path, dataset_t *dataset);
static int scan_directory(const char *dir_path, uint8_t shock_label, dataset_t *dataset);
static void free_dataset(dataset_t *dataset);

/* Metrics */
static void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
                           const uint32_t *file_indices, uint32_t num_files,
                           metrics_t *metrics, uint32_t source_sample_rate);
static void calculate_metrics(metrics_t *metrics);
static void print_metrics(const metrics_t *metrics);

/* Grid search */
static void init_default_grid(param_grid_t *grid);
static void free_grid(param_grid_t *grid);
static uint64_t grid_size(const param_grid_t *grid);
static void grid_get_config(const param_grid_t *grid, uint64_t index, ekg_config_t *config);

/* Search strategies */
static void search_random(const param_grid_t *grid, const dataset_t *dataset,
                         uint32_t num_samples, progress_t *progress, uint32_t source_sample_rate);
static void search_exhaustive(const param_grid_t *grid, const dataset_t *dataset,
                              progress_t *progress, uint32_t source_sample_rate);
static void search_adaptive(const param_grid_t *grid, const dataset_t *dataset,
                           progress_t *progress, uint32_t source_sample_rate);

/* Output generation */
static int write_best_config_header(const char *filename, const ekg_config_t *config,
                                    const metrics_t *metrics);

/* Utilities */
static void print_progress(const progress_t *progress);
static void print_help(const char *prog_name);

/* ========================================
 * Main Function
 * ======================================== */

int main(int argc, char *argv[])
{
    /* Default options */
    const char *strategy = "random";
    const char *output_file = "outputs/results.csv";
    const char *config_file = "outputs/best_config.h";
    uint32_t num_combinations = 500;
    uint32_t source_sample_rate = 100;  /* Default: no downsampling */

    /* Parse arguments */
    static struct option long_options[] = {
        {"strategy", required_argument, 0, 's'},
        {"num-samples", required_argument, 0, 'n'},
        {"output", required_argument, 0, 'o'},
        {"config", required_argument, 0, 'c'},
        {"sample-rate", required_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:n:o:c:r:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 's': strategy = optarg; break;
            case 'n': num_combinations = atoi(optarg); break;
            case 'o': output_file = optarg; break;
            case 'c': config_file = optarg; break;
            case 'r':
                source_sample_rate = atoi(optarg);
                if (source_sample_rate != 100 &&
                    source_sample_rate != 200 &&
                    source_sample_rate != 500 &&
                    source_sample_rate != 1000 &&
                    source_sample_rate != 2000 &&
                    source_sample_rate != 5000 &&
                    source_sample_rate != 10000) {
                    fprintf(stderr, "Error: Supported sample rates: 100, 200, 500, 1000, 2000, 5000, 10000 Hz\n");
                    return 1;
                }
                break;
            case 'h': print_help(argv[0]); return 0;
            default: print_help(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: Dataset path required\n\n");
        print_help(argv[0]);
        return 1;
    }

    const char *dataset_path = argv[optind];

    /* Create outputs directory if it doesn't exist */
    #ifdef _WIN32
        mkdir("outputs");
    #else
        mkdir("outputs", 0755);
    #endif

    /* Load dataset */
    printf("========================================\n");
    printf("EKG Training & Evaluation Tool\n");
    printf("========================================\n");
    printf("Loading dataset from: %s\n", dataset_path);

    dataset_t dataset = {0};
    if (load_dataset(dataset_path, &dataset) != 0) {
        fprintf(stderr, "Error: Failed to load dataset\n");
        return 1;
    }

    printf("Files loaded: %u (%u SHOCK, %u NOSHOCK)\n",
           dataset.count, dataset.shock_count, dataset.noshock_count);

    /* Initialize parameter grid */
    param_grid_t grid;
    init_default_grid(&grid);

    uint64_t total = grid_size(&grid);
    printf("Grid size: %lu combinations\n", total);
    printf("\n");

    /* Run search strategy */
    progress_t progress = {0};
    progress.start_time = time(NULL);

    /* Open CSV output file */
    progress.csv_file = fopen(output_file, "w");
    if (progress.csv_file != NULL) {
        fprintf(progress.csv_file, "vt_hr,vt_qrs,vt_reg,vt_conf,vf_min,vf_max,vf_zcr,vf_conf,"
                "accuracy,sensitivity,specificity,precision,f1,tp,tn,fp,fn\n");
    }

    if (strcmp(strategy, "exhaustive") == 0) {
        progress.total_combinations = grid_size(&grid);
        printf("Starting exhaustive search (%lu combinations)...\n", progress.total_combinations);
        search_exhaustive(&grid, &dataset, &progress, source_sample_rate);
    } else if (strcmp(strategy, "adaptive") == 0) {
        printf("Starting adaptive search...\n");
        search_adaptive(&grid, &dataset, &progress, source_sample_rate);
    } else {
        progress.total_combinations = num_combinations;
        printf("Starting random search (%u combinations)...\n", num_combinations);
        search_random(&grid, &dataset, num_combinations, &progress, source_sample_rate);
    }

    if (progress.csv_file != NULL) {
        fclose(progress.csv_file);
        printf("\nResults saved to: %s\n", output_file);
    }

    printf("\n========================================\n");
    printf("BEST CONFIGURATION FOUND\n");
    printf("========================================\n");
    printf("VT_HR_THRESHOLD: %.1f bpm\n", progress.best_config.vt_hr_threshold);
    printf("VT_QRS_WIDTH_MS: %.1f ms\n", progress.best_config.vt_qrs_width_ms);
    printf("VT_REGULARITY_CV: %.1f%%\n", progress.best_config.vt_regularity_cv);
    printf("VT_CONFIDENCE_THRESHOLD: %.2f\n", progress.best_config.vt_confidence_threshold);
    printf("VF_FREQ_MIN: %.1f Hz\n", progress.best_config.vf_freq_min);
    printf("VF_FREQ_MAX: %.1f Hz\n", progress.best_config.vf_freq_max);
    printf("VF_ZCR_THRESHOLD: %.2f\n", progress.best_config.vf_zcr_threshold);
    printf("VF_CONFIDENCE_THRESHOLD: %.2f\n", progress.best_config.vf_confidence_threshold);
    printf("\n");

    printf("PERFORMANCE METRICS:\n");
    print_metrics(&progress.best_metrics);
    printf("========================================\n");

    /* Write best config header */
    if (write_best_config_header(config_file, &progress.best_config, &progress.best_metrics) == 0) {
        printf("\nBest config saved to: %s\n", config_file);
    }

    /* Cleanup */
    free_grid(&grid);
    free_dataset(&dataset);

    return 0;
}

/* ========================================
 * Dataset Loading Implementation
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

/* Load complete dataset from base path */
static int load_dataset(const char *base_path, dataset_t *dataset)
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
static void free_dataset(dataset_t *dataset)
{
    if (dataset->files != NULL) {
        free(dataset->files);
        dataset->files = NULL;
    }
}

/* ========================================
 * Metrics Implementation
 * ======================================== */

/* Calculate number of windows that fit in the data */
static int calculate_num_windows(uint32_t total_samples, int window_size, int step_size)
{
    if (total_samples < (uint32_t)window_size) {
        return 0;
    }
    return ((total_samples - window_size) / step_size) + 1;
}

/* Calculate derived metrics from confusion matrix */
static void calculate_metrics(metrics_t *metrics)
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
static void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
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

/* Print metrics */
static void print_metrics(const metrics_t *metrics)
{
    printf("  Accuracy:    %.1f%%\n", metrics->accuracy * 100.0f);
    printf("  Sensitivity: %.1f%%\n", metrics->sensitivity * 100.0f);
    printf("  Specificity: %.1f%%\n", metrics->specificity * 100.0f);
    printf("  Precision:   %.1f%%\n", metrics->precision * 100.0f);
    printf("  F1-Score:    %.1f%%\n", metrics->f1_score * 100.0f);
    printf("  Confusion: TP=%u TN=%u FP=%u FN=%u\n",
           metrics->tp, metrics->tn, metrics->fp, metrics->fn);
}

/* ========================================
 * Grid Search Implementation
 * ======================================== */

/* Initialize default parameter grid */
static void init_default_grid(param_grid_t *grid)
{
    /* VT_HR_THRESHOLD: 100-140 step 5 (9 values) */
    grid->vt_hr_count = 9;
    grid->vt_hr_values = malloc(grid->vt_hr_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_hr_count; i++) {
        grid->vt_hr_values[i] = 100.0f + i * 5.0f;
    }

    /* VT_QRS_WIDTH_MS: 100-140 step 10 (5 values) */
    grid->vt_qrs_count = 5;
    grid->vt_qrs_values = malloc(grid->vt_qrs_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_qrs_count; i++) {
        grid->vt_qrs_values[i] = 100.0f + i * 10.0f;
    }

    /* VT_REGULARITY_CV: 15-30 step 5 (4 values) */
    grid->vt_reg_count = 4;
    grid->vt_reg_values = malloc(grid->vt_reg_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_reg_count; i++) {
        grid->vt_reg_values[i] = 15.0f + i * 5.0f;
    }

    /* VT_CONFIDENCE_THRESHOLD: 0.5-0.9 step 0.1 (5 values) */
    grid->vt_conf_count = 5;
    grid->vt_conf_values = malloc(grid->vt_conf_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_conf_count; i++) {
        grid->vt_conf_values[i] = 0.5f + i * 0.1f;
    }

    /* VF_FREQ_MIN: 3.5-5.0 step 0.5 (4 values) */
    grid->vf_fmin_count = 4;
    grid->vf_fmin_values = malloc(grid->vf_fmin_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_fmin_count; i++) {
        grid->vf_fmin_values[i] = 3.5f + i * 0.5f;
    }

    /* VF_FREQ_MAX: 9.0-12.0 step 0.5 (7 values) */
    grid->vf_fmax_count = 7;
    grid->vf_fmax_values = malloc(grid->vf_fmax_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_fmax_count; i++) {
        grid->vf_fmax_values[i] = 9.0f + i * 0.5f;
    }

    /* VF_ZCR_THRESHOLD: 0.10-0.25 step 0.05 (4 values) */
    grid->vf_zcr_count = 4;
    grid->vf_zcr_values = malloc(grid->vf_zcr_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_zcr_count; i++) {
        grid->vf_zcr_values[i] = 0.10f + i * 0.05f;
    }

    /* VF_CONFIDENCE_THRESHOLD: 0.5-0.9 step 0.1 (5 values) */
    grid->vf_conf_count = 5;
    grid->vf_conf_values = malloc(grid->vf_conf_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_conf_count; i++) {
        grid->vf_conf_values[i] = 0.5f + i * 0.1f;
    }
}

/* Free grid memory */
static void free_grid(param_grid_t *grid)
{
    free(grid->vt_hr_values);
    free(grid->vt_qrs_values);
    free(grid->vt_reg_values);
    free(grid->vt_conf_values);
    free(grid->vf_fmin_values);
    free(grid->vf_fmax_values);
    free(grid->vf_zcr_values);
    free(grid->vf_conf_values);
}

/* Calculate total grid size */
static uint64_t grid_size(const param_grid_t *grid)
{
    return (uint64_t)grid->vt_hr_count *
           grid->vt_qrs_count *
           grid->vt_reg_count *
           grid->vt_conf_count *
           grid->vf_fmin_count *
           grid->vf_fmax_count *
           grid->vf_zcr_count *
           grid->vf_conf_count;
}

/* Get configuration at grid index */
static void grid_get_config(const param_grid_t *grid, uint64_t index, ekg_config_t *config)
{
    /* Decode multi-dimensional index */
    uint64_t idx = index;

    uint32_t i7 = idx % grid->vf_conf_count;
    idx /= grid->vf_conf_count;

    uint32_t i6 = idx % grid->vf_zcr_count;
    idx /= grid->vf_zcr_count;

    uint32_t i5 = idx % grid->vf_fmax_count;
    idx /= grid->vf_fmax_count;

    uint32_t i4 = idx % grid->vf_fmin_count;
    idx /= grid->vf_fmin_count;

    uint32_t i3 = idx % grid->vt_conf_count;
    idx /= grid->vt_conf_count;

    uint32_t i2 = idx % grid->vt_reg_count;
    idx /= grid->vt_reg_count;

    uint32_t i1 = idx % grid->vt_qrs_count;
    idx /= grid->vt_qrs_count;

    uint32_t i0 = idx % grid->vt_hr_count;

    /* Build configuration */
    config->vt_hr_threshold = grid->vt_hr_values[i0];
    config->vt_qrs_width_ms = grid->vt_qrs_values[i1];
    config->vt_regularity_cv = grid->vt_reg_values[i2];
    config->vt_confidence_threshold = grid->vt_conf_values[i3];
    config->vf_freq_min = grid->vf_fmin_values[i4];
    config->vf_freq_max = grid->vf_fmax_values[i5];
    config->vf_zcr_threshold = grid->vf_zcr_values[i6];
    config->vf_confidence_threshold = grid->vf_conf_values[i7];
    config->vf_min_amplitude = VF_MIN_AMPLITUDE;  /* Not tuned */
}

/* ========================================
 * Search Strategies
 * ======================================== */

/* Random search */
static void search_random(const param_grid_t *grid, const dataset_t *dataset,
                         uint32_t num_combinations, progress_t *progress, uint32_t source_sample_rate)
{
    srand(42);  /* Fixed seed for reproducibility */

    uint64_t total_size = grid_size(grid);
    progress->best_metrics.f1_score = 0.0f;

    for (uint32_t i = 0; i < num_combinations; i++) {
        /* Random configuration */
        uint64_t random_idx = ((uint64_t)rand() * rand()) % total_size;

        ekg_config_t config;
        grid_get_config(grid, random_idx, &config);

        /* Evaluate */
        metrics_t metrics;
        evaluate_config(&config, dataset, NULL, dataset->count, &metrics, source_sample_rate);

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
                    metrics.accuracy, metrics.sensitivity, metrics.specificity,
                    metrics.precision, metrics.f1_score,
                    metrics.tp, metrics.tn, metrics.fp, metrics.fn);
        }

        /* Update best */
        if (metrics.f1_score > progress->best_metrics.f1_score) {
            progress->best_metrics = metrics;
            progress->best_config = config;
        }

        progress->completed = i + 1;

        /* Print progress every 10 combinations */
        if ((i + 1) % 10 == 0 || i == num_combinations - 1) {
            print_progress(progress);
        }
    }
}

/* Exhaustive search */
static void search_exhaustive(const param_grid_t *grid, const dataset_t *dataset,
                              progress_t *progress, uint32_t source_sample_rate)
{
    uint64_t total = grid_size(grid);
    progress->best_metrics.f1_score = 0.0f;

    for (uint64_t i = 0; i < total; i++) {
        ekg_config_t config;
        grid_get_config(grid, i, &config);

        /* Evaluate */
        metrics_t metrics;
        evaluate_config(&config, dataset, NULL, dataset->count, &metrics, source_sample_rate);

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
                    metrics.accuracy, metrics.sensitivity, metrics.specificity,
                    metrics.precision, metrics.f1_score,
                    metrics.tp, metrics.tn, metrics.fp, metrics.fn);
        }

        /* Update best */
        if (metrics.f1_score > progress->best_metrics.f1_score) {
            progress->best_metrics = metrics;
            progress->best_config = config;
        }

        progress->completed = i + 1;

        /* Print progress every 100 iterations or at end */
        if ((i + 1) % 100 == 0 || i == total - 1) {
            print_progress(progress);
        }
    }
}

/* Adaptive search */
static void search_adaptive(const param_grid_t *grid, const dataset_t *dataset,
                           progress_t *progress, uint32_t source_sample_rate)
{
    /* Phase 1: Coarse search (double step sizes) */
    param_grid_t coarse_grid;

    /* Create coarse grid with larger steps */
    coarse_grid.vt_hr_count = (grid->vt_hr_count + 1) / 2;
    coarse_grid.vt_hr_values = malloc(coarse_grid.vt_hr_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vt_hr_count; i++) {
        coarse_grid.vt_hr_values[i] = grid->vt_hr_values[i * 2];
    }

    coarse_grid.vt_qrs_count = (grid->vt_qrs_count + 1) / 2;
    coarse_grid.vt_qrs_values = malloc(coarse_grid.vt_qrs_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vt_qrs_count; i++) {
        coarse_grid.vt_qrs_values[i] = grid->vt_qrs_values[i < grid->vt_qrs_count ? i * 2 : grid->vt_qrs_count - 1];
    }

    coarse_grid.vt_reg_count = (grid->vt_reg_count + 1) / 2;
    coarse_grid.vt_reg_values = malloc(coarse_grid.vt_reg_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vt_reg_count; i++) {
        coarse_grid.vt_reg_values[i] = grid->vt_reg_values[i * 2];
    }

    coarse_grid.vt_conf_count = (grid->vt_conf_count + 1) / 2;
    coarse_grid.vt_conf_values = malloc(coarse_grid.vt_conf_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vt_conf_count; i++) {
        coarse_grid.vt_conf_values[i] = grid->vt_conf_values[i < grid->vt_conf_count ? i * 2 : grid->vt_conf_count - 1];
    }

    coarse_grid.vf_fmin_count = (grid->vf_fmin_count + 1) / 2;
    coarse_grid.vf_fmin_values = malloc(coarse_grid.vf_fmin_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vf_fmin_count; i++) {
        coarse_grid.vf_fmin_values[i] = grid->vf_fmin_values[i * 2];
    }

    coarse_grid.vf_fmax_count = (grid->vf_fmax_count + 1) / 2;
    coarse_grid.vf_fmax_values = malloc(coarse_grid.vf_fmax_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vf_fmax_count; i++) {
        coarse_grid.vf_fmax_values[i] = grid->vf_fmax_values[i < grid->vf_fmax_count ? i * 2 : grid->vf_fmax_count - 1];
    }

    coarse_grid.vf_zcr_count = (grid->vf_zcr_count + 1) / 2;
    coarse_grid.vf_zcr_values = malloc(coarse_grid.vf_zcr_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vf_zcr_count; i++) {
        coarse_grid.vf_zcr_values[i] = grid->vf_zcr_values[i * 2];
    }

    coarse_grid.vf_conf_count = (grid->vf_conf_count + 1) / 2;
    coarse_grid.vf_conf_values = malloc(coarse_grid.vf_conf_count * sizeof(float));
    for (uint32_t i = 0; i < coarse_grid.vf_conf_count; i++) {
        coarse_grid.vf_conf_values[i] = grid->vf_conf_values[i < grid->vf_conf_count ? i * 2 : grid->vf_conf_count - 1];
    }

    uint64_t coarse_size = grid_size(&coarse_grid);
    progress->total_combinations = coarse_size + 1000;  /* Estimate */

    printf("Phase 1: Coarse search (%lu combinations)\n", coarse_size);

    /* Run coarse search */
    progress_t coarse_progress = {0};
    coarse_progress.start_time = progress->start_time;
    coarse_progress.total_combinations = coarse_size;
    coarse_progress.csv_file = progress->csv_file;

    search_exhaustive(&coarse_grid, dataset, &coarse_progress, source_sample_rate);

    /* Use best from coarse search */
    progress->best_config = coarse_progress.best_config;
    progress->best_metrics = coarse_progress.best_metrics;
    progress->completed = coarse_size;

    printf("\nPhase 2: Fine search around best (random sampling)\n");

    /* Phase 2: Random search around best */
    search_random(grid, dataset, 1000, progress, source_sample_rate);

    free_grid(&coarse_grid);
}

/* ========================================
 * Output Generation
 * ======================================== */

/* Write best config header file */
static int write_best_config_header(const char *filename, const ekg_config_t *config,
                                    const metrics_t *metrics)
{
    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        return -1;
    }

    fprintf(f, "/* Generated by ekg_train */\n");
    fprintf(f, "/* Performance: F1=%.1f%%, Sensitivity=%.1f%%, Specificity=%.1f%% */\n",
            metrics->f1_score * 100, metrics->sensitivity * 100, metrics->specificity * 100);
    fprintf(f, "#ifndef EKG_OPTIMIZED_CONFIG_H\n");
    fprintf(f, "#define EKG_OPTIMIZED_CONFIG_H\n\n");
    fprintf(f, "#define VT_HR_THRESHOLD       %.1ff\n", config->vt_hr_threshold);
    fprintf(f, "#define VT_QRS_WIDTH_MS       %.1ff\n", config->vt_qrs_width_ms);
    fprintf(f, "#define VT_REGULARITY_CV      %.1ff\n", config->vt_regularity_cv);
    fprintf(f, "#define VT_CONFIDENCE_THRESHOLD %.2ff\n", config->vt_confidence_threshold);
    fprintf(f, "#define VF_FREQ_MIN           %.1ff\n", config->vf_freq_min);
    fprintf(f, "#define VF_FREQ_MAX           %.1ff\n", config->vf_freq_max);
    fprintf(f, "#define VF_ZCR_THRESHOLD      %.2ff\n", config->vf_zcr_threshold);
    fprintf(f, "#define VF_CONFIDENCE_THRESHOLD %.2ff\n", config->vf_confidence_threshold);
    fprintf(f, "\n#endif /* EKG_OPTIMIZED_CONFIG_H */\n");

    fclose(f);
    return 0;
}

/* ========================================
 * Utility Functions
 * ======================================== */

/* Print help */
static void print_help(const char *prog_name)
{
    printf("EKG Training & Evaluation Tool\n\n");
    printf("USAGE:\n");
    printf("  %s [OPTIONS] <dataset_path>\n\n", prog_name);
    printf("OPTIONS:\n");
    printf("  -s, --strategy <type>      Search strategy: random, exhaustive, adaptive (default: random)\n");
    printf("  -n, --num-combinations <N> Number of combinations for random search (default: 500)\n");
    printf("  -o, --output <file>        Output CSV file (default: results.csv)\n");
    printf("  -c, --config <file>        Best config header file (default: best_config.h)\n");
    printf("  -r, --sample-rate <Hz>     Source sample rate (default: 100)\n");
    printf("                             Supported: 100, 200, 500, 1000, 2000, 5000, 10000 Hz\n");
    printf("  -h, --help                 Show this help\n\n");
    printf("EXAMPLES:\n");
    printf("  %s data/20251129-ECGrecording/PROCESSEDINTEGER/\n", prog_name);
    printf("  %s -s random -n 100 data/20251129-ECGrecording/PROCESSEDINTEGER/\n", prog_name);
    printf("  %s -s adaptive data/20251129-ECGrecording/PROCESSEDINTEGER/\n\n", prog_name);
}

/* Print progress */
static void print_progress(const progress_t *progress)
{
    float percent = 100.0f * progress->completed / progress->total_combinations;

    printf("Progress: %lu/%lu (%.1f%%) - Best F1=%.3f\n",
           progress->completed, progress->total_combinations, percent,
           progress->best_metrics.f1_score);
}
