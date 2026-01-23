/**
 * Evaluation Utilities Implementation
 *
 * Shared dataset loading, evaluation, and reporting functions.
 *
 * For educational and prototype purposes only - NOT for medical use.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
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
#ifdef DIAG_ENABLED
#include "../src/vf_detector.h"
#endif

/* Internal constants */
#define MAX_FILES 15000
#define MAX_SAMPLES 100000

/* ========================================
 * Static Helper Functions
 * ======================================== */

/* Parse rhythm type from filename */
static uint8_t parse_rhythm_type(const char *filename)
{
    if (strstr(filename, "vfib=coarse") != NULL) {
        return RHYTHM_VF_COARSE;
    }
    if (strstr(filename, "vfib=fine") != NULL) {
        return RHYTHM_VF_FINE;
    }
    if (strstr(filename, "nsr=") != NULL) {
        return RHYTHM_NSR;
    }
    if (strstr(filename, "vntwave=") != NULL) {
        return RHYTHM_VNTWAVE;
    }
    if (strstr(filename, "prewave=") != NULL) {
        return RHYTHM_PREWAVE;
    }
    if (strstr(filename, "spvwave=") != NULL) {
        return RHYTHM_SPVWAVE;
    }
    if (strstr(filename, "cndwave=") != NULL) {
        return RHYTHM_CNDWAVE;
    }
    return RHYTHM_UNKNOWN;
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

/* ========================================
 * Dataset Loading Functions
 * ======================================== */

/* Load complete dataset from base path */
int load_dataset(const char *base_path, dataset_t *dataset)
{
    /* Allocate file array */
    dataset->files = malloc(MAX_FILES * sizeof(dataset_file_t));
    if (dataset->files == NULL) {
        fprintf(stderr, "Error: Failed to allocate dataset memory (%zu bytes)\n",
                MAX_FILES * sizeof(dataset_file_t));
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
        metrics->f2_score = 0.0f;
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

    /* F1-score: harmonic mean of precision and sensitivity */
    if (metrics->precision + metrics->sensitivity > 0) {
        metrics->f1_score = 2.0f * (metrics->precision * metrics->sensitivity) /
                           (metrics->precision + metrics->sensitivity);
    } else {
        metrics->f1_score = 0.0f;
    }

    /* F2-score: F-beta with beta=2, weights sensitivity 4x over precision
     * Formula: (1 + beta^2) * (precision * sensitivity) / (beta^2 * precision + sensitivity)
     * With beta=2: (1 + 4) * (P * S) / (4 * P + S) = 5 * P * S / (4 * P + S)
     */
    if (4.0f * metrics->precision + metrics->sensitivity > 0) {
        metrics->f2_score = 5.0f * (metrics->precision * metrics->sensitivity) /
                           (4.0f * metrics->precision + metrics->sensitivity);
    } else {
        metrics->f2_score = 0.0f;
    }
}

/* Diagnostic tracking - file scope for simplicity */
#ifdef DIAG_ENABLED
static diag_fail_breakdown_t g_diag_fails;
#endif

/* Evaluate configuration with per-rhythm breakdown */
void evaluate_config(const ekg_config_t *config, const dataset_t *dataset,
                    const uint32_t *file_indices, uint32_t num_files,
                    eval_result_t *result, uint32_t source_sample_rate)
{
    int16_t *raw_samples = NULL;
    uint32_t num_raw_samples = 0;
    int16_t *samples = NULL;
    uint32_t num_samples = 0;

    /* Reset result */
    memset(result, 0, sizeof(eval_result_t));
#ifdef DIAG_ENABLED
    memset(&g_diag_fails, 0, sizeof(g_diag_fails));
#endif

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
        int num_windows = ((num_samples - WINDOW_SIZE) / WINDOW_STEP) + 1;
        if (num_windows == 0) {
            continue;
        }

        /* Any-positive aggregation: file classified as SHOCK if ANY window advises shock */
        uint8_t file_shock_advised = 0;
#ifdef DIAG_ENABLED
        uint8_t file_fail_reason = 0;
#endif

        /* Analyze each window */
        for (int w = 0; w < num_windows; w++) {
            uint32_t window_offset = w * WINDOW_STEP;

            /* Boundary check */
            if (window_offset + WINDOW_SIZE > num_samples) {
                break;
            }

            /* Analyze this window */
            ekg_analysis_result_t analysis;
            int ret = ekg_analyze(&samples[window_offset], WINDOW_SIZE, &analysis, config);

            if (ret != EKG_SUCCESS) {
                continue;
            }

            /* Any-positive: if any window advises shock, mark file as shock */
            if (analysis.shock_advised) {
                file_shock_advised = 1;
                break;
            }

#ifdef DIAG_ENABLED
            /* Track failure reasons for shockable rhythms that were missed */
            if (file->ground_truth_shock && !analysis.shock_advised) {
                file_fail_reason |= analysis.diag_fail_reason;
            }
#endif
        }

        /* Update overall confusion matrix */
        result->overall.tp += (file_shock_advised && file->ground_truth_shock);
        result->overall.tn += (!file_shock_advised && !file->ground_truth_shock);
        result->overall.fp += (file_shock_advised && !file->ground_truth_shock);
        result->overall.fn += (!file_shock_advised && file->ground_truth_shock);

#ifdef DIAG_ENABLED
        /* Track failure reasons for false negatives */
        if (!file_shock_advised && file->ground_truth_shock) {
            g_diag_fails.diag_total_fn++;
            if (file_fail_reason & DIAG_FAIL_AMPLITUDE) g_diag_fails.diag_amplitude_fails++;
            if (file_fail_reason & DIAG_FAIL_FREQUENCY) g_diag_fails.diag_frequency_fails++;
            if (file_fail_reason & DIAG_FAIL_FREQ_SUBOPT) g_diag_fails.diag_freq_subopt_fails++;
            if (file_fail_reason & DIAG_FAIL_IRREGULARITY) g_diag_fails.diag_irregularity_fails++;
            if (file_fail_reason & DIAG_FAIL_ORGANIZED) g_diag_fails.diag_organized_fails++;
            if (file_fail_reason & DIAG_FAIL_QRS_PENALTY) g_diag_fails.diag_qrs_penalty_fails++;
            if (file_fail_reason & DIAG_FAIL_CONFIDENCE) g_diag_fails.diag_confidence_fails++;
            if (file_fail_reason & DIAG_FAIL_LOW_EVIDENCE) g_diag_fails.diag_low_evidence_fails++;
        }
#endif

        /* Update per-rhythm metrics based on rhythm type */
        metrics_t *rhythm_metric = NULL;
        uint8_t is_shockable = file->ground_truth_shock;

        switch (file->ground_truth_rhythm) {
            case RHYTHM_VF_COARSE:
                rhythm_metric = &result->vf_coarse;
                break;
            case RHYTHM_VF_FINE:
                rhythm_metric = &result->vf_fine;
                break;
            case RHYTHM_NSR:
                rhythm_metric = &result->nsr;
                break;
            case RHYTHM_VNTWAVE:
                rhythm_metric = &result->vntwave;
                break;
            case RHYTHM_PREWAVE:
                rhythm_metric = &result->prewave;
                break;
            case RHYTHM_SPVWAVE:
                rhythm_metric = &result->spvwave;
                break;
            case RHYTHM_CNDWAVE:
                rhythm_metric = &result->cndwave;
                break;
            default:
                rhythm_metric = is_shockable ?
                    &result->other_shockable : &result->other_nonshockable;
                break;
        }

        rhythm_metric->tp += (file_shock_advised && is_shockable);
        rhythm_metric->tn += (!file_shock_advised && !is_shockable);
        rhythm_metric->fp += (file_shock_advised && !is_shockable);
        rhythm_metric->fn += (!file_shock_advised && is_shockable);
    }

    free(raw_samples);
    if (source_sample_rate != 100) {
        free(samples);
    }

    /* Calculate derived metrics */
    calculate_metrics(&result->overall);
    calculate_metrics(&result->vf_coarse);
    calculate_metrics(&result->vf_fine);
    calculate_metrics(&result->nsr);
    calculate_metrics(&result->vntwave);
    calculate_metrics(&result->prewave);
    calculate_metrics(&result->spvwave);
    calculate_metrics(&result->cndwave);
    calculate_metrics(&result->other_shockable);
    calculate_metrics(&result->other_nonshockable);
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
    printf("  F2-Score:    %.1f%%\n", metrics->f2_score * 100.0f);
    printf("  Confusion: TP=%u TN=%u FP=%u FN=%u\n",
           metrics->tp, metrics->tn, metrics->fp, metrics->fn);
}

/* Print evaluation result with per-rhythm breakdown */
void print_result(const eval_result_t *result)
{
    printf("Overall Metrics:\n");
    print_metrics(&result->overall);
    printf("\n");

    printf("Per-Rhythm Breakdown (Shockable):\n");
    if (result->vf_coarse.tp + result->vf_coarse.fn > 0) {
        printf("  VF Coarse:   Total=%4u  TP=%4u  FN=%4u  Sens=%.1f%%\n",
               result->vf_coarse.tp + result->vf_coarse.fn,
               result->vf_coarse.tp, result->vf_coarse.fn,
               result->vf_coarse.sensitivity * 100.0f);
    }
    if (result->vf_fine.tp + result->vf_fine.fn > 0) {
        printf("  VF Fine:     Total=%4u  TP=%4u  FN=%4u  Sens=%.1f%%\n",
               result->vf_fine.tp + result->vf_fine.fn,
               result->vf_fine.tp, result->vf_fine.fn,
               result->vf_fine.sensitivity * 100.0f);
    }
    if (result->other_shockable.tp + result->other_shockable.fn > 0) {
        printf("  Other Shock: Total=%4u  TP=%4u  FN=%4u  Sens=%.1f%%\n",
               result->other_shockable.tp + result->other_shockable.fn,
               result->other_shockable.tp, result->other_shockable.fn,
               result->other_shockable.sensitivity * 100.0f);
    }
    printf("\n");

    printf("Per-Rhythm Breakdown (Non-Shockable):\n");
    if (result->nsr.tn + result->nsr.fp > 0) {
        printf("  NSR:         Total=%4u  TN=%4u  FP=%4u  Spec=%.1f%%\n",
               result->nsr.tn + result->nsr.fp,
               result->nsr.tn, result->nsr.fp,
               result->nsr.specificity * 100.0f);
    }
    if (result->vntwave.tn + result->vntwave.fp > 0) {
        printf("  Ventricular: Total=%4u  TN=%4u  FP=%4u  Spec=%.1f%%\n",
               result->vntwave.tn + result->vntwave.fp,
               result->vntwave.tn, result->vntwave.fp,
               result->vntwave.specificity * 100.0f);
    }
    if (result->prewave.tn + result->prewave.fp > 0) {
        printf("  Premature:   Total=%4u  TN=%4u  FP=%4u  Spec=%.1f%%\n",
               result->prewave.tn + result->prewave.fp,
               result->prewave.tn, result->prewave.fp,
               result->prewave.specificity * 100.0f);
    }
    if (result->spvwave.tn + result->spvwave.fp > 0) {
        printf("  Supravent:   Total=%4u  TN=%4u  FP=%4u  Spec=%.1f%%\n",
               result->spvwave.tn + result->spvwave.fp,
               result->spvwave.tn, result->spvwave.fp,
               result->spvwave.specificity * 100.0f);
    }
    if (result->cndwave.tn + result->cndwave.fp > 0) {
        printf("  Conduction:  Total=%4u  TN=%4u  FP=%4u  Spec=%.1f%%\n",
               result->cndwave.tn + result->cndwave.fp,
               result->cndwave.tn, result->cndwave.fp,
               result->cndwave.specificity * 100.0f);
    }
    if (result->other_nonshockable.tn + result->other_nonshockable.fp > 0) {
        printf("  Other:       Total=%4u  TN=%4u  FP=%4u  Spec=%.1f%%\n",
               result->other_nonshockable.tn + result->other_nonshockable.fp,
               result->other_nonshockable.tn, result->other_nonshockable.fp,
               result->other_nonshockable.specificity * 100.0f);
    }

#ifdef DIAG_ENABLED
    diag_print_fail_breakdown(&g_diag_fails);
#endif
}

/* Print configuration parameters */
void print_config_params(const ekg_config_t *config)
{
    printf("Configuration:\n");

    /* VT Detection */
    printf("  vt_hr_thresh:               %.1f bpm\n", config->vt_hr_thresh);
    printf("  vt_qrs_width_ms:            %.1f ms\n", config->vt_qrs_width_ms);
    printf("  vt_regularity_cv:           %.1f%%\n", config->vt_regularity_cv);
    printf("  vt_confidence_thresh:       %.2f\n", config->vt_confidence_thresh);
    printf("  vt_min_consecutive:         %u\n", config->vt_min_consecutive);

    /* VT Bayesian Boundaries */
    printf("  vt_hr_high_thresh:          %.1f bpm\n", config->vt_hr_high_thresh);
    printf("  vt_qrs_vwide_thresh:        %.1f ms\n", config->vt_qrs_vwide_thresh);
    printf("  vt_vregular_cv_thresh:      %.1f%%\n", config->vt_vregular_cv_thresh);

    /* VT Bayesian Log-Likelihood Ratios */
    printf("  lr_vt_hr_thresh_pos:        %.2f\n", config->lr_vt_hr_thresh_pos);
    printf("  lr_vt_hr_thresh_neg:        %.2f\n", config->lr_vt_hr_thresh_neg);
    printf("  lr_vt_hr_high_pos:          %.2f\n", config->lr_vt_hr_high_pos);
    printf("  lr_vt_hr_high_neg:          %.2f\n", config->lr_vt_hr_high_neg);
    printf("  lr_vt_qrs_wide_pos:         %.2f\n", config->lr_vt_qrs_wide_pos);
    printf("  lr_vt_qrs_wide_neg:         %.2f\n", config->lr_vt_qrs_wide_neg);
    printf("  lr_vt_qrs_vwide_pos:        %.2f\n", config->lr_vt_qrs_vwide_pos);
    printf("  lr_vt_qrs_vwide_neg:        %.2f\n", config->lr_vt_qrs_vwide_neg);
    printf("  lr_vt_regular_pos:          %.2f\n", config->lr_vt_regular_pos);
    printf("  lr_vt_regular_neg:          %.2f\n", config->lr_vt_regular_neg);
    printf("  lr_vt_vregular_pos:         %.2f\n", config->lr_vt_vregular_pos);
    printf("  lr_vt_vregular_neg:         %.2f\n", config->lr_vt_vregular_neg);
    printf("  lr_vt_sustained_pos:        %.2f\n", config->lr_vt_sustained_pos);
    printf("  lr_vt_sustained_neg:        %.2f\n", config->lr_vt_sustained_neg);

    /* VF Detection */
    printf("  vf_freq_min:                %.1f Hz\n", config->vf_freq_min);
    printf("  vf_freq_max:                %.1f Hz\n", config->vf_freq_max);
    printf("  vf_dominance_thresh:        %.2f\n", config->vf_dominance_thresh);
    printf("  vf_noise_floor_mult:        %.1f\n", config->vf_noise_floor_mult);
    printf("  vf_tcr_thresh_ratio:        %.2f\n", config->vf_tcr_thresh_ratio);
    printf("  vf_evidence_thresh:         %.1f\n", config->vf_evidence_thresh);
    printf("  vf_confidence_thresh:       %.2f\n", config->vf_confidence_thresh);
    printf("  vf_qrs_rate_thresh:         %.2f\n", config->vf_qrs_rate_thresh);
    printf("  vf_fine_amplitude_max:      %.1f\n", config->vf_fine_amplitude_max);
    printf("  vf_penalty_freq_min:        %.1f Hz\n", config->vf_penalty_freq_min);
    printf("  vf_penalty_freq_max:        %.1f Hz\n", config->vf_penalty_freq_max);

    /* VF Amplitude Thresholds */
    printf("  vf_amp_high_thresh:         %.1f\n", config->vf_amp_high_thresh);
    printf("  vf_amp_medium_thresh:       %.1f\n", config->vf_amp_medium_thresh);
    printf("  vf_amp_low_thresh:          %.1f\n", config->vf_amp_low_thresh);
    printf("  vf_amp_vlow_thresh:         %.1f\n", config->vf_amp_vlow_thresh);

    /* VF Irregularity Thresholds */
    printf("  vf_zcr_high_thresh:         %.2f\n", config->vf_zcr_high_thresh);
    printf("  vf_zcr_mod_thresh:          %.2f\n", config->vf_zcr_mod_thresh);
    printf("  vf_tcr_high_thresh:         %.2f\n", config->vf_tcr_high_thresh);
    printf("  vf_tcr_mod_thresh:          %.2f\n", config->vf_tcr_mod_thresh);

    /* VF Entropy Thresholds */
    printf("  vf_entropy_high_thresh:     %.2f\n", config->vf_entropy_high_thresh);
    printf("  vf_entropy_mod_thresh:      %.2f\n", config->vf_entropy_mod_thresh);

    /* VF Spectral Flatness Thresholds */
    printf("  vf_flatness_high_thresh:    %.2f\n", config->vf_flatness_high_thresh);
    printf("  vf_flatness_mod_thresh:     %.2f\n", config->vf_flatness_mod_thresh);

    /* VF Organized Rhythm Detection */
    printf("  vf_org_hr_min:              %.1f bpm\n", config->vf_org_hr_min);
    printf("  vf_org_hr_max:              %.1f bpm\n", config->vf_org_hr_max);
    printf("  vf_org_cv_thresh:           %.1f%%\n", config->vf_org_cv_thresh);
    printf("  vf_qrs_rate_penalty_thresh: %.2f\n", config->vf_qrs_rate_penalty_thresh);

    /* VF Evidence Weights - Amplitude */
    printf("  vf_ev_amp_high:             %.2f\n", config->vf_ev_amp_high);
    printf("  vf_ev_amp_medium:           %.2f\n", config->vf_ev_amp_medium);
    printf("  vf_ev_amp_low:              %.2f\n", config->vf_ev_amp_low);
    printf("  vf_ev_amp_vlow:             %.2f\n", config->vf_ev_amp_vlow);

    /* VF Evidence Weights - Frequency */
    printf("  vf_ev_freq_optimal:         %.2f\n", config->vf_ev_freq_optimal);
    printf("  vf_ev_freq_good:            %.2f\n", config->vf_ev_freq_good);
    printf("  vf_ev_freq_edge:            %.2f\n", config->vf_ev_freq_edge);

    /* VF Evidence Weights - Irregularity */
    printf("  vf_ev_qrs_absent:           %.2f\n", config->vf_ev_qrs_absent);
    printf("  vf_ev_high_irreg:           %.2f\n", config->vf_ev_high_irreg);
    printf("  vf_ev_mod_irreg:            %.2f\n", config->vf_ev_mod_irreg);

    /* VF Evidence Weights - Entropy */
    printf("  vf_ev_entropy_high:         %.2f\n", config->vf_ev_entropy_high);
    printf("  vf_ev_entropy_mod:          %.2f\n", config->vf_ev_entropy_mod);

    /* VF Evidence Weights - Spectral Flatness */
    printf("  vf_ev_flatness_high:        %.2f\n", config->vf_ev_flatness_high);
    printf("  vf_ev_flatness_mod:         %.2f\n", config->vf_ev_flatness_mod);

    /* VF Evidence Weights - Penalties */
    printf("  vf_ev_organized_penalty:    %.2f\n", config->vf_ev_organized_penalty);
    printf("  vf_ev_qrs_freq_penalty:     %.2f\n", config->vf_ev_qrs_freq_penalty);
    printf("  vf_ev_qrs_regular_penalty:  %.2f\n", config->vf_ev_qrs_regular_penalty);

    /* Signal Quality */
    printf("  snr_good_thresh:            %.1f\n", config->snr_good_thresh);
    printf("  signal_quality_min:         %.2f\n", config->signal_quality_min);
}

/* ========================================
 * Diagnostic Functions (DIAG_ENABLED)
 * ======================================== */

#ifdef DIAG_ENABLED

void diag_print_fail_breakdown(const diag_fail_breakdown_t *fails)
{
    printf("\nFailure Breakdown (False Negatives: %u):\n", fails->diag_total_fn);
    if (fails->diag_total_fn > 0) {
        printf("  Amplitude:     %4u (%5.1f%%)\n", fails->diag_amplitude_fails,
               100.0f * fails->diag_amplitude_fails / fails->diag_total_fn);
        printf("  Frequency:     %4u (%5.1f%%)\n", fails->diag_frequency_fails,
               100.0f * fails->diag_frequency_fails / fails->diag_total_fn);
        printf("  Freq SubOpt:   %4u (%5.1f%%)\n", fails->diag_freq_subopt_fails,
               100.0f * fails->diag_freq_subopt_fails / fails->diag_total_fn);
        printf("  Irregularity:  %4u (%5.1f%%)\n", fails->diag_irregularity_fails,
               100.0f * fails->diag_irregularity_fails / fails->diag_total_fn);
        printf("  Organized:     %4u (%5.1f%%)\n", fails->diag_organized_fails,
               100.0f * fails->diag_organized_fails / fails->diag_total_fn);
        printf("  QRS Penalty:   %4u (%5.1f%%)\n", fails->diag_qrs_penalty_fails,
               100.0f * fails->diag_qrs_penalty_fails / fails->diag_total_fn);
        printf("  Confidence:    %4u (%5.1f%%)\n", fails->diag_confidence_fails,
               100.0f * fails->diag_confidence_fails / fails->diag_total_fn);
        printf("  Low Evidence:  %4u (%5.1f%%)\n", fails->diag_low_evidence_fails,
               100.0f * fails->diag_low_evidence_fails / fails->diag_total_fn);
    }
}

#endif /* DIAG_ENABLED */
