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

/* Rhythm type constants */
#define RHYTHM_UNKNOWN     0
#define RHYTHM_NSR         1   /* nsr= */
#define RHYTHM_VF_COARSE   2   /* vfib=coarse */
#define RHYTHM_VF_FINE     3   /* vfib=fine */
#define RHYTHM_VNTWAVE     4   /* vntwave= (ventricular) */
#define RHYTHM_PREWAVE     5   /* prewave= (premature beats) */
#define RHYTHM_SPVWAVE     6   /* spvwave= (supraventricular) */
#define RHYTHM_CNDWAVE     7   /* cndwave= (conduction) */

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
    uint8_t ground_truth_rhythm;  /* RHYTHM_* constant */
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

/* Evaluation result with per-rhythm breakdown */
typedef struct {
    metrics_t overall;
    metrics_t vf_coarse;
    metrics_t vf_fine;
    metrics_t nsr;
    metrics_t vntwave;           /* Ventricular */
    metrics_t prewave;           /* Premature beats */
    metrics_t spvwave;           /* Supraventricular */
    metrics_t cndwave;           /* Conduction */
    metrics_t other_shockable;
    metrics_t other_nonshockable;
} eval_result_t;

/* ========================================
 * Function Prototypes
 * ======================================== */

/* Dataset loading functions */
int load_dataset(const char *base_path, dataset_t *dataset);
void free_dataset(dataset_t *dataset);

/* Evaluation functions */
void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
                    const uint32_t *file_indices, uint32_t num_files,
                    eval_result_t *result, uint32_t source_sample_rate);
void calculate_metrics(metrics_t *metrics);

/* Reporting functions */
void print_metrics(const metrics_t *metrics);
void print_result(const eval_result_t *result);
void print_config_params(const ekg_config_t *config);

#endif /* EVAL_UTILS_H */
