# Frisson · LIFE — PRD

## 1. Summary

LIFE is a modulator that adds slow, correlated, 1/f-shaped drift to the other
ingredients — EQ centres, saturation drive, width, left/right balance,
transient emphasis — so the result breathes like hardware instead of sitting
perfectly still. Class `frisson::LifeModulator`
(`plugins/Frisson/modules/Life.h/.cpp`).

## 2. The response it targets

Pitch and loudness fluctuations in music follow a 1/f spectrum (Voss & Clarke
1975): slow drift dominates, fast jitter is rare, and the brain finds that
balance of predictability and surprise most engaging. Analogue gear drifts
the same way (thermal drift, tape wow/flutter, component tolerances), which
is a large part of what people mean by "warmth" and "life". White noise
modulation sounds like a fault; perfectly static sounds like a machine.

## 3. Requirements

- R1. `setAmount(0)` returns `Modulation::identity()`; activity 0.
- R2. Six independent `ap::DriftSource`s (different seeds), updated once per
  block with the control rate = sampleRate / numSamples (recompute the
  prepare() rates if the block size changes by more than 2×; otherwise use
  the prepare-time nominal block size). Fastest row rate = 2 Hz × rate,
  `setRate(0.5..2)`, default 1.
- R3. Per block, with `d_k` the six drift values (−1..1) and `a = amount`:
  - `eqFreqMul = 2^(0.03 × a × d1)` (±2.1 % at full)
  - `driveMul = 1 + 0.10 × a × d2`
  - `widthMul = 1 + 0.05 × a × d3`
  - `gainL = dB→lin(0.2 × a × d4)`, `gainR = dB→lin(0.2 × a × d5)`
  - `transientMul = 1 + 0.10 × a × d6`
  - `airDb = 0`, `densityMul = 1`.
- R4. Deterministic for a given seed (tests rely on it); `reset()` restores
  the seeded state.
- R5. `activity()` = `|d1|` (the EQ drift), 0..1.
- R6. Does not read the audio (the `analyze` buffers are ignored) — it is a
  clock, not a listener.

## 4. Design

Trivial on top of `ap::DriftSource`. Keep an internal time accumulator so the
drift rate stays correct across varying block sizes: call each source's
`next()` once per block and set its control rate from the nominal block
length passed to `prepare()`. Document the ±2 %/±0.2 dB numbers in the header
so nobody "improves" them into audible wobble.

Public API beyond the contract: `setRate(float)`, `setSeed(uint32_t)`.

## 5. Acceptance tests (`tests/frisson/test_life.cpp`)

Run 60 s of 512-sample blocks at 48 kHz.

- T1. amount 0 → identity every block; activity 0.
- T2. amount 1: over 60 s, `eqFreqMul` stays within [0.979, 1.0215], `driveMul`
  within [0.9, 1.1], `widthMul` within [0.95, 1.05], gains within ±0.2 dB,
  `transientMul` within [0.9, 1.1]; `airDb == 0`, `densityMul == 1`.
- T3. It moves: the standard deviation of `eqFreqMul` over 60 s is > 0.004 and
  the value is not constant for more than 2 s at a time.
- T4. It is slow: autocorrelation of `eqFreqMul` at a 1 s lag > 0.35, and the
  block-to-block difference never exceeds 0.002.
- T5. L and R drift independently: correlation between `gainL` and `gainR`
  series < 0.5.
- T6. Deterministic: two instances with the same seed produce identical
  series; `reset()` reproduces the series from the start.
- T7. rate 2 vs rate 0.5: the rate-2 series has lower 1 s autocorrelation.
