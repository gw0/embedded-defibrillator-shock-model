/**
 * VF Detector Unit Tests
 *
 * Tests for VF detection including fine VF detection path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../src/vf_detector.h"
#include "../src/qrs_detector.h"
#include "../src/ekg_config.h"

#define TEST_SAMPLES 500  /* 5 seconds at 100 Hz */

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* Helper to generate coarse VF signal */
void generate_coarse_vf(float *signal, uint16_t num_samples, float amplitude)
{
    for (uint16_t i = 0; i < num_samples; i++) {
        float t = (float)i / (float)EKG_SAMPLE_RATE;

        /* Mix of frequencies in VF range (4-10 Hz) */
        float vf1 = amplitude * 0.40f * sinf(2.0f * PI * 6.0f * t);
        float vf2 = amplitude * 0.30f * sinf(2.0f * PI * 5.2f * t + 1.1f);
        float vf3 = amplitude * 0.20f * sinf(2.0f * PI * 7.5f * t + 2.3f);
        float vf4 = amplitude * 0.10f * sinf(2.0f * PI * 8.3f * t + 0.8f);

        /* Amplitude modulation for chaotic appearance */
        float amp_mod = 0.8f + 0.2f * sinf(2.0f * PI * 0.5f * t);

        /* Add broadband noise for realistic spectral entropy (real VF is chaotic) */
        float noise = ((float)(rand() % 1000) - 500.0f) / 500.0f * amplitude * 0.15f;

        signal[i] = (vf1 + vf2 + vf3 + vf4) * amp_mod + noise;
    }
}

/* Helper to generate fine VF signal (lower amplitude) */
void generate_fine_vf(float *signal, uint16_t num_samples, float amplitude)
{
    for (uint16_t i = 0; i < num_samples; i++) {
        float t = (float)i / (float)EKG_SAMPLE_RATE;

        /* Fine VF has similar frequency but lower amplitude */
        float vf1 = amplitude * 0.35f * sinf(2.0f * PI * 6.5f * t);
        float vf2 = amplitude * 0.30f * sinf(2.0f * PI * 5.8f * t + 0.7f);
        float vf3 = amplitude * 0.20f * sinf(2.0f * PI * 7.2f * t + 1.5f);
        float vf4 = amplitude * 0.15f * sinf(2.0f * PI * 8.0f * t + 2.1f);

        /* More irregular amplitude modulation */
        float amp_mod = 0.7f + 0.3f * sinf(2.0f * PI * 0.8f * t + sinf(2.0f * PI * 0.3f * t));

        /* Add broadband noise for realistic spectral entropy */
        float noise = ((float)(rand() % 1000) - 500.0f) / 500.0f * amplitude * 0.20f;

        signal[i] = (vf1 + vf2 + vf3 + vf4) * amp_mod + noise;
    }
}

/* Helper to generate normal sinus rhythm */
void generate_nsr(float *signal, uint16_t num_samples, float hr_bpm)
{
    float rr_interval_sec = 60.0f / hr_bpm;
    uint16_t rr_samples = (uint16_t)(rr_interval_sec * EKG_SAMPLE_RATE);

    /* Initialize to baseline with small noise */
    for (uint16_t i = 0; i < num_samples; i++) {
        signal[i] = ((float)(rand() % 20) - 10.0f);
    }

    /* Generate QRS complexes */
    for (uint16_t beat = 0; beat * rr_samples < num_samples; beat++) {
        uint16_t qrs_start = beat * rr_samples;
        if (qrs_start >= num_samples) break;

        /* QRS complex */
        for (int j = -2; j <= 5 && (qrs_start + j) < num_samples; j++) {
            if (qrs_start + j < 0) continue;
            if (j < 0) {
                signal[qrs_start + j] = -100.0f;
            } else if (j <= 2) {
                signal[qrs_start + j] = 1500.0f - 400.0f * (float)abs(j - 1);
            } else {
                signal[qrs_start + j] = -150.0f;
            }
        }
    }
}

/* Helper to generate asystole (flat line with noise) */
void generate_asystole(float *signal, uint16_t num_samples)
{
    for (uint16_t i = 0; i < num_samples; i++) {
        signal[i] = ((float)(rand() % 20) - 10.0f);
    }
}

/* Test helper */
void run_test(const char *name, int passed)
{
    if (passed) {
        printf("  [PASS] %s\n", name);
        tests_passed++;
    } else {
        printf("  [FAIL] %s\n", name);
        tests_failed++;
    }
}

/* Test: Coarse VF detection */
void test_coarse_vf_detection(void)
{
    printf("\n=== Test: Coarse VF Detection ===\n");

    float signal[TEST_SAMPLES];
    generate_coarse_vf(signal, TEST_SAMPLES, 500.0f);  /* High amplitude */

    /* Create empty QRS result (no QRS detected in VF) */
    qrs_detection_result_t qrs_result;
    memset(&qrs_result, 0, sizeof(qrs_result));
    qrs_result.count = 0;

    vf_detection_result_t vf_result;
    int ret = detect_vf(signal, TEST_SAMPLES, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);

    run_test("detect_vf returns success", ret == EKG_SUCCESS);
    run_test("VF detected", vf_result.vf_detected == 1);
    run_test("Not classified as fine VF", vf_result.is_fine_vf == 0);
    run_test("Dominant frequency in VF range",
             vf_result.dominant_freq >= VF_FREQ_MIN && vf_result.dominant_freq <= VF_FREQ_MAX);
    run_test("Confidence > 0.5", vf_result.confidence > 0.5f);

    printf("  Dominant freq: %.1f Hz, Confidence: %.2f\n",
           vf_result.dominant_freq, vf_result.confidence);
}

