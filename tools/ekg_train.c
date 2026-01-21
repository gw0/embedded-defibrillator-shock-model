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
        eval_result_t result;
        evaluate_config(&config, dataset, NULL, dataset->count, &result, source_sample_rate);
        metrics_t *metrics = &result.overall;

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
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
        eval_result_t result;
        evaluate_config(&config, dataset, NULL, dataset->count, &result, source_sample_rate);
        metrics_t *metrics = &result.overall;

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            fprintf(progress->csv_file, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,"
                    "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
                    config.vt_hr_threshold, config.vt_qrs_width_ms, config.vt_regularity_cv,
                    config.vt_confidence_threshold, config.vf_freq_min, config.vf_freq_max,
                    config.vf_zcr_threshold, config.vf_confidence_threshold,
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
