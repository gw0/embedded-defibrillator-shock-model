/**
 * EKG Training & Evaluation Tool
 *
 * Performs grid search optimization to find optimal EKG analyzer parameters.
 * Uses real-world dataset with ground truth labels.
 *
 * For educational and prototype purposes only - NOT for medical use.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
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
#include "eval_utils.h"

/* Constants */
#define MAX_SAMPLES 100000

/* ========================================
 * Data Structures
 * ======================================== */

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
    float *vf_min_amp_values;       uint32_t vf_min_amp_count;
    float *vf_dominance_values;     uint32_t vf_dominance_count;
    float *vf_fine_amp_max_values;  uint32_t vf_fine_amp_max_count;
    float *vf_fine_conf_values;     uint32_t vf_fine_conf_count;
    float *vf_noise_mult_values;    uint32_t vf_noise_mult_count;
    float *vf_amp_ratio_values;     uint32_t vf_amp_ratio_count;
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

/* Grid search */
static void init_default_grid(param_grid_t *grid);
static void free_grid(param_grid_t *grid);
static uint64_t grid_size(const param_grid_t *grid);
static void grid_get_config(const param_grid_t *grid, uint64_t index, ekg_config_t *config);

/* Search strategies */
static void search_random(const param_grid_t *grid, const dataset_t *dataset,
                         uint32_t num_samples, progress_t *progress, uint32_t source_sample_rate);
static void search_local(const param_grid_t *grid, const dataset_t *dataset,
                        uint32_t num_samples, progress_t *progress, uint32_t source_sample_rate);
static void search_exhaustive(const param_grid_t *grid, const dataset_t *dataset,
                              progress_t *progress, uint32_t source_sample_rate);
