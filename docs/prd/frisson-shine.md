# Frisson · SHINE — PRD

## 1. Summary

SHINE adds the top-end sheen that reads as "mastered": a harmonic exciter
above 6 kHz plus an air shelf. Module class `frisson::ShineModule`
(`plugins/Frisson/modules/Shine.h/.cpp`).

## 2. The response it targets

The ear is least sensitive above ~10 kHz (ISO 226), so energy there can be
added generously before it hurts, and listeners associate it with expensive
playback and professional masters. Exciters (Aphex Aural Exciter) generate new
harmonics from existing high content, which restores "detail" to dull sources
in a way EQ cannot (EQ can only boost what is there). CHILLS leans on this knob
via `Modulation.airDb`.

## 3. Requirements

- R1. `setAmount(0)` is an exact bypass (airDb is ignored when amount is 0).
- R2. Exciter path: `e = LR4_HP(in, 6 kHz)` → `ap::ExciterShaper` with
  `drive = 1 + 3 × amount` → `LR4_HP(·, 8 kHz)` (removes the DC and the
  intermodulation below) → mixed into the output at
  `−12 dB + 6 dB × amount` relative gain (i.e. −12 dB at amount 0⁺ ramping to
  −6 dB at amount 1), times amount.
- R3. Air shelf: high shelf at 11 kHz × eqFreqMul (clamped 0.8–1.25), Q 0.707,
  gain `2.5 dB × amount + clamp(airDb, 0, 4) × amount`.
- R4. Content below 3 kHz unchanged within 0.2 dB at any amount.
- R5. No latency; smoothed changes; coefficient updates at most every
  32 samples.
- R6. `activity()` = exciter output level relative to input high band,
  normalised to 0..1 (0 when the exciter path is silent).

## 4. Design

Per channel: two LR4 high-passes, shaper, air shelf biquad. The exciter level
follower (for activity) reads the mono sum of the exciter path.

Public API beyond the contract: none.

## 5. Acceptance tests (`tests/frisson/test_shine.cpp`)

- T1. amount 0: stereo pink → `maxDiff == 0`; also with airDb 4 set.
- T2. 7 kHz sine at −12 dBFS, amount 1 → Goertzel 14 kHz ≥ −40 dB re 7 kHz
  (exciter harmonic); at amount 0.3 the 14 kHz level is lower.
- T3. Air: 12 kHz sine, amount 1 → +2.5 ± 0.5 dB; with `Modulation.airDb = 4`
  → ≥ +5.5 dB; airDb 10 is clamped (≤ +7 dB).
- T4. 1 kHz sine, amount 1 → within 0.2 dB. Pink: `bandRms` 100–3000 Hz within
  0.2 dB of input at amount 1.
- T5. No low intermod: 7 kHz + 7.5 kHz sines, amount 1 → Goertzel at 500 Hz
  ≤ −60 dBFS.
- T6. eqFreqMul 0.8 lowers the shelf corner: a 9 kHz sine gets more boost than
  at eqFreqMul 1.0.
- T7. `latencySamples() == 0`; finite; silence → silence.