/* Test: Fine VF detection */
void test_fine_vf_detection(void)
{
    printf("\n=== Test: Fine VF Detection ===\n");

    float signal[TEST_SAMPLES];
    generate_fine_vf(signal, TEST_SAMPLES, 80.0f);  /* Lower amplitude for fine VF */

    /* Create empty QRS result */
    qrs_detection_result_t qrs_result;
    memset(&qrs_result, 0, sizeof(qrs_result));
    qrs_result.count = 0;

    vf_detection_result_t vf_result;
    int ret = detect_vf(signal, TEST_SAMPLES, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);

    run_test("detect_vf returns success", ret == EKG_SUCCESS);
    run_test("VF detected", vf_result.vf_detected == 1);

    printf("  Fine VF: %s, Amplitude: %.1f, Adaptive threshold: %.1f\n",
           vf_result.is_fine_vf ? "Yes" : "No",
           vf_result.amplitude, vf_result.adaptive_threshold);
    printf("  Dominant freq: %.1f Hz, Confidence: %.2f\n",
           vf_result.dominant_freq, vf_result.confidence);
}

/* Test: Normal sinus rhythm should NOT trigger VF */
void test_nsr_not_vf(void)
{
    printf("\n=== Test: NSR Should Not Trigger VF ===\n");

    float signal[TEST_SAMPLES];
    generate_nsr(signal, TEST_SAMPLES, 70.0f);

    /* Create QRS result with detected QRS complexes */
    qrs_detection_result_t qrs_result;
    memset(&qrs_result, 0, sizeof(qrs_result));
    qrs_result.count = 5;  /* ~70 bpm for 5 seconds */

    vf_detection_result_t vf_result;
    int ret = detect_vf(signal, TEST_SAMPLES, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);

    run_test("detect_vf returns success", ret == EKG_SUCCESS);
    run_test("VF NOT detected", vf_result.vf_detected == 0);

    printf("  Amplitude: %.1f, QRS absent: %d\n",
           vf_result.amplitude, vf_result.qrs_absent);
}

/* Test: Asystole should NOT trigger VF */
void test_asystole_not_vf(void)
{
    printf("\n=== Test: Asystole Should Not Trigger VF ===\n");

    float signal[TEST_SAMPLES];
    generate_asystole(signal, TEST_SAMPLES);

    qrs_detection_result_t qrs_result;
    memset(&qrs_result, 0, sizeof(qrs_result));
    qrs_result.count = 0;

    vf_detection_result_t vf_result;
    int ret = detect_vf(signal, TEST_SAMPLES, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);

    run_test("detect_vf returns success", ret == EKG_SUCCESS);
    run_test("VF NOT detected (amplitude too low)", vf_result.vf_detected == 0);

    printf("  Amplitude: %.1f, Adaptive threshold: %.1f\n",
           vf_result.amplitude, vf_result.adaptive_threshold);
}

