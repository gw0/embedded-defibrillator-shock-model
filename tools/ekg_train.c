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
    /* Original parameters */
    float *vt_hr_values;      uint32_t vt_hr_count;
    float *vt_qrs_values;     uint32_t vt_qrs_count;
    float *vt_reg_values;     uint32_t vt_reg_count;
    float *vt_conf_values;    uint32_t vt_conf_count;
    float *vf_fmin_values;    uint32_t vf_fmin_count;
    float *vf_fmax_values;    uint32_t vf_fmax_count;
    float *vf_min_amp_values;       uint32_t vf_min_amp_count;
    float *vf_dominance_values;     uint32_t vf_dominance_count;
    float *vf_fine_amp_max_values;  uint32_t vf_fine_amp_max_count;
    float *vf_fine_conf_values;     uint32_t vf_fine_conf_count;
    float *vf_noise_mult_values;    uint32_t vf_noise_mult_count;
    float *vf_amp_ratio_values;     uint32_t vf_amp_ratio_count;
    float *vf_evidence_values;      uint32_t vf_evidence_count;

    /* New VF Evidence Amplitude Thresholds */
    float *vf_amp_high_thresh_values;    uint32_t vf_amp_high_thresh_count;
    float *vf_amp_medium_thresh_values;  uint32_t vf_amp_medium_thresh_count;
    float *vf_amp_low_thresh_values;     uint32_t vf_amp_low_thresh_count;
    float *vf_amp_vlow_thresh_values;    uint32_t vf_amp_vlow_thresh_count;

    /* New Irregularity Thresholds */
    float *vf_zcr_high_thresh_values;    uint32_t vf_zcr_high_thresh_count;
    float *vf_zcr_mod_thresh_values;     uint32_t vf_zcr_mod_thresh_count;
    float *vf_tcr_high_thresh_values;    uint32_t vf_tcr_high_thresh_count;
    float *vf_tcr_mod_thresh_values;     uint32_t vf_tcr_mod_thresh_count;

    /* New Entropy Thresholds */
    float *vf_entropy_high_thresh_values;  uint32_t vf_entropy_high_thresh_count;
    float *vf_entropy_mod_thresh_values;   uint32_t vf_entropy_mod_thresh_count;

    /* New Organized Rhythm Detection */
    float *vf_org_hr_min_values;    uint32_t vf_org_hr_min_count;
    float *vf_org_hr_max_values;    uint32_t vf_org_hr_max_count;
    float *vf_org_cv_thresh_values; uint32_t vf_org_cv_thresh_count;

    /* New QRS Rate Penalty */
    float *vf_qrs_rate_penalty_thresh_values;  uint32_t vf_qrs_rate_penalty_thresh_count;

    /* New VT Bayesian Boundaries */
    float *vt_hr_high_thresh_values;     uint32_t vt_hr_high_thresh_count;
    float *vt_qrs_vwide_thresh_values;   uint32_t vt_qrs_vwide_thresh_count;
    float *vt_vregular_cv_thresh_values; uint32_t vt_vregular_cv_thresh_count;

    /* Spectral Flatness Thresholds */
    float *vf_flatness_high_thresh_values;  uint32_t vf_flatness_high_thresh_count;
    float *vf_flatness_mod_thresh_values;   uint32_t vf_flatness_mod_thresh_count;
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
static void search_adaptive(const param_grid_t *grid, const dataset_t *dataset,
                           uint32_t num_combinations, progress_t *progress, uint32_t source_sample_rate);

