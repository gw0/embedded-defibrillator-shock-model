/**
 * Simple verification test for signal processing functions
 *
 * This program tests:
 * 1. DC offset removal
 * 2. Bandpass filter with sine waves at different frequencies
 * 3. Moving average filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/signal_processing.h"
#include "../src/ekg_config.h"

#define TEST_SAMPLES 500
#define SAMPLE_RATE 100

/* Generate a sine wave */
void generate_sine(float *output, uint16_t num_samples, float freq, float amplitude, float offset)
{
    for (uint16_t i = 0; i < num_samples; i++) {
        float t = (float)i / (float)SAMPLE_RATE;
        output[i] = amplitude * sinf(2.0f * PI * freq * t) + offset;
    }
}

/* Calculate RMS (root mean square) of signal */
float calculate_rms(const float *signal, uint16_t num_samples)
{
    float sum = 0.0f;
    for (uint16_t i = 0; i < num_samples; i++) {
        sum += signal[i] * signal[i];
    }
    return sqrtf(sum / (float)num_samples);
}

/* Calculate mean of signal */
float calculate_mean(const float *signal, uint16_t num_samples)
{
    float sum = 0.0f;
    for (uint16_t i = 0; i < num_samples; i++) {
        sum += signal[i];
    }
    return sum / (float)num_samples;
}

/* Test 1: DC Offset Removal */
void test_dc_offset_removal(void)
{
    printf("\n=== Test 1: DC Offset Removal ===\n");

    int16_t input[TEST_SAMPLES];
    float output[TEST_SAMPLES];

    /* Generate signal with DC offset */
    float dc_offset = 500.0f;
    for (uint16_t i = 0; i < TEST_SAMPLES; i++) {
        input[i] = (int16_t)(100.0f * sinf(2.0f * PI * 10.0f * (float)i / (float)SAMPLE_RATE) + dc_offset);
    }

    /* Remove DC offset */
    remove_dc_offset(input, output, TEST_SAMPLES);

    /* Calculate mean of output (should be close to 0) */
    float mean_after = calculate_mean(output, TEST_SAMPLES);

    printf("Input DC offset: %.2f\n", dc_offset);
    printf("Output mean: %.6f (should be ~0)\n", mean_after);
    printf("Test %s\n", (fabsf(mean_after) < 0.1f) ? "PASSED" : "FAILED");
}

/* Test 2: Bandpass Filter */
void test_bandpass_filter(void)
{
    printf("\n=== Test 2: Bandpass Filter ===\n");

    float input[TEST_SAMPLES];
    float output[TEST_SAMPLES];
    float filter_state[FILTER_STATE_SIZE] = {0};

    /* Test frequencies: 1 Hz (should be attenuated), 10 Hz (should pass), 30 Hz (should be attenuated) */
    float test_freqs[] = {1.0f, 10.0f, 30.0f};
    const char* freq_names[] = {"1 Hz (below passband)", "10 Hz (in passband)", "30 Hz (above passband)"};
    int num_tests = 3;

    for (int t = 0; t < num_tests; t++) {
        /* Generate sine wave */
        generate_sine(input, TEST_SAMPLES, test_freqs[t], 1000.0f, 0.0f);

        /* Reset filter state */
        for (int i = 0; i < FILTER_STATE_SIZE; i++) {
            filter_state[i] = 0.0f;
        }

        /* Apply bandpass filter */
        bandpass_filter(input, output, TEST_SAMPLES, filter_state);

        /* Calculate RMS of input and output (skip first 50 samples for filter settling) */
        float input_rms = calculate_rms(&input[50], TEST_SAMPLES - 50);
        float output_rms = calculate_rms(&output[50], TEST_SAMPLES - 50);

        /* Calculate attenuation/gain */
        float gain_db = 20.0f * log10f(output_rms / input_rms);

        printf("%s:\n", freq_names[t]);
        printf("  Input RMS:  %.2f\n", input_rms);
        printf("  Output RMS: %.2f\n", output_rms);
        printf("  Gain:       %.2f dB\n", gain_db);

        /* Expected behavior:
         * - 1 Hz should be attenuated (gain << 0 dB)
         * - 10 Hz should pass with minimal attenuation (gain ~ 0 dB)
         * - 30 Hz should be attenuated (gain << 0 dB)
         */
    }
}

