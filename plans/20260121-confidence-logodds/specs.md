# Clinical Confidence Computation - Research Findings & Specifications

## Problem Statement

The current confidence calculation in `src/vf_detector.c` and `src/vt_detector.c` uses arbitrary additive bonuses that have no statistical basis. A "confidence" of 0.8 doesn't represent an 80% probability - it's just an accumulated score.

### Current Implementation Issues

| Issue | Location | Description |
|-------|----------|-------------|
| Arbitrary additive scoring | `vf_detector.c:366-389` | Adds fixed bonuses (0.1, 0.2) with no statistical basis |
| Fixed base confidence | `vf_detector.c:367`, `vf_detector.c:430` | Starts at 0.5 or 0.7 arbitrarily |
| Asymmetric caps | `vf_detector.c:389` vs `437` | Standard VF capped at 1.0, Fine VF at 0.95 |
| Signal quality ignored | All detectors | Noisy signals get same confidence as clean signals |
| Non-detection with confidence | `vt_detector.c:252` | Returns partial score even when `vt_detected=0` |
| Integer truncation | `ekg_analyzer.c:120,129` | `float * 100` to `uint8_t` loses precision |

## Research Findings

### What Clinical Confidence Should Represent

Clinical confidence should be **posterior probability** - the probability that a rhythm truly requires shock given observed features, computed via Bayes' theorem:

```
P(shockable | features) = P(features | shockable) × P(shockable) / P(features)
```

Using log-odds form:
```
log_odds_posterior = log_odds_prior + sum(log_likelihood_ratios)
probability = 1 / (1 + exp(-log_odds))
```

### AHA Requirements for AED Algorithms

Source: [PMC3910663](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC3910663/)

| Rhythm Type | Sensitivity | Specificity |
|-------------|-------------|-------------|
| Coarse VF | >90% | - |
| Rapid VT | >75% | - |
| Normal Sinus Rhythm | - | >99% |
| Other non-shockable | - | >95% |

### Why Likelihood Ratios