/* Output generation */
static void write_result_csv(FILE *f, const ekg_config_t *config, const metrics_t *metrics);
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
    const char *strategy = "adaptive";
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

    if (strcmp(strategy, "adaptive") == 0) {
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
    float f2_delta = progress.best_metrics.f2_score - baseline_result.overall.f2_score;
    printf("\nBaseline: F2=%.4f, F1=%.4f, Sens=%.4f, Spec=%.4f\n",
           baseline_result.overall.f2_score, baseline_result.overall.f1_score,
           baseline_result.overall.sensitivity, baseline_result.overall.specificity);
    printf("Search:   F2=%.4f, F1=%.4f, Sens=%.4f, Spec=%.4f  (F2 %+.4f)\n",
           progress.best_metrics.f2_score, progress.best_metrics.f1_score,
           progress.best_metrics.sensitivity, progress.best_metrics.specificity, f2_delta);

    if (progress.best_metrics.f2_score <= baseline_result.overall.f2_score) {
        printf("Search did not improve upon baseline. Keeping current config.\n");
        progress.best_config = baseline;
        progress.best_metrics = baseline_result.overall;
    }

    printf("\n========================================\n");
    printf("BEST CONFIGURATION FOUND\n");
    printf("========================================\n");
    print_config_params(&progress.best_config);
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
    /*
     * STABLE PARAMETERS (1 value - no tuning needed based on analysis)
     * These parameters consistently stayed at default across optimization runs.
     */

    /* VT_HR_THRESHOLD: stable at 120.0 (clinical standard) */
    grid->vt_hr_count = 1;
    grid->vt_hr_values = malloc(grid->vt_hr_count * sizeof(float));
    grid->vt_hr_values[0] = 120.0f;

    /* VT_QRS_WIDTH_MS: stable at 100.0 (clinical standard) */
    grid->vt_qrs_count = 1;
    grid->vt_qrs_values = malloc(grid->vt_qrs_count * sizeof(float));
    grid->vt_qrs_values[0] = 100.0f;

    /* VT_CONFIDENCE_THRESHOLD: stable at 0.70 */
    grid->vt_conf_count = 1;
    grid->vt_conf_values = malloc(grid->vt_conf_count * sizeof(float));
    grid->vt_conf_values[0] = 0.70f;

    /* VF_FREQ_MAX: stable at 9.5 */
    grid->vf_fmax_count = 1;
    grid->vf_fmax_values = malloc(grid->vf_fmax_count * sizeof(float));
    grid->vf_fmax_values[0] = 9.5f;

    /* VF_MIN_AMPLITUDE: stable at 200.0 */
    grid->vf_min_amp_count = 1;
    grid->vf_min_amp_values = malloc(grid->vf_min_amp_count * sizeof(float));
    grid->vf_min_amp_values[0] = 200.0f;

    /* VF_FINE_CONFIDENCE: stable at 0.60 */
    grid->vf_fine_conf_count = 1;
    grid->vf_fine_conf_values = malloc(grid->vf_fine_conf_count * sizeof(float));
    grid->vf_fine_conf_values[0] = 0.60f;

    /* VF_AMPLITUDE_THRESH_RATIO: stable at 0.05 */
    grid->vf_amp_ratio_count = 1;
    grid->vf_amp_ratio_values = malloc(grid->vf_amp_ratio_count * sizeof(float));
    grid->vf_amp_ratio_values[0] = 0.05f;

    /* VF_EVIDENCE_THRESHOLD: stable at 3.0 */
    grid->vf_evidence_count = 1;
    grid->vf_evidence_values = malloc(grid->vf_evidence_count * sizeof(float));
    grid->vf_evidence_values[0] = 3.0f;

    /*
     * ACTIVE PARAMETERS (multiple values - these influence performance)
     */

    /* VT_REGULARITY_CV: 15-30 step 5 (4 values) - changed in optimization */
    grid->vt_reg_count = 4;
    grid->vt_reg_values = malloc(grid->vt_reg_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_reg_count; i++) {
        grid->vt_reg_values[i] = 15.0f + i * 5.0f;
    }

    /* VF_FREQ_MIN: 2.5-5.0 step 0.5 (6 values) - changed in optimization */
    grid->vf_fmin_count = 6;
    grid->vf_fmin_values = malloc(grid->vf_fmin_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_fmin_count; i++) {
        grid->vf_fmin_values[i] = 2.5f + i * 0.5f;
    }

    /* VF_DOMINANCE_THRESHOLD: 0.30-0.50 step 0.05 (5 values) - changed in optimization */
    grid->vf_dominance_count = 5;
    grid->vf_dominance_values = malloc(grid->vf_dominance_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_dominance_count; i++) {
        grid->vf_dominance_values[i] = 0.30f + i * 0.05f;
    }

    /* VF_FINE_AMPLITUDE_MAX: 150-300 step 50 (4 values) - changed in optimization */
    grid->vf_fine_amp_max_count = 4;
    grid->vf_fine_amp_max_values = malloc(grid->vf_fine_amp_max_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_fine_amp_max_count; i++) {
        grid->vf_fine_amp_max_values[i] = 150.0f + i * 50.0f;
    }

    /* VF_NOISE_FLOOR_MULT: 2.0-4.0 step 0.5 (5 values) - changed in optimization */
    grid->vf_noise_mult_count = 5;
    grid->vf_noise_mult_values = malloc(grid->vf_noise_mult_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_noise_mult_count; i++) {
        grid->vf_noise_mult_values[i] = 2.0f + i * 0.5f;
    }

    /*
     * NEW PARAMETERS - High Priority
     */

    /* VF_AMP_HIGH_THRESH: 250-400 step 50 (4 values) */
    grid->vf_amp_high_thresh_count = 4;
    grid->vf_amp_high_thresh_values = malloc(grid->vf_amp_high_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_amp_high_thresh_count; i++) {
        grid->vf_amp_high_thresh_values[i] = 250.0f + i * 50.0f;
    }

    /* VF_AMP_MEDIUM_THRESH: 150-250 step 25 (5 values) */
    grid->vf_amp_medium_thresh_count = 5;
    grid->vf_amp_medium_thresh_values = malloc(grid->vf_amp_medium_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_amp_medium_thresh_count; i++) {
        grid->vf_amp_medium_thresh_values[i] = 150.0f + i * 25.0f;
    }

    /* VF_AMP_LOW_THRESH: 75-150 step 25 (4 values) */
    grid->vf_amp_low_thresh_count = 4;
    grid->vf_amp_low_thresh_values = malloc(grid->vf_amp_low_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_amp_low_thresh_count; i++) {
        grid->vf_amp_low_thresh_values[i] = 75.0f + i * 25.0f;
    }

    /* VF_AMP_VLOW_THRESH: 25-75 step 25 (3 values) */
    grid->vf_amp_vlow_thresh_count = 3;
    grid->vf_amp_vlow_thresh_values = malloc(grid->vf_amp_vlow_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_amp_vlow_thresh_count; i++) {
        grid->vf_amp_vlow_thresh_values[i] = 25.0f + i * 25.0f;
    }

    /* VF_ZCR_HIGH_THRESH: 0.15-0.35 step 0.05 (5 values) */
    grid->vf_zcr_high_thresh_count = 5;
    grid->vf_zcr_high_thresh_values = malloc(grid->vf_zcr_high_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_zcr_high_thresh_count; i++) {
        grid->vf_zcr_high_thresh_values[i] = 0.15f + i * 0.05f;
    }

    /* VF_ZCR_MOD_THRESH: 0.10-0.25 step 0.05 (4 values) */
    grid->vf_zcr_mod_thresh_count = 4;
    grid->vf_zcr_mod_thresh_values = malloc(grid->vf_zcr_mod_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_zcr_mod_thresh_count; i++) {
        grid->vf_zcr_mod_thresh_values[i] = 0.10f + i * 0.05f;
    }

    /* VF_TCR_HIGH_THRESH: 0.3-0.7 step 0.1 (5 values) */
    grid->vf_tcr_high_thresh_count = 5;
    grid->vf_tcr_high_thresh_values = malloc(grid->vf_tcr_high_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_tcr_high_thresh_count; i++) {
        grid->vf_tcr_high_thresh_values[i] = 0.3f + i * 0.1f;
    }

    /* VF_TCR_MOD_THRESH: 0.2-0.5 step 0.1 (4 values) */
    grid->vf_tcr_mod_thresh_count = 4;
    grid->vf_tcr_mod_thresh_values = malloc(grid->vf_tcr_mod_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_tcr_mod_thresh_count; i++) {
        grid->vf_tcr_mod_thresh_values[i] = 0.2f + i * 0.1f;
    }

    /* VF_ENTROPY_HIGH_THRESH: 0.5-0.7 step 0.05 (5 values) */
    grid->vf_entropy_high_thresh_count = 5;
    grid->vf_entropy_high_thresh_values = malloc(grid->vf_entropy_high_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_entropy_high_thresh_count; i++) {
        grid->vf_entropy_high_thresh_values[i] = 0.5f + i * 0.05f;
    }

    /* VF_ENTROPY_MOD_THRESH: 0.3-0.5 step 0.05 (5 values) */
    grid->vf_entropy_mod_thresh_count = 5;
    grid->vf_entropy_mod_thresh_values = malloc(grid->vf_entropy_mod_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_entropy_mod_thresh_count; i++) {
        grid->vf_entropy_mod_thresh_values[i] = 0.3f + i * 0.05f;
    }

    /* VF_ORG_HR_MIN: 100-140 step 10 (5 values) */
    grid->vf_org_hr_min_count = 5;
    grid->vf_org_hr_min_values = malloc(grid->vf_org_hr_min_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_org_hr_min_count; i++) {
        grid->vf_org_hr_min_values[i] = 100.0f + i * 10.0f;
    }

    /* VF_ORG_HR_MAX: 180-250 step 10 (8 values) */
    grid->vf_org_hr_max_count = 8;
    grid->vf_org_hr_max_values = malloc(grid->vf_org_hr_max_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_org_hr_max_count; i++) {
        grid->vf_org_hr_max_values[i] = 180.0f + i * 10.0f;
    }

    /* VF_ORG_CV_THRESH: 10-20 step 2.5 (5 values) */
    grid->vf_org_cv_thresh_count = 5;
    grid->vf_org_cv_thresh_values = malloc(grid->vf_org_cv_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_org_cv_thresh_count; i++) {
        grid->vf_org_cv_thresh_values[i] = 10.0f + i * 2.5f;
    }

    /* VF_QRS_RATE_PENALTY_THRESH: 0.5-1.0 step 0.1 (6 values) */
    grid->vf_qrs_rate_penalty_thresh_count = 6;
    grid->vf_qrs_rate_penalty_thresh_values = malloc(grid->vf_qrs_rate_penalty_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_qrs_rate_penalty_thresh_count; i++) {
        grid->vf_qrs_rate_penalty_thresh_values[i] = 0.5f + i * 0.1f;
    }

    /*
     * NEW VT PARAMETERS - Medium Priority
     */

    /* VT_HR_HIGH_THRESH: 140-180 step 10 (5 values) */
    grid->vt_hr_high_thresh_count = 5;
    grid->vt_hr_high_thresh_values = malloc(grid->vt_hr_high_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_hr_high_thresh_count; i++) {
        grid->vt_hr_high_thresh_values[i] = 140.0f + i * 10.0f;
    }

    /* VT_QRS_VWIDE_THRESH: 120-160 step 10 (5 values) */
    grid->vt_qrs_vwide_thresh_count = 5;
    grid->vt_qrs_vwide_thresh_values = malloc(grid->vt_qrs_vwide_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_qrs_vwide_thresh_count; i++) {
        grid->vt_qrs_vwide_thresh_values[i] = 120.0f + i * 10.0f;
    }

    /* VT_VREGULAR_CV_THRESH: 5-15 step 2.5 (5 values) */
    grid->vt_vregular_cv_thresh_count = 5;
    grid->vt_vregular_cv_thresh_values = malloc(grid->vt_vregular_cv_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vt_vregular_cv_thresh_count; i++) {
        grid->vt_vregular_cv_thresh_values[i] = 5.0f + i * 2.5f;
    }

    /*
     * SPECTRAL FLATNESS PARAMETERS - High Impact for VF sensitivity
     */

    /* VF_FLATNESS_HIGH_THRESH: 0.6-0.8 step 0.05 (5 values) */
    grid->vf_flatness_high_thresh_count = 5;
    grid->vf_flatness_high_thresh_values = malloc(grid->vf_flatness_high_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_flatness_high_thresh_count; i++) {
        grid->vf_flatness_high_thresh_values[i] = 0.6f + i * 0.05f;
    }

    /* VF_FLATNESS_MOD_THRESH: 0.4-0.6 step 0.05 (5 values) */
    grid->vf_flatness_mod_thresh_count = 5;
    grid->vf_flatness_mod_thresh_values = malloc(grid->vf_flatness_mod_thresh_count * sizeof(float));
    for (uint32_t i = 0; i < grid->vf_flatness_mod_thresh_count; i++) {
        grid->vf_flatness_mod_thresh_values[i] = 0.4f + i * 0.05f;
    }
}

