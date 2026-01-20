/**
 * Evaluation Utilities for EKG Analyzer Training Tools
 *
 * Shared dataset loading, evaluation, and reporting functions used by
 * ekg_train and ekg_eval tools.
 *
 * For educational and prototype purposes only - NOT for medical use.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#ifndef EVAL_UTILS_H
#define EVAL_UTILS_H

#include <stdint.h>
#include "../src/ekg_config.h"

/* Constants */
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
} metrics_t;

/* ========================================
 * Function Prototypes
 * ======================================== */

/* Dataset loading functions */
int load_dataset(const char *base_path, dataset_t *dataset);
void free_dataset(dataset_t *dataset);

/* Evaluation functions */
void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
                    const uint32_t *file_indices, uint32_t num_files,
                    metrics_t *metrics, uint32_t source_sample_rate);
void calculate_metrics(metrics_t *metrics);

/* Reporting functions (shared with ekg_train) */
void print_metrics(const metrics_t *metrics);
void print_config_params(const ekg_config_t *config);

#endif /* EVAL_UTILS_H */
