/**
 * EKG Signal Analyzer - Signal Processing Module
 *
 * Functions for preprocessing EKG signals including DC offset removal,
 * bandpass filtering, and moving average.
 */

#ifndef SIGNAL_PROCESSING_H
#define SIGNAL_PROCESSING_H

#include <stdint.h>

/**
 * Remove DC offset from signal by subtracting mean value
 *
 * @param input       Input signal (int16_t array)
 * @param output      Output signal (float array)
 * @param num_samples Number of samples
 */
void remove_dc_offset(const int16_t *input, float *output, uint16_t num_samples);

/**
 * Apply bandpass filter (5-18 Hz) to emphasize QRS complexes
 * Uses cascaded 2nd-order IIR Butterworth filters
 *
 * @param input       Input signal (float array)
 * @param output      Output signal (float array)
 * @param num_samples Number of samples
 * @param state       Filter state for IIR (8 floats, initialized to 0)
 */
void bandpass_filter(const float *input, float *output,
                     uint16_t num_samples, float *state);

/**
 * Apply moving average filter for smoothing
 *
 * @param input       Input signal
 * @param output      Output signal
 * @param num_samples Number of samples
 * @param window_size Window size for averaging (e.g., 15 for 150ms at 100Hz)
 */
void moving_average(const float *input, float *output,
                   uint16_t num_samples, uint8_t window_size);

#endif /* SIGNAL_PROCESSING_H */