/* Free grid memory */
static void free_grid(param_grid_t *grid)
{
    /* Original parameters */
    free(grid->vt_hr_values);
    free(grid->vt_qrs_values);
    free(grid->vt_reg_values);
    free(grid->vt_conf_values);
    free(grid->vf_fmin_values);
    free(grid->vf_fmax_values);
    free(grid->vf_min_amp_values);
    free(grid->vf_dominance_values);
    free(grid->vf_fine_amp_max_values);
    free(grid->vf_fine_conf_values);
    free(grid->vf_noise_mult_values);
    free(grid->vf_amp_ratio_values);
    free(grid->vf_evidence_values);

    /* New VF parameters */
    free(grid->vf_amp_high_thresh_values);
    free(grid->vf_amp_medium_thresh_values);
    free(grid->vf_amp_low_thresh_values);
    free(grid->vf_amp_vlow_thresh_values);
    free(grid->vf_zcr_high_thresh_values);
    free(grid->vf_zcr_mod_thresh_values);
    free(grid->vf_tcr_high_thresh_values);
    free(grid->vf_tcr_mod_thresh_values);
    free(grid->vf_entropy_high_thresh_values);
    free(grid->vf_entropy_mod_thresh_values);
    free(grid->vf_org_hr_min_values);
    free(grid->vf_org_hr_max_values);
    free(grid->vf_org_cv_thresh_values);
    free(grid->vf_qrs_rate_penalty_thresh_values);

    /* New VT parameters */
    free(grid->vt_hr_high_thresh_values);
    free(grid->vt_qrs_vwide_thresh_values);
    free(grid->vt_vregular_cv_thresh_values);

    /* Spectral flatness parameters */
    free(grid->vf_flatness_high_thresh_values);
    free(grid->vf_flatness_mod_thresh_values);
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
           grid->vf_min_amp_count *
           grid->vf_dominance_count *
           grid->vf_fine_amp_max_count *
           grid->vf_fine_conf_count *
           grid->vf_noise_mult_count *
           grid->vf_amp_ratio_count *
           grid->vf_evidence_count *
           /* New VF parameters */
           grid->vf_amp_high_thresh_count *
           grid->vf_amp_medium_thresh_count *
           grid->vf_amp_low_thresh_count *
           grid->vf_amp_vlow_thresh_count *
           grid->vf_zcr_high_thresh_count *
           grid->vf_zcr_mod_thresh_count *
           grid->vf_tcr_high_thresh_count *
           grid->vf_tcr_mod_thresh_count *
           grid->vf_entropy_high_thresh_count *
           grid->vf_entropy_mod_thresh_count *
           grid->vf_org_hr_min_count *
           grid->vf_org_hr_max_count *
           grid->vf_org_cv_thresh_count *
           grid->vf_qrs_rate_penalty_thresh_count *
           /* New VT parameters */
           grid->vt_hr_high_thresh_count *
           grid->vt_qrs_vwide_thresh_count *
           grid->vt_vregular_cv_thresh_count *
           /* Spectral flatness parameters */
           grid->vf_flatness_high_thresh_count *
           grid->vf_flatness_mod_thresh_count;
}

