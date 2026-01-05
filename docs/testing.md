# Testing

## Test Suite Structure

### Unit Tests (`tests/test_signal_processing.c`)
- DC offset removal verification
- Bandpass filter response at various frequencies
- Moving average filter tests
- Signal processing pipeline validation

### Integration Tests (`tests/test_ekg_analyzer.c`)
- Full pipeline testing with synthetic EKG signals
- Normal rhythm, VT, and VF detection accuracy
- QRS detection accuracy verification
- Shock recommendation logic validation
- Various amplitude variations

## Running Tests

```bash
make test
```

This builds and runs both unit and integration test suites.

## What Tests Cover

**Unit Tests:**
- Individual signal processing components
- Filter frequency responses
- Edge cases and boundary conditions
- Numerical accuracy of algorithms

**Integration Tests:**
- End-to-end analysis pipeline
- Synthetic EKG signal classification
- Shock decision accuracy
- Confidence level calibration
- Performance across different rhythm types

## Interpreting Results

Tests output:
- Pass/fail status for each test case
- Expected vs actual values for numerical tests
- Error messages for failures

All tests must pass before committing changes to the algorithm.
