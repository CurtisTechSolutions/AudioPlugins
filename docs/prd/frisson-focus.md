# Frisson · FOCUS — PRD

## 1. Summary

FOCUS pulls attention to the important part of the sound: it clears low-mid
fog, lifts presence, and guards against the harshness and sibilance that make
listeners fatigue. Module class `frisson::FocusModule`
(`plugins/Frisson/modules/Focus.h/.cpp`).

## 2. The response it targets

The ear canal resonates around 3–4 kHz, making it the most sensitive region
(ISO 226); a little energy at 2–2.5 kHz reads as "forward" and intelligible,
too much at 3–4 kHz reads as harsh and causes fatigue. 250–450 Hz build-up is
heard as "mud" and masks everything above it. Sibilance (6–8 kHz) is the other
classic fatigue trigger. Clarity is perceived as effortless attention.

## 3. Requirements

- R1. `setAmount(0)` is an exact bypass.
- R2. Static shape, all scaled by amount and frequency-scaled by eqFreqMul
  (clamped 0.8–1.25):
  - peak cut at 330 Hz, Q 1.0, 0 to −3 dB;
  - peak lift at 2.2 kHz, Q 1.2, 0 to +2 dB.
- R3. Dynamic harshness guard: detector = band-pass 3–4 kHz on the mono sum,
  peak envelope 2 ms / 80 ms, compared with the wideband envelope (same
  times). Ratio `r = bandEnv / (wideEnv + eps)`. When `r > r0` (r0 = 0.30)
  apply a peak cut at 3.5 kHz, Q 1.5, depth `min(4 dB, 4 dB × amount × (r − r0)/r0)`.
- R4. Dynamic sibilance guard: same structure for 6–8 kHz, r0 = 0.25, cut at
  7 kHz, Q 2, max 4 dB × amount.
- R5. Pink noise must not trigger either guard (cut < 0.3 dB over 2 s).
- R6. Guards are level independent (ratios only).
- R7. No latency. `activity()` = max(dynamic cuts) / 4 dB.
- R8. Coefficient updates for the dynamic cuts at most every 32 samples, with
  the depth smoothed, so there are no zipper artefacts.

## 4. Design

Per channel: four biquads in series (mud cut, presence, harsh cut, sibilance
cut); the dynamic ones recomputed every 32 samples from the shared detector
(mono sum). Static coefficients recomputed only when amount/eqFreqMul change
(compare to last applied value; smoother on amount).

Public API beyond the contract: none.

## 5. Acceptance tests (`tests/frisson/test_focus.cpp`)

- T1. amount 0: stereo pink → `maxDiff == 0`.
- T2. Static: 330 Hz sine, amount 1 → −3 ± 0.4 dB; 2.2 kHz sine → +2 ± 0.4 dB;
  1 kHz → within 0.3 dB; 10 kHz → within 0.3 dB.
- T3. Harsh guard: pink at −20 dBFS plus a 3.5 kHz sine at −14 dBFS, amount 1
  → the 3.5 kHz Goertzel level drops ≥ 2 dB vs the same signal at amount 0;
  with pink alone the 3.5 kHz band changes < 0.3 dB. `activity()` > 0.3 during
  the harsh signal.
- T4. Sibilance guard: pink plus a 7 kHz sine at −14 dBFS → 7 kHz level drops
  ≥ 2 dB; pink alone unchanged within 0.3 dB at 7 kHz.
- T5. Level independence: T3 repeated at −40 dBFS overall gives the same cut
  within 0.5 dB.
- T6. eqFreqMul 1.2 moves the mud cut: a 330 Hz sine is cut less than with
  eqFreqMul 1.0, and a 396 Hz sine is cut more.
- T7. `latencySamples() == 0`; outputs finite; no NaN with silence input.