/* Get configuration at grid index */
static void grid_get_config(const param_grid_t *grid, uint64_t index, ekg_config_t *config)
{
    /* Decode multi-dimensional index (reverse order of multiplication in grid_size) */
    uint64_t idx = index;

    /* Spectral flatness parameters (decode last to first from grid_size) */
    uint32_t i_vf_flatness_mod = idx % grid->vf_flatness_mod_thresh_count;
    idx /= grid->vf_flatness_mod_thresh_count;

    uint32_t i_vf_flatness_high = idx % grid->vf_flatness_high_thresh_count;
    idx /= grid->vf_flatness_high_thresh_count;

    /* New VT parameters (decode last to first from grid_size) */
    uint32_t i_vt_vregular_cv = idx % grid->vt_vregular_cv_thresh_count;
    idx /= grid->vt_vregular_cv_thresh_count;

    uint32_t i_vt_qrs_vwide = idx % grid->vt_qrs_vwide_thresh_count;
    idx /= grid->vt_qrs_vwide_thresh_count;

    uint32_t i_vt_hr_high = idx % grid->vt_hr_high_thresh_count;
    idx /= grid->vt_hr_high_thresh_count;

    /* New VF parameters */
    uint32_t i_vf_qrs_rate_penalty = idx % grid->vf_qrs_rate_penalty_thresh_count;
    idx /= grid->vf_qrs_rate_penalty_thresh_count;

    uint32_t i_vf_org_cv = idx % grid->vf_org_cv_thresh_count;
    idx /= grid->vf_org_cv_thresh_count;

    uint32_t i_vf_org_hr_max = idx % grid->vf_org_hr_max_count;
    idx /= grid->vf_org_hr_max_count;

    uint32_t i_vf_org_hr_min = idx % grid->vf_org_hr_min_count;
    idx /= grid->vf_org_hr_min_count;

    uint32_t i_vf_entropy_mod = idx % grid->vf_entropy_mod_thresh_count;
    idx /= grid->vf_entropy_mod_thresh_count;

    uint32_t i_vf_entropy_high = idx % grid->vf_entropy_high_thresh_count;
    idx /= grid->vf_entropy_high_thresh_count;

    uint32_t i_vf_tcr_mod = idx % grid->vf_tcr_mod_thresh_count;
    idx /= grid->vf_tcr_mod_thresh_count;

    uint32_t i_vf_tcr_high = idx % grid->vf_tcr_high_thresh_count;
    idx /= grid->vf_tcr_high_thresh_count;

    uint32_t i_vf_zcr_mod = idx % grid->vf_zcr_mod_thresh_count;
    idx /= grid->vf_zcr_mod_thresh_count;

    uint32_t i_vf_zcr_high = idx % grid->vf_zcr_high_thresh_count;
    idx /= grid->vf_zcr_high_thresh_count;

    uint32_t i_vf_amp_vlow = idx % grid->vf_amp_vlow_thresh_count;
    idx /= grid->vf_amp_vlow_thresh_count;

    uint32_t i_vf_amp_low = idx % grid->vf_amp_low_thresh_count;
    idx /= grid->vf_amp_low_thresh_count;

    uint32_t i_vf_amp_medium = idx % grid->vf_amp_medium_thresh_count;
    idx /= grid->vf_amp_medium_thresh_count;

    uint32_t i_vf_amp_high = idx % grid->vf_amp_high_thresh_count;
    idx /= grid->vf_amp_high_thresh_count;

    /* Original parameters (continue decoding) */
    uint32_t i14 = idx % grid->vf_evidence_count;
    idx /= grid->vf_evidence_count;

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

    /* Build configuration - original 15 parameters */
    config->vt_hr_threshold = grid->vt_hr_values[i0];
    config->vt_qrs_width_ms = grid->vt_qrs_values[i1];
    config->vt_regularity_cv = grid->vt_reg_values[i2];
    config->vt_confidence_threshold = grid->vt_conf_values[i3];
    config->vf_freq_min = grid->vf_fmin_values[i4];
    config->vf_freq_max = grid->vf_fmax_values[i5];
    config->vf_min_amplitude = grid->vf_min_amp_values[i8];
    config->vf_dominance_threshold = grid->vf_dominance_values[i9];
    config->vf_fine_amplitude_max = grid->vf_fine_amp_max_values[i10];
    config->vf_fine_confidence = grid->vf_fine_conf_values[i11];
    config->vf_noise_floor_mult = grid->vf_noise_mult_values[i12];
    config->vf_amplitude_thresh_ratio = grid->vf_amp_ratio_values[i13];
    config->vf_evidence_threshold = grid->vf_evidence_values[i14];

    /* New VF evidence amplitude thresholds */
    config->vf_amp_high_thresh = grid->vf_amp_high_thresh_values[i_vf_amp_high];
    config->vf_amp_medium_thresh = grid->vf_amp_medium_thresh_values[i_vf_amp_medium];
    config->vf_amp_low_thresh = grid->vf_amp_low_thresh_values[i_vf_amp_low];
    config->vf_amp_vlow_thresh = grid->vf_amp_vlow_thresh_values[i_vf_amp_vlow];

    /* New irregularity thresholds */
    config->vf_zcr_high_thresh = grid->vf_zcr_high_thresh_values[i_vf_zcr_high];
    config->vf_zcr_mod_thresh = grid->vf_zcr_mod_thresh_values[i_vf_zcr_mod];
    config->vf_tcr_high_thresh = grid->vf_tcr_high_thresh_values[i_vf_tcr_high];
    config->vf_tcr_mod_thresh = grid->vf_tcr_mod_thresh_values[i_vf_tcr_mod];

    /* New entropy thresholds */
    config->vf_entropy_high_thresh = grid->vf_entropy_high_thresh_values[i_vf_entropy_high];
    config->vf_entropy_mod_thresh = grid->vf_entropy_mod_thresh_values[i_vf_entropy_mod];

    /* New organized rhythm detection */
    config->vf_org_hr_min = grid->vf_org_hr_min_values[i_vf_org_hr_min];
    config->vf_org_hr_max = grid->vf_org_hr_max_values[i_vf_org_hr_max];
    config->vf_org_cv_thresh = grid->vf_org_cv_thresh_values[i_vf_org_cv];

    /* New QRS rate penalty */
    config->vf_qrs_rate_penalty_thresh = grid->vf_qrs_rate_penalty_thresh_values[i_vf_qrs_rate_penalty];

    /* New VT Bayesian boundaries */
    config->vt_hr_high_thresh = grid->vt_hr_high_thresh_values[i_vt_hr_high];
    config->vt_qrs_vwide_thresh = grid->vt_qrs_vwide_thresh_values[i_vt_qrs_vwide];
    config->vt_vregular_cv_thresh = grid->vt_vregular_cv_thresh_values[i_vt_vregular_cv];

    /* Spectral flatness thresholds */
    config->vf_flatness_high_thresh = grid->vf_flatness_high_thresh_values[i_vf_flatness_high];
    config->vf_flatness_mod_thresh = grid->vf_flatness_mod_thresh_values[i_vf_flatness_mod];
}