static void search_adaptive(const param_grid_t *grid, const dataset_t *dataset,
                           uint32_t num_combinations, progress_t *progress, uint32_t source_sample_rate);

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
    uint32_t num_combinations = 2000;
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
                "vf_min_amp,vf_dominance,vf_fine_amp_max,vf_fine_conf,vf_noise_mult,vf_amp_ratio,"
                "accuracy,sensitivity,specificity,precision,f1,tp,tn,fp,fn\n");
    }

    if (strcmp(strategy, "exhaustive") == 0) {
        progress.total_combinations = grid_size(&grid);
        printf("Starting exhaustive search (%lu combinations)...\n", progress.total_combinations);
        search_exhaustive(&grid, &dataset, &progress, source_sample_rate);
    } else if (strcmp(strategy, "adaptive") == 0) {
        printf("Starting adaptive search...\n");
        search_adaptive(&grid, &dataset, num_combinations, &progress, source_sample_rate);
    } else if (strcmp(strategy, "local") == 0) {
        progress.best_config = EKG_DEFAULT_CONFIG;
        progress.total_combinations = num_combinations;
        printf("Starting local search (%u combinations)...\n", num_combinations);
        search_local(&grid, &dataset, num_combinations, &progress, source_sample_rate);
    } else {
        progress.total_combinations = num_combinations;
        printf("Starting random search (%u combinations)...\n", num_combinations);
        search_random(&grid, &dataset, num_combinations, &progress, source_sample_rate);
    }

    if (progress.csv_file != NULL) {
        fclose(progress.csv_file);
        printf("\nResults saved to: %s\n", output_file);
    }

    /* Compare with baseline - only report improvement if search found better */
    ekg_config_t baseline = EKG_DEFAULT_CONFIG;
    eval_result_t baseline_result;
    evaluate_config(&baseline, &dataset, NULL, dataset.count, &baseline_result, source_sample_rate);
    printf("\nBaseline F1=%.4f, Search best F1=%.4f\n",
           baseline_result.overall.f1_score, progress.best_metrics.f1_score);

    if (progress.best_metrics.f1_score <= baseline_result.overall.f1_score) {
        printf("Search did not improve upon baseline. Keeping current config.\n");
        progress.best_config = baseline;
        progress.best_metrics = baseline_result.overall;
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
    printf("VF_MIN_AMPLITUDE: %.1f\n", progress.best_config.vf_min_amplitude);
    printf("VF_DOMINANCE_THRESHOLD: %.2f\n", progress.best_config.vf_dominance_threshold);
    printf("VF_FINE_AMPLITUDE_MAX: %.1f\n", progress.best_config.vf_fine_amplitude_max);
    printf("VF_FINE_CONFIDENCE: %.2f\n", progress.best_config.vf_fine_confidence);
    printf("VF_NOISE_FLOOR_MULT: %.1f\n", progress.best_config.vf_noise_floor_mult);
    printf("VF_AMPLITUDE_THRESH_RATIO: %.2f\n", progress.best_config.vf_amplitude_thresh_ratio);
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

    /* VF_MIN_AMPLITUDE: 50-200 step 50 (4 values) */
    grid->vf_min_amp_count = 4;
    grid->vf_min_amp_values = malloc(grid->vf_min_amp_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_min_amp_count; i++) {
        grid->vf_min_amp_values[i] = 50.0f + i * 50.0f;
    }

    /* VF_DOMINANCE_THRESHOLD: 0.30-0.50 step 0.05 (5 values) */
    grid->vf_dominance_count = 5;
    grid->vf_dominance_values = malloc(grid->vf_dominance_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_dominance_count; i++) {
        grid->vf_dominance_values[i] = 0.30f + i * 0.05f;
    }

    /* VF_FINE_AMPLITUDE_MAX: 150-300 step 50 (4 values) */
    grid->vf_fine_amp_max_count = 4;
    grid->vf_fine_amp_max_values = malloc(grid->vf_fine_amp_max_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_fine_amp_max_count; i++) {
        grid->vf_fine_amp_max_values[i] = 150.0f + i * 50.0f;
    }

    /* VF_FINE_CONFIDENCE: 0.60-0.80 step 0.05 (5 values) */
    grid->vf_fine_conf_count = 5;
    grid->vf_fine_conf_values = malloc(grid->vf_fine_conf_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_fine_conf_count; i++) {
        grid->vf_fine_conf_values[i] = 0.60f + i * 0.05f;
    }

    /* VF_NOISE_FLOOR_MULT: 2.0-4.0 step 0.5 (5 values) */
    grid->vf_noise_mult_count = 5;
    grid->vf_noise_mult_values = malloc(grid->vf_noise_mult_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_noise_mult_count; i++) {
        grid->vf_noise_mult_values[i] = 2.0f + i * 0.5f;
    }

    /* VF_AMPLITUDE_THRESH_RATIO: 0.05-0.20 step 0.05 (4 values) */
    grid->vf_amp_ratio_count = 4;
    grid->vf_amp_ratio_values = malloc(grid->vf_amp_ratio_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_amp_ratio_count; i++) {
        grid->vf_amp_ratio_values[i] = 0.05f + i * 0.05f;
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
    free(grid->vf_min_amp_values);
    free(grid->vf_dominance_values);
    free(grid->vf_fine_amp_max_values);
    free(grid->vf_fine_conf_values);
    free(grid->vf_noise_mult_values);
    free(grid->vf_amp_ratio_values);
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
           grid->vf_conf_count *
           grid->vf_min_amp_count *
           grid->vf_dominance_count *
           grid->vf_fine_amp_max_count *
           grid->vf_fine_conf_count *
           grid->vf_noise_mult_count *
           grid->vf_amp_ratio_count;
}

/* Get configuration at grid index */
static void grid_get_config(const param_grid_t *grid, uint64_t index, ekg_config_t *config)
{
    /* Decode multi-dimensional index (reverse order of multiplication in grid_size) */
    uint64_t idx = index;

    uint32_t i13 = idx % grid->vf_amp_ratio_count;
    idx /= grid->vf_amp_ratio_count;

    uint32_t i12 = idx % grid->vf_noise_mult_count;
    idx /= grid->vf_noise_mult_count;

    uint32_t i11 = idx % grid->vf_fine_conf_count;
    idx /= grid->vf_fine_conf_count;

    uint32_t i10 = idx % grid->vf_fine_amp_max_count;
    idx /= grid->vf_fine_amp_max_count;

    uint32_t i9 = idx % grid->vf_dominance_count;
    idx /= grid->vf_dominance_count;

    uint32_t i8 = idx % grid->vf_min_amp_count;
    idx /= grid->vf_min_amp_count;

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

    /* Build configuration - all 14 parameters */
    config->vt_hr_threshold = grid->vt_hr_values[i0];
    config->vt_qrs_width_ms = grid->vt_qrs_values[i1];
    config->vt_regularity_cv = grid->vt_reg_values[i2];
    config->vt_confidence_threshold = grid->vt_conf_values[i3];
    config->vf_freq_min = grid->vf_fmin_values[i4];
    config->vf_freq_max = grid->vf_fmax_values[i5];
    config->vf_zcr_threshold = grid->vf_zcr_values[i6];
    config->vf_confidence_threshold = grid->vf_conf_values[i7];
    config->vf_min_amplitude = grid->vf_min_amp_values[i8];
    config->vf_dominance_threshold = grid->vf_dominance_values[i9];
    config->vf_fine_amplitude_max = grid->vf_fine_amp_max_values[i10];
    config->vf_fine_confidence = grid->vf_fine_conf_values[i11];
    config->vf_noise_floor_mult = grid->vf_noise_mult_values[i12];
    config->vf_amplitude_thresh_ratio = grid->vf_amp_ratio_values[i13];
}

/* ========================================
 * Search Strategies
 * ======================================== */

/* Random search */
static void search_random(const param_grid_t *grid, const dataset_t *dataset,
                         uint32_t num_samples, progress_t *progress, uint32_t source_sample_rate)
{
    srand(42);  /* Fixed seed for reproducibility */

    uint64_t total_size = grid_size(grid);

    for (uint32_t i = 0; i < num_samples; i++) {
        /* Random configuration */
        uint64_t random_idx = ((uint64_t)rand() * rand()) % total_size;

        ekg_config_t config;
        grid_get_config(grid, random_idx, &config);

        /* Evaluate */
        eval_result_t result;
        evaluate_config(&config, dataset, NULL, dataset->count, &result, source_sample_rate);
        metrics_t *metrics = &result.overall;

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.1f,%.2f,%.1f,%.2f,%.1f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
                    config.vf_min_amplitude, config.vf_dominance_threshold,
                    config.vf_fine_amplitude_max, config.vf_fine_confidence,
                    config.vf_noise_floor_mult, config.vf_amplitude_thresh_ratio,
                    metrics->accuracy, metrics->sensitivity, metrics->specificity,
                    metrics->precision, metrics->f1_score,
                    metrics->tp, metrics->tn, metrics->fp, metrics->fn);
        }

        /* Update best */
        if (metrics->f1_score > progress->best_metrics.f1_score) {
            progress->best_metrics = *metrics;
            progress->best_config = config;
        }

        progress->completed++;

        /* Print progress every 10 combinations */
        if (progress->completed % 10 == 0 || i == num_samples - 1) {
            print_progress(progress);
        }
    }
}

