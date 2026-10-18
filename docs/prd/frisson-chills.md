# Frisson · CHILLS — PRD

## 1. Summary

CHILLS is a modulator: it listens for the moments the music is building
(crescendo) or something new arrives (a sudden spectral change) and, for a few
seconds, leans the ingredients into it — more air, more width, more density —
then relaxes. It amplifies cues the music already gives. Class
`frisson::ChillsModulator` (`plugins/Frisson/modules/Chills.h/.cpp`).

## 2. The response it targets

Musical chills (frisson) are reliably triggered by sudden dynamic change, a
crescendo, a new voice or instrument entering, and unprepared harmony
(Sloboda 1991; Grewe et al. 2007; Huron 2006), and dopamine release peaks at
those moments after rising anticipation (Salimpoor et al. 2011). The module
detects the first three (it cannot read harmony) and makes the peak slightly
bigger than the build, which is the shape of the chills response itself.

## 3. Requirements

- R1. `setAmount(0)` returns `Modulation::identity()` and `activity() == 0`.
- R2. Analysis at block rate on the mono sum of the dry input:
  - Loudness: 400 ms RMS (`L_fast`, dB) and 3 s RMS (`L_slow`, dB) via one-pole
    envelopes updated per block.
  - Spectral novelty: six band-pass detectors (centres 100, 300, 1000, 3000,
    8000, 14000 Hz, Q 1) each with a 50 ms peak envelope in dB; `flux =
    Σ_bands max(0, env_now − env_300ms_ago)` (the 300 ms-ago value is from a
    small ring of block values).
- R3. Trigger conditions (thresholds scaled by sensitivity, `setSensitivity(0..1)`,
  default 0.5; `thr = base × (1.5 − sensitivity)`):
  - crescendo: `L_fast − L_slow > 3 dB` (base) **and** `L_fast` rising over the
    last 500 ms;
  - novelty: `flux > 9 dB` (base).
- R4. Envelope on trigger: attack 150 ms to 1.0, hold 1 s, release 3 s
  (exponential), refractory 4 s after a trigger (retriggers ignored). Envelope
  value `e` ∈ 0..1.
- R5. Output per block: `airDb = 3 × amount × e`, `widthMul = 1 + 0.3 × amount × e`,
  `densityMul = 1 + 0.25 × amount × e`, everything else identity.
- R6. Steady material (stationary pink, steady loop) must not trigger: `e < 0.1`
  over 20 s.
- R7. Level independent (dB differences only). Silence never triggers.
- R8. `activity() == e`.

## 4. Design

Block processing: accumulate sum of squares and per-band peak over the block,
convert to dB, update envelopes with block-length-aware coefficients (time
constants in ms, coefficient computed from `numSamples / sampleRate`). Keep a
ring of the last ~16 block values of `L_fast` and the band envelopes for the
"500 ms ago" / "300 ms ago" comparisons (ring indexed by time, not block
count, so odd block sizes work). State machine: Idle → Attack → Hold →
Release → Idle, plus a refractory timer.

Public API beyond the contract: `setSensitivity(float)`.

## 5. Acceptance tests (`tests/frisson/test_chills.cpp`)

Use 512-sample blocks at 48 kHz; feed `analyze(l, r, n)` sequentially.

- T1. amount 0 with a strong crescendo → identity modulation and activity 0.
- T2. Crescendo: pink at −30 dBFS for 4 s, then ramping linearly in dB to
  −18 dBFS over 2 s, then steady: `activity()` exceeds 0.5 at some point during
  or within 1 s after the ramp; `airDb` at that moment ≥ 1.5 at amount 1; after
  10 s steady, `activity() < 0.1`.
- T3. Novelty: pink at −24 dBFS for 5 s, then add a 12 kHz sine at −20 dBFS →
  triggers within 400 ms (activity > 0.3); with the sine present continuously
  from the start, no trigger after the first 4 s.
- T4. Steady pink at −20 dBFS for 20 s → activity never > 0.1 after the first
  3 s.
- T5. Refractory: two crescendos 2 s apart → only one trigger (activity does
  not re-rise above 0.8 within 4 s of the first trigger).
- T6. Sensitivity 1.0 triggers on a 2 dB crescendo that sensitivity 0.0 ignores.
- T7. Level independence: T2 at −50/−38 dBFS behaves the same.
- T8. Silence → identity, no NaN; odd block sizes (e.g. 37) work.