/* ========================================
 * Search Strategies
 * ======================================== */

/* Write result row to CSV file */
static void write_result_csv(FILE *f, const ekg_config_t *config, const metrics_t *metrics)
{
    fprintf(f, "%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,"
            "%.1f,%.2f,%.1f,%.2f,%.1f,%.2f,%.1f,"
            "%.1f,%.1f,%.1f,%.1f,"
            "%.2f,%.2f,%.2f,%.2f,"
            "%.2f,%.2f,"
            "%.1f,%.1f,%.1f,"
            "%.2f,"
            "%.1f,%.1f,%.1f,"
            "%.2f,%.2f,"
            "%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%u\n",
            /* Original parameters */
            config->vt_hr_threshold, config->vt_qrs_width_ms, config->vt_regularity_cv,
            config->vt_confidence_threshold, config->vf_freq_min, config->vf_freq_max,
            config->vf_min_amplitude, config->vf_dominance_threshold,
            config->vf_fine_amplitude_max, config->vf_fine_confidence,
            config->vf_noise_floor_mult, config->vf_amplitude_thresh_ratio,
            config->vf_evidence_threshold,
            /* VF amplitude thresholds */
            config->vf_amp_high_thresh, config->vf_amp_medium_thresh,
            config->vf_amp_low_thresh, config->vf_amp_vlow_thresh,
            /* Irregularity thresholds */
            config->vf_zcr_high_thresh, config->vf_zcr_mod_thresh,
            config->vf_tcr_high_thresh, config->vf_tcr_mod_thresh,
            /* Entropy thresholds */
            config->vf_entropy_high_thresh, config->vf_entropy_mod_thresh,
            /* Organized rhythm detection */
            config->vf_org_hr_min, config->vf_org_hr_max, config->vf_org_cv_thresh,
            /* QRS rate penalty */
            config->vf_qrs_rate_penalty_thresh,
            /* VT Bayesian boundaries */
            config->vt_hr_high_thresh, config->vt_qrs_vwide_thresh, config->vt_vregular_cv_thresh,
            /* Spectral flatness thresholds */
            config->vf_flatness_high_thresh, config->vf_flatness_mod_thresh,
            /* Metrics */
            metrics->accuracy, metrics->sensitivity, metrics->specificity,
            metrics->precision, metrics->f1_score,
            metrics->tp, metrics->tn, metrics->fp, metrics->fn);
}

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
            write_result_csv(progress->csv_file, &config, metrics);
        }

        /* Update best using F2-score (prioritizes sensitivity) */
        if (metrics->f2_score > progress->best_metrics.f2_score) {
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
        if (rand() % 10 < 2) config.vf_min_amplitude = grid->vf_min_amp_values[rand() % grid->vf_min_amp_count];
        if (rand() % 10 < 2) config.vf_dominance_threshold = grid->vf_dominance_values[rand() % grid->vf_dominance_count];
        if (rand() % 10 < 2) config.vf_fine_amplitude_max = grid->vf_fine_amp_max_values[rand() % grid->vf_fine_amp_max_count];
        if (rand() % 10 < 2) config.vf_fine_confidence = grid->vf_fine_conf_values[rand() % grid->vf_fine_conf_count];
        if (rand() % 10 < 2) config.vf_noise_floor_mult = grid->vf_noise_mult_values[rand() % grid->vf_noise_mult_count];
        if (rand() % 10 < 2) config.vf_amplitude_thresh_ratio = grid->vf_amp_ratio_values[rand() % grid->vf_amp_ratio_count];
        if (rand() % 10 < 2) config.vf_evidence_threshold = grid->vf_evidence_values[rand() % grid->vf_evidence_count];
        if (rand() % 10 < 2) config.vf_amp_high_thresh = grid->vf_amp_high_thresh_values[rand() % grid->vf_amp_high_thresh_count];
        if (rand() % 10 < 2) config.vf_amp_medium_thresh = grid->vf_amp_medium_thresh_values[rand() % grid->vf_amp_medium_thresh_count];
        if (rand() % 10 < 2) config.vf_amp_low_thresh = grid->vf_amp_low_thresh_values[rand() % grid->vf_amp_low_thresh_count];
        if (rand() % 10 < 2) config.vf_amp_vlow_thresh = grid->vf_amp_vlow_thresh_values[rand() % grid->vf_amp_vlow_thresh_count];
        if (rand() % 10 < 2) config.vf_zcr_high_thresh = grid->vf_zcr_high_thresh_values[rand() % grid->vf_zcr_high_thresh_count];
        if (rand() % 10 < 2) config.vf_zcr_mod_thresh = grid->vf_zcr_mod_thresh_values[rand() % grid->vf_zcr_mod_thresh_count];
        if (rand() % 10 < 2) config.vf_tcr_high_thresh = grid->vf_tcr_high_thresh_values[rand() % grid->vf_tcr_high_thresh_count];
        if (rand() % 10 < 2) config.vf_tcr_mod_thresh = grid->vf_tcr_mod_thresh_values[rand() % grid->vf_tcr_mod_thresh_count];
        if (rand() % 10 < 2) config.vf_entropy_high_thresh = grid->vf_entropy_high_thresh_values[rand() % grid->vf_entropy_high_thresh_count];
        if (rand() % 10 < 2) config.vf_entropy_mod_thresh = grid->vf_entropy_mod_thresh_values[rand() % grid->vf_entropy_mod_thresh_count];
        if (rand() % 10 < 2) config.vf_org_hr_min = grid->vf_org_hr_min_values[rand() % grid->vf_org_hr_min_count];
        if (rand() % 10 < 2) config.vf_org_hr_max = grid->vf_org_hr_max_values[rand() % grid->vf_org_hr_max_count];
        if (rand() % 10 < 2) config.vf_org_cv_thresh = grid->vf_org_cv_thresh_values[rand() % grid->vf_org_cv_thresh_count];
        if (rand() % 10 < 2) config.vf_qrs_rate_penalty_thresh = grid->vf_qrs_rate_penalty_thresh_values[rand() % grid->vf_qrs_rate_penalty_thresh_count];
        if (rand() % 10 < 2) config.vt_hr_high_thresh = grid->vt_hr_high_thresh_values[rand() % grid->vt_hr_high_thresh_count];
        if (rand() % 10 < 2) config.vt_qrs_vwide_thresh = grid->vt_qrs_vwide_thresh_values[rand() % grid->vt_qrs_vwide_thresh_count];
        if (rand() % 10 < 2) config.vt_vregular_cv_thresh = grid->vt_vregular_cv_thresh_values[rand() % grid->vt_vregular_cv_thresh_count];
        if (rand() % 10 < 2) config.vf_flatness_high_thresh = grid->vf_flatness_high_thresh_values[rand() % grid->vf_flatness_high_thresh_count];
        if (rand() % 10 < 2) config.vf_flatness_mod_thresh = grid->vf_flatness_mod_thresh_values[rand() % grid->vf_flatness_mod_thresh_count];

        /* Skip if no parameter changed */
        if (memcmp(&config, &progress->best_config, sizeof(ekg_config_t)) == 0) {
            continue;
        }

        /* Evaluate */
        eval_result_t result;
        evaluate_config(&config, dataset, NULL, dataset->count, &result, source_sample_rate);
        metrics_t *metrics = &result.overall;

        /* Write to CSV */
        if (progress->csv_file != NULL) {
            write_result_csv(progress->csv_file, &config, metrics);
        }

        /* Update best if improved using F2-score (prioritizes sensitivity) */
        if (metrics->f2_score > progress->best_metrics.f2_score) {
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
    fprintf(f, "/*\n");
    fprintf(f, " * PERFORMANCE METRICS:\n");
    fprintf(f, " *   Accuracy:    %.1f%%\n", metrics->accuracy * 100.0f);
    fprintf(f, " *   Sensitivity: %.1f%%\n", metrics->sensitivity * 100.0f);
    fprintf(f, " *   Specificity: %.1f%%\n", metrics->specificity * 100.0f);
    fprintf(f, " *   Precision:   %.1f%%\n", metrics->precision * 100.0f);
    fprintf(f, " *   F1-Score:    %.1f%%\n", metrics->f1_score * 100.0f);
    fprintf(f, " *   F2-Score:    %.1f%%\n", metrics->f2_score * 100.0f);
    fprintf(f, " *   Confusion:   TP=%u TN=%u FP=%u FN=%u\n",
            metrics->tp, metrics->tn, metrics->fp, metrics->fn);
    fprintf(f, " */\n");
    fprintf(f, "#ifndef EKG_OPTIMIZED_CONFIG_H\n");
    fprintf(f, "#define EKG_OPTIMIZED_CONFIG_H\n\n");
    fprintf(f, "#define VT_HR_THRESHOLD       %.1ff\n", config->vt_hr_threshold);
    fprintf(f, "#define VT_QRS_WIDTH_MS       %.1ff\n", config->vt_qrs_width_ms);
    fprintf(f, "#define VT_REGULARITY_CV      %.1ff\n", config->vt_regularity_cv);
    fprintf(f, "#define VT_CONFIDENCE_THRESHOLD %.2ff\n", config->vt_confidence_threshold);
    fprintf(f, "#define VF_FREQ_MIN           %.1ff\n", config->vf_freq_min);
    fprintf(f, "#define VF_FREQ_MAX           %.1ff\n", config->vf_freq_max);
    fprintf(f, "#define VF_MIN_AMPLITUDE      %.1ff\n", config->vf_min_amplitude);
    fprintf(f, "#define VF_DOMINANCE_THRESHOLD %.2ff\n", config->vf_dominance_threshold);
    fprintf(f, "#define VF_FINE_AMPLITUDE_MAX %.1ff\n", config->vf_fine_amplitude_max);
    fprintf(f, "#define VF_FINE_CONFIDENCE    %.2ff\n", config->vf_fine_confidence);
    fprintf(f, "#define VF_NOISE_FLOOR_MULT   %.1ff\n", config->vf_noise_floor_mult);
    fprintf(f, "#define VF_AMPLITUDE_THRESHOLD_RATIO %.2ff\n", config->vf_amplitude_thresh_ratio);
    fprintf(f, "#define VF_EVIDENCE_THRESHOLD %.1ff\n", config->vf_evidence_threshold);
    /* Evidence Amplitude Thresholds */
    fprintf(f, "#define VF_AMP_HIGH_THRESH    %.1ff\n", config->vf_amp_high_thresh);
    fprintf(f, "#define VF_AMP_MEDIUM_THRESH  %.1ff\n", config->vf_amp_medium_thresh);
    fprintf(f, "#define VF_AMP_LOW_THRESH     %.1ff\n", config->vf_amp_low_thresh);
    fprintf(f, "#define VF_AMP_VLOW_THRESH    %.1ff\n", config->vf_amp_vlow_thresh);
    /* Irregularity Thresholds */
    fprintf(f, "#define VF_ZCR_HIGH_THRESH    %.2ff\n", config->vf_zcr_high_thresh);
    fprintf(f, "#define VF_ZCR_MOD_THRESH     %.2ff\n", config->vf_zcr_mod_thresh);
    fprintf(f, "#define VF_TCR_HIGH_THRESH    %.2ff\n", config->vf_tcr_high_thresh);
    fprintf(f, "#define VF_TCR_MOD_THRESH     %.2ff\n", config->vf_tcr_mod_thresh);
    /* Entropy Thresholds */
    fprintf(f, "#define VF_ENTROPY_HIGH_THRESH %.2ff\n", config->vf_entropy_high_thresh);
    fprintf(f, "#define VF_ENTROPY_MOD_THRESH %.2ff\n", config->vf_entropy_mod_thresh);
    /* Organized Rhythm Detection */
    fprintf(f, "#define VF_ORG_HR_MIN         %.1ff\n", config->vf_org_hr_min);
    fprintf(f, "#define VF_ORG_HR_MAX         %.1ff\n", config->vf_org_hr_max);
    fprintf(f, "#define VF_ORG_CV_THRESH      %.1ff\n", config->vf_org_cv_thresh);
    /* QRS Rate Penalty */
    fprintf(f, "#define VF_QRS_RATE_PENALTY_THRESH %.2ff\n", config->vf_qrs_rate_penalty_thresh);
    /* VT Bayesian Boundaries */
    fprintf(f, "#define VT_HR_HIGH_THRESH     %.1ff\n", config->vt_hr_high_thresh);
    fprintf(f, "#define VT_QRS_VWIDE_THRESH   %.1ff\n", config->vt_qrs_vwide_thresh);
    fprintf(f, "#define VT_VREGULAR_CV_THRESH %.1ff\n", config->vt_vregular_cv_thresh);
    /* Spectral Flatness Thresholds */
    fprintf(f, "#define VF_FLATNESS_HIGH_THRESH %.2ff\n", config->vf_flatness_high_thresh);
    fprintf(f, "#define VF_FLATNESS_MOD_THRESH  %.2ff\n", config->vf_flatness_mod_thresh);
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
    printf("  -s, --strategy <type>      Search strategy: random, local, adaptive (default: adaptive)\n");
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

    printf("Progress: %lu/%lu (%.1f%%) - Best F2=%.3f (Sens=%.1f%%)\n",
           progress->completed, progress->total_combinations, percent,
           progress->best_metrics.f2_score, progress->best_metrics.sensitivity * 100.0f);
    fflush(stdout);
}