/* Test 3: Moving Average */
void test_moving_average(void)
{
    printf("\n=== Test 3: Moving Average ===\n");

    float input[TEST_SAMPLES];
    float output[TEST_SAMPLES];

    /* Generate noisy signal */
    for (uint16_t i = 0; i < TEST_SAMPLES; i++) {
        float clean = 100.0f * sinf(2.0f * PI * 5.0f * (float)i / (float)SAMPLE_RATE);
        float noise = ((float)rand() / (float)RAND_MAX - 0.5f) * 50.0f;
        input[i] = clean + noise;
    }

    /* Apply moving average */
    uint8_t window_size = 15;
    moving_average(input, output, TEST_SAMPLES, window_size);

    /* Calculate RMS of difference (noise reduction) */
    float noise_rms = 0.0f;
    for (uint16_t i = 50; i < TEST_SAMPLES - 50; i++) {
        float diff = input[i] - output[i];
        noise_rms += diff * diff;
    }
    noise_rms = sqrtf(noise_rms / (float)(TEST_SAMPLES - 100));

    printf("Window size: %d samples\n", window_size);
    printf("Noise reduction RMS: %.2f\n", noise_rms);
    printf("Test %s (smoothing applied)\n", (noise_rms > 10.0f) ? "PASSED" : "WARNING");
}

/* Calculate standard deviation */
float calculate_std_dev(const float *signal, uint16_t num_samples)
{
    float mean = calculate_mean(signal, num_samples);
    float variance_sum = 0.0f;
    for (uint16_t i = 0; i < num_samples; i++) {
        float diff = signal[i] - mean;
        variance_sum += diff * diff;
    }
    return sqrtf(variance_sum / (float)num_samples);
}

/* Test 4: Signal Normalization */
void test_normalization(void)
{
    printf("\n=== Test 4: Signal Normalization ===\n");

    float signal[TEST_SAMPLES];

    /* Subtest 1: Normalize signal with known std_dev */
    printf("\nSubtest 1: Normalization correctness\n");
    generate_sine(signal, TEST_SAMPLES, 10.0f, 1000.0f, 0.0f);
    float std_before = calculate_std_dev(signal, TEST_SAMPLES);
    normalize_signal(signal, TEST_SAMPLES, 500.0f);
    float std_after = calculate_std_dev(signal, TEST_SAMPLES);
    float mean_after = calculate_mean(signal, TEST_SAMPLES);

    printf("  Std dev before: %.2f\n", std_before);
    printf("  Std dev after:  %.2f (target: 500.0)\n", std_after);
    printf("  Mean after:     %.6f (should be ~0)\n", mean_after);
    printf("  Test %s\n", (fabsf(std_after - 500.0f) < 10.0f && fabsf(mean_after) < 1.0f) ? "PASSED" : "FAILED");

    /* Subtest 2: Zero-mean preservation */
    printf("\nSubtest 2: Zero-mean preservation\n");
    generate_sine(signal, TEST_SAMPLES, 8.0f, 447.0f, 0.0f);
    float mean_before = calculate_mean(signal, TEST_SAMPLES);
    normalize_signal(signal, TEST_SAMPLES, 500.0f);
    mean_after = calculate_mean(signal, TEST_SAMPLES);

    printf("  Mean before: %.6f\n", mean_before);
    printf("  Mean after:  %.6f (should remain ~0)\n", mean_after);
    printf("  Test %s\n", (fabsf(mean_after) < 1.0f) ? "PASSED" : "FAILED");

    /* Subtest 3: Edge case - flat signal */
    printf("\nSubtest 3: Flat signal handling\n");
    for (uint16_t i = 0; i < TEST_SAMPLES; i++) {
        signal[i] = 0.0f;
    }
    normalize_signal(signal, TEST_SAMPLES, 500.0f);
    int all_zero = 1;
    for (uint16_t i = 0; i < TEST_SAMPLES; i++) {
        if (signal[i] != 0.0f) {
            all_zero = 0;
            break;
        }
    }
    printf("  Signal remained unchanged: %s\n", all_zero ? "YES" : "NO");
    printf("  Test %s\n", all_zero ? "PASSED" : "FAILED");

    /* Subtest 4: Compatibility with old data (std ≈ 447) */
    printf("\nSubtest 4: Old data compatibility\n");
    generate_sine(signal, TEST_SAMPLES, 10.0f, 447.0f, 0.0f);
    std_before = calculate_std_dev(signal, TEST_SAMPLES);
    normalize_signal(signal, TEST_SAMPLES, 500.0f);
    std_after = calculate_std_dev(signal, TEST_SAMPLES);
    float scale_factor = std_after / std_before;

    printf("  Std dev before: %.2f\n", std_before);
    printf("  Std dev after:  %.2f\n", std_after);
    printf("  Scale factor:   %.3f (expected: ~1.12)\n", scale_factor);
    printf("  Test %s (minimal scaling applied)\n", (scale_factor > 1.0f && scale_factor < 1.2f) ? "PASSED" : "FAILED");
}

int main(void)
{
    printf("Signal Processing Module - Verification Tests\n");
    printf("Sample rate: %d Hz\n", SAMPLE_RATE);
    printf("Test samples: %d\n", TEST_SAMPLES);

    test_dc_offset_removal();
    test_bandpass_filter();
    test_moving_average();
    test_normalization();

    printf("\n=== All tests complete ===\n");

    return 0;
}
