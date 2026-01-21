/**
 * EKG Evaluation Tool
 *
 * Evaluates the default EKG analyzer configuration on a labeled dataset.
 * Useful for cross-dataset validation and testing.
 *
 * For educational and prototype purposes only - NOT for medical use.
 *
 * Copyright &copy; 2025-2026 *gw0* [<http://gw.tnode.com/>] &lt;<gw.2026@ena.one>&gt;
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "../src/ekg_analyzer.h"
#include "../src/ekg_config.h"
#include "csv_utils.h"
#include "eval_utils.h"

/* ========================================
 * Function Prototypes
 * ======================================== */

static void print_help(const char *prog_name);

/* ========================================
 * Main Function
 * ======================================== */

int main(int argc, char *argv[])
{
    /* Default options */
    uint32_t source_sample_rate = 100;  /* Default: no downsampling */
    const char *dataset_path = NULL;

    /* Parse command-line arguments */
    int opt;
    struct option long_options[] = {
        {"sample-rate", required_argument, 0, 'r'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "r:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'r':
                source_sample_rate = atoi(optarg);
                if (source_sample_rate != 100 && source_sample_rate != 200 &&
                    source_sample_rate != 500 && source_sample_rate != 1000 &&
                    source_sample_rate != 2000 && source_sample_rate != 5000 &&
                    source_sample_rate != 10000) {
                    fprintf(stderr, "Error: Invalid sample rate. Supported: 100, 200, 500, 1000, 2000, 5000, 10000 Hz\n");
                    return 2;
                }
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 2;
        }
    }

    /* Check for dataset path argument */
    if (optind >= argc) {
        fprintf(stderr, "Error: Dataset path required\n\n");
        print_help(argv[0]);
        return 2;
    }
    dataset_path = argv[optind];

    /* Print header */
    printf("========================================\n");
    printf("EKG Evaluation Tool\n");
    printf("========================================\n");
    printf("Dataset: %s\n", dataset_path);
    printf("Source sample rate: %u Hz\n", source_sample_rate);
    printf("\n");

    /* Load dataset */
    printf("Loading dataset...\n");
    dataset_t dataset;
    if (load_dataset(dataset_path, &dataset) != 0) {
        fprintf(stderr, "Error: Failed to load dataset\n");
        return 1;
    }

    printf("Files loaded: %u (%u SHOCK, %u NOSHOCK)\n",
           dataset.count, dataset.shock_count, dataset.noshock_count);
    printf("\n");

    /* Print configuration parameters */
    print_config_params(&EKG_DEFAULT_CONFIG);
    printf("\n");

    /* Evaluate default configuration */
    printf("Evaluating...\n");

    eval_result_t result;
    evaluate_config(&EKG_DEFAULT_CONFIG, &dataset, NULL, dataset.count,
                    &result, source_sample_rate);

    /* Print results */
    printf("\n");
    printf("========================================\n");
    printf("EVALUATION RESULTS\n");
    printf("========================================\n");
    print_result(&result);
    printf("========================================\n");

    /* Cleanup */
    free_dataset(&dataset);

    return 0;
}

/* ========================================
 * Helper Functions
 * ======================================== */

static void print_help(const char *prog_name)
{
    printf("Usage: %s [OPTIONS] <dataset_path>\n", prog_name);
    printf("\n");
    printf("Evaluate default EKG analyzer configuration on a labeled dataset.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  <dataset_path>      Path to dataset directory (must contain SHOCK/ and NOSHOCK/)\n");
    printf("\n");
    printf("Options:\n");
    printf("  -r, --sample-rate <Hz>  Source sample rate (default: 100)\n");
    printf("                          Supported: 100, 200, 500, 1000, 2000, 5000, 10000\n");
    printf("  -h, --help              Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s data/20251129-ECGrecording/PROCESSEDINTEGER/\n", prog_name);
    printf("  %s -r 500 data/high-rate-dataset/\n", prog_name);
    printf("\n");
}