/* Local search around best config */
static void search_local(const param_grid_t *grid, const dataset_t *dataset,
                        uint32_t num_samples, progress_t *progress, uint32_t source_sample_rate)
{
    srand(42);  /* Fixed seed for reproducibility */

    for (uint32_t i = 0; i < num_samples; i++) {
        ekg_config_t config = progress->best_config;

        /* Randomly perturb each parameter with 20% probability */
        if (rand() % 10 < 2) config.vt_hr_threshold = grid->vt_hr_values[rand() % grid->vt_hr_count];
        if (rand() % 10 < 2) config.vt_qrs_width_ms = grid->vt_qrs_values[rand() % grid->vt_qrs_count];
        if (rand() % 10 < 2) config.vt_regularity_cv = grid->vt_reg_values[rand() % grid->vt_reg_count];
        if (rand() % 10 < 2) config.vt_confidence_threshold = grid->vt_conf_values[rand() % grid->vt_conf_count];
        if (rand() % 10 < 2) config.vf_freq_min = grid->vf_fmin_values[rand() % grid->vf_fmin_count];
        if (rand() % 10 < 2) config.vf_freq_max = grid->vf_fmax_values[rand() % grid->vf_fmax_count];
        if (rand() % 10 < 2) config.vf_zcr_threshold = grid->vf_zcr_values[rand() % grid->vf_zcr_count];
        if (rand() % 10 < 2) config.vf_confidence_threshold = grid->vf_conf_values[rand() % grid->vf_conf_count];
        if (rand() % 10 < 2) config.vf_min_amplitude = grid->vf_min_amp_values[rand() % grid->vf_min_amp_count];
        if (rand() % 10 < 2) config.vf_dominance_threshold = grid->vf_dominance_values[rand() % grid->vf_dominance_count];
        if (rand() % 10 < 2) config.vf_fine_amplitude_max = grid->vf_fine_amp_max_values[rand() % grid->vf_fine_amp_max_count];
        if (rand() % 10 < 2) config.vf_fine_confidence = grid->vf_fine_conf_values[rand() % grid->vf_fine_conf_count];
        if (rand() % 10 < 2) config.vf_noise_floor_mult = grid->vf_noise_mult_values[rand() % grid->vf_noise_mult_count];
        if (rand() % 10 < 2) config.vf_amplitude_thresh_ratio = grid->vf_amp_ratio_values[rand() % grid->vf_amp_ratio_count];

        /* Evaluate */
        eval_result_t result;
        evaluate_config(&config, dataset, NULL, dataset->count, &result, source_sample_rate);
        metrics_t *metrics = &result.overall;

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.1f,%.2f,%.1f,%.2f,%.1f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
                    config.vf_min_amplitude, config.vf_dominance_threshold,
                    config.vf_fine_amplitude_max, config.vf_fine_confidence,
                    config.vf_noise_floor_mult, config.vf_amplitude_thresh_ratio,
                    metrics->accuracy, metrics->sensitivity, metrics->specificity,
                    metrics->precision, metrics->f1_score,
                    metrics->tp, metrics->tn, metrics->fp, metrics->fn);
        }

        /* Update best if improved */
        if (metrics->f1_score > progress->best_metrics.f1_score) {
            progress->best_metrics = *metrics;
            progress->best_config = config;
        }

        progress->completed++;

        /* Print progress every 10 combinations */
        if (progress->completed % 10 == 0 || i == num_samples - 1) {
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
        eval_result_t result;
        evaluate_config(&config, dataset, NULL, dataset->count, &result, source_sample_rate);
        metrics_t *metrics = &result.overall;

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.1f,%.2f,%.1f,%.2f,%.1f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
                    config.vf_min_amplitude, config.vf_dominance_threshold,
                    config.vf_fine_amplitude_max, config.vf_fine_confidence,
                    config.vf_noise_floor_mult, config.vf_amplitude_thresh_ratio,
                    metrics->accuracy, metrics->sensitivity, metrics->specificity,
                    metrics->precision, metrics->f1_score,
                    metrics->tp, metrics->tn, metrics->fp, metrics->fn);
        }

        /* Update best */
        if (metrics->f1_score > progress->best_metrics.f1_score) {
            progress->best_metrics = *metrics;
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
                           uint32_t num_combinations, progress_t *progress, uint32_t source_sample_rate)
{
    uint32_t phase1_samples = num_combinations;
    uint32_t phase2_samples = num_combinations * 4;
    progress->total_combinations = phase1_samples + phase2_samples;

    /* Phase 1: Random global search */
    printf("Phase 1: Random search (%u samples)\n", phase1_samples);
    search_random(grid, dataset, phase1_samples, progress, source_sample_rate);

    /* Phase 2: Local search around best */
    printf("\nPhase 2: Local search around best (%u samples)\n", phase2_samples);
    search_local(grid, dataset, phase2_samples, progress, source_sample_rate);
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
    fprintf(f, "#define VF_MIN_AMPLITUDE      %.1ff\n", config->vf_min_amplitude);
    fprintf(f, "#define VF_DOMINANCE_THRESHOLD %.2ff\n", config->vf_dominance_threshold);
    fprintf(f, "#define VF_FINE_AMPLITUDE_MAX %.1ff\n", config->vf_fine_amplitude_max);
    fprintf(f, "#define VF_FINE_CONFIDENCE    %.2ff\n", config->vf_fine_confidence);
    fprintf(f, "#define VF_NOISE_FLOOR_MULT   %.1ff\n", config->vf_noise_floor_mult);
    fprintf(f, "#define VF_AMPLITUDE_THRESHOLD_RATIO %.2ff\n", config->vf_amplitude_thresh_ratio);
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
    printf("  -s, --strategy <type>      Search strategy: random, local, exhaustive, adaptive (default: random)\n");
    printf("  -n, --num-combinations <N> Number of combinations (default: 2000)\n");
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
    fflush(stdout);
}