/* Test: New helper functions */
void test_helper_functions(void)
{
    printf("\n=== Test: Helper Functions ===\n");

    float signal[TEST_SAMPLES];
    generate_coarse_vf(signal, TEST_SAMPLES, 300.0f);

    /* Test noise floor estimation */
    float noise_floor = estimate_noise_floor(signal, TEST_SAMPLES);
    run_test("Noise floor > 0", noise_floor > 0.0f);
    printf("  Noise floor: %.1f\n", noise_floor);

    /* Test amplitude calculation */
    float amplitude = calculate_amplitude(signal, TEST_SAMPLES);
    run_test("Amplitude > 0", amplitude > 0.0f);
    printf("  Amplitude: %.1f\n", amplitude);

    /* Test threshold crossing rate */
    float threshold = amplitude * 0.1f;
    crossing_result_t tcr = calculate_threshold_crossing_rate(signal, TEST_SAMPLES,
                                                               EKG_SAMPLE_RATE, threshold);
    run_test("Threshold crossing rate > 0", tcr.rate > 0.0f);
    run_test("Threshold crossing confidence in [0,1]", tcr.confidence >= 0.0f && tcr.confidence <= 1.0f);
    printf("  Threshold crossing rate: %.1f/s (conf=%.2f, threshold=%.1f)\n", tcr.rate, tcr.confidence, threshold);

    /* Test Goertzel with Hanning window */
    float mag_6hz = goertzel_magnitude(signal, TEST_SAMPLES, 6.0f, EKG_SAMPLE_RATE);
    float mag_20hz = goertzel_magnitude(signal, TEST_SAMPLES, 20.0f, EKG_SAMPLE_RATE);
    run_test("6 Hz magnitude > 20 Hz magnitude for VF signal", mag_6hz > mag_20hz);
    printf("  Magnitude at 6 Hz: %.1f, at 20 Hz: %.1f\n", mag_6hz, mag_20hz);

    /* Test frequency detection with dominance threshold */
    freq_result_t freq = find_dominant_frequency(signal, TEST_SAMPLES, EKG_SAMPLE_RATE, 0.4f);
    run_test("Dominant frequency in VF range", freq.frequency >= VF_FREQ_MIN && freq.frequency <= VF_FREQ_MAX);
    run_test("Frequency confidence in [0,1]", freq.confidence >= 0.0f && freq.confidence <= 1.0f);
    printf("  Dominant frequency: %.1f Hz (confidence=%.2f)\n", freq.frequency, freq.confidence);

    /* Test zero-crossing rate */
    float zcr = calculate_zero_crossing_rate(signal, TEST_SAMPLES, EKG_SAMPLE_RATE);
    run_test("Zero crossing rate > 0", zcr > 0.0f);
    printf("  Zero crossing rate: %.1f/s\n", zcr);

    /* Test spectral entropy */
    entropy_result_t ent = calculate_spectral_entropy(signal, TEST_SAMPLES, EKG_SAMPLE_RATE);
    run_test("Spectral entropy in [0,1]", ent.entropy >= 0.0f && ent.entropy <= 1.0f);
    run_test("Entropy confidence in [0,1]", ent.confidence >= 0.0f && ent.confidence <= 1.0f);
    printf("  Spectral entropy: %.2f (confidence=%.2f)\n", ent.entropy, ent.confidence);

    /* Test organized tachycardia detection with regular rhythm */
    qrs_detection_result_t qrs_regular;
    memset(&qrs_regular, 0, sizeof(qrs_regular));
    qrs_regular.count = 6;
    uint16_t regular_locations[6] = {50, 100, 150, 200, 250, 300};  /* 120 bpm, very regular */
    qrs_regular.locations = regular_locations;
    uint8_t is_organized = check_organized_tachycardia(&qrs_regular, EKG_SAMPLE_RATE);
    run_test("Regular tachycardia detected as organized", is_organized == 1);
    printf("  Organized tachycardia (regular): %s\n", is_organized ? "yes" : "no");

    /* Test organized tachycardia detection with irregular rhythm (VF-like) */
    qrs_detection_result_t qrs_irregular;
    memset(&qrs_irregular, 0, sizeof(qrs_irregular));
    qrs_irregular.count = 2;  /* Too few QRS for assessment */
    uint16_t irregular_locations[2] = {50, 200};
    qrs_irregular.locations = irregular_locations;
    uint8_t is_organized_irreg = check_organized_tachycardia(&qrs_irregular, EKG_SAMPLE_RATE);
    run_test("Irregular/sparse QRS not detected as organized", is_organized_irreg == 0);
    printf("  Organized tachycardia (irregular): %s\n", is_organized_irreg ? "yes" : "no");
}

/* Test: Boundary conditions */
void test_boundary_conditions(void)
{
    printf("\n=== Test: Boundary Conditions ===\n");

    float signal[TEST_SAMPLES];
    generate_coarse_vf(signal, TEST_SAMPLES, 300.0f);

    qrs_detection_result_t qrs_result;
    memset(&qrs_result, 0, sizeof(qrs_result));
    vf_detection_result_t vf_result;

    /* Test NULL signal */
    int ret = detect_vf(NULL, TEST_SAMPLES, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);
    run_test("NULL signal returns error", ret == EKG_ERROR_NULL_PTR);

    /* Test NULL qrs_result */
    ret = detect_vf(signal, TEST_SAMPLES, NULL, EKG_SAMPLE_RATE, NULL, &vf_result);
    run_test("NULL qrs_result returns error", ret == EKG_ERROR_NULL_PTR);

    /* Test NULL result */
    ret = detect_vf(signal, TEST_SAMPLES, &qrs_result, EKG_SAMPLE_RATE, NULL, NULL);
    run_test("NULL result returns error", ret == EKG_ERROR_NULL_PTR);

    /* Test too short signal */
    ret = detect_vf(signal, 200, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);
    run_test("Too short signal returns error", ret == EKG_ERROR_INVALID_LEN);

    /* Test too long signal */
    ret = detect_vf(signal, 1100, &qrs_result, EKG_SAMPLE_RATE, NULL, &vf_result);
    run_test("Too long signal returns error", ret == EKG_ERROR_INVALID_LEN);
}

int main(void)
{
    printf("VF Detector Unit Tests\n");
    printf("======================\n");

    /* Seed random number generator */
    srand(42);  /* Fixed seed for reproducibility */

    /* Run all tests */
    test_coarse_vf_detection();
    test_fine_vf_detection();
    test_nsr_not_vf();
    test_asystole_not_vf();
    test_helper_functions();
    test_boundary_conditions();

    /* Summary */
    printf("\n======================\n");
    printf("Test Summary:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("======================\n");

    return (tests_failed == 0) ? 0 : 1;
}