Source: [CEBM Oxford](https://www.cebm.ox.ac.uk/resources/ebm-tools/likelihood-ratios)

Likelihood ratios (LR) are preferred over raw sensitivity/specificity because:

1. **Prevalence-independent**: Unlike PPV/NPV, LRs don't depend on disease prevalence
2. **Composable**: Multiple features can be combined by multiplying LRs (or adding log-LRs)
3. **Calibrated output**: Produces true probabilities, not arbitrary scores

Interpretation:
- LR > 10: Strong evidence to rule IN
- LR < 0.1: Strong evidence to rule OUT
- LR = 1: No diagnostic value

### Bayesian Approach in Medical Devices

Source: [FDA Guidance](https://www.fda.gov/regulatory-information/search-fda-guidance-documents/statistical-guidance-reporting-results-studies-evaluating-diagnostic-tests-guidance-industry-and-fda), [PMC9984131](https://pmc.ncbi.nlm.nih.gov/articles/PMC9984131/)

FDA guidance (2019) supports Bayesian approaches for medical device benefit-risk assessments:
- Posterior probabilities for trial success (vs frequentist p-values)
- Bayesian credible intervals (vs frequentist confidence intervals)
- Borrowing information from past studies via hierarchical models

## Specification: Correct Confidence Calculation

### Definition

**Confidence** = P(rhythm is truly shockable | observed signal features) × signal_quality_factor

### Mathematical Framework

```
// 1. Start with prior odds (neutral: 50% = 1:1 odds, log-odds = 0)
log_odds = 0.0

// 2. For each diagnostic feature, add log-likelihood-ratio
for each feature:
    if feature_present:
        log_odds += log(LR_positive)
    else:
        log_odds += log(LR_negative)

// 3. Convert to probability
posterior = 1 / (1 + exp(-log_odds))

// 4. Scale by signal quality
confidence = posterior × signal_quality
```

### Likelihood Ratio Estimates

Based on published AED accuracy studies and physiological principles:

#### VF Detection Features

| Feature | LR+ (present) | LR- (absent) | log(LR+) | log(LR-) |
|---------|---------------|--------------|----------|----------|
| Frequency 3-12 Hz (basic) | 3.0 | 0.2 | +1.10 | -1.61 |
| Frequency 5-8 Hz (optimal) | 5.0 | 0.5 | +1.61 | -0.69 |
| High ZCR (>threshold) | 2.5 | 0.4 | +0.92 | -0.92 |
| High TCR (>2×threshold) | 2.0 | 0.5 | +0.69 | -0.69 |
| QRS absent | 4.0 | 0.1 | +1.39 | -2.30 |
| Irregular (not organized) | 3.0 | 0.3 | +1.10 | -1.20 |

#### VT Detection Features

| Feature | LR+ (present) | LR- (absent) | log(LR+) | log(LR-) |
|---------|---------------|--------------|----------|----------|
| HR > threshold | 3.0 | 0.3 | +1.10 | -1.20 |
| HR > 150 bpm | 2.0 | 0.7 | +0.69 | -0.36 |
| QRS width > threshold | 4.0 | 0.2 | +1.39 | -1.61 |
| QRS width > 140ms | 2.5 | 0.6 | +0.92 | -0.51 |
| Regular (low RR CV) | 3.0 | 0.4 | +1.10 | -0.92 |
| Very regular (CV < 10%) | 2.0 | 0.7 | +0.69 | -0.36 |
| Sustained duration | 2.5 | 0.5 | +0.92 | -0.69 |

#### Fine VF Adjustments

Fine VF inherently has lower certainty due to low amplitude. Apply a penalty:
- Base log-odds adjustment: -0.5 (reduces confidence by ~12% at midpoint)

### Signal Quality Factor

Signal quality should scale confidence based on noise/artifact level:

```
signal_quality = clamp(SNR / SNR_GOOD, 0.5, 1.0)
```

Where:
- `SNR_GOOD` = 10.0 (threshold for "clean" signal)
- Minimum quality factor = 0.5 (never reduce confidence by more than 50%)
- Maximum = 1.0 (no penalty for clean signals)

SNR estimation:
```
SNR = signal_power / noise_power
    = variance(signal) / variance(high_freq_residual)
```

### Output Constraints

- Confidence range: 0.0 to 1.0 (no artificial caps)
- Non-detection: When rhythm not detected, confidence represents P(shockable) which should be low
- Integer conversion: `round(confidence * 100)` not truncate

## Benefits of This Approach

| Aspect | Current | Proposed |
|--------|---------|----------|
| Confidence meaning | Arbitrary score | True probability |
| Statistical basis | None | Bayesian inference |
| Signal quality | Ignored | Explicitly modeled |
| Caps | Arbitrary (0.95, 1.0) | Natural from math |
| Tuning | Ad-hoc constants | LRs from data/literature |
| Calibration | Unknown | Verifiable via calibration plots |

## Validation Approach

1. **Calibration plot**: For predicted probabilities in bins (0.6-0.7, 0.7-0.8, etc.), actual positive rate should match
2. **ROC curve**: Area under curve should meet AHA requirements
3. **Per-rhythm evaluation**: Sensitivity/specificity for each rhythm class

## References

1. [Likelihood Ratios - CEBM Oxford](https://www.cebm.ox.ac.uk/resources/ebm-tools/likelihood-ratios)
2. [Rhythm Analysis during CPR - PMC3910663](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC3910663/)
3. [FDA Statistical Guidance on Diagnostic Tests](https://www.fda.gov/regulatory-information/search-fda-guidance-documents/statistical-guidance-reporting-results-studies-evaluating-diagnostic-tests-guidance-industry-and-fda)
4. [Bayesian Statistics for Medical Devices - PMC9984131](https://pmc.ncbi.nlm.nih.gov/articles/PMC9984131/)
5. [Bayes' formula in medical decision-making - PMC7808025](https://pmc.ncbi.nlm.nih.gov/articles/PMC7808025/)
6. [Diagnostic tests: likelihood ratios - PMC478236](https://pmc.ncbi.nlm.nih.gov/articles/PMC478236/)
