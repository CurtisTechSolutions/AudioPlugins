# Frisson · WEIGHT — PRD

## 1. Summary

WEIGHT makes the low end feel physical ("in the chest") and makes it translate
to small speakers, without adding mud. Module class `frisson::WeightModule`
(`plugins/Frisson/modules/Weight.h/.cpp`).

## 2. The response it targets

Felt bass (20–90 Hz) is perceived as much through the body as the ears; it is
the band most associated with "big" and "powerful". Small speakers cannot
reproduce it, but the auditory system reconstructs a missing fundamental from
its harmonics (the "missing fundamental" effect), which is how psychoacoustic
bass enhancers (MaxxBass et al.) make phones sound bassy. Wide stereo bass
smears the sense of weight and causes mono-sum cancellation, so bass is kept
centred.

## 3. Requirements

- R1. `setAmount(0)` is an exact bypass (output == input, bitwise).
- R2. Clean sub lift: low shelf, corner 70 Hz, 0 to +4 dB scaled by amount.
- R3. Psychoacoustic bass: harmonics (2nd and 3rd) of the 40–90 Hz content,
  band-limited to 80–250 Hz, mixed in at −12 dB (amount 1) relative to the
  band they came from; level tracks the sub level so quiet passages do not
  get a constant hum.
- R4. Mono below `monoBelowHz` (default 120 Hz, settable 60–300): the side
  channel is high-passed with an LR4 at that frequency. Reaches full depth by
  amount 0.25 so bass is centred at ordinary knob settings.
- R5. No added latency. No DC on the output (harmonic generator is DC-blocked).
- R6. Level independent (R3's level tracking uses a ratio, not a threshold).
- R7. `activity()` = normalised level of the generated harmonics (0..1).
- R8. Smooth parameter changes: amount and monoBelowHz are smoothed (≥ 20 ms).

## 4. Design

1. M/S encode. `S_high = LR4_HP(S, monoBelowHz)`; `S = S_high` (bass side discarded).
   Decode back to L/R.
2. Per channel: `shelf = LowShelf(gain = 4 dB × amount)` whose rise *starts* at 70 Hz (biquad centre
   placed so 60 Hz gets ≈ +3 dB at full amount, 150 Hz ≤ +0.3 dB).
3. Harmonic generator on the mono sum M:
   `b = BPF(M, 40–90 Hz)` (two cascaded 2nd-order band-passes or LR4 pair);
   `env = EnvelopeFollower(peak, 5 ms / 80 ms)(b)`;
   `n = b / max(env, 1e-4)` (normalised ±1-ish);
   `h = 0.7·T2(n) + 0.3·T3(n)` with Chebyshev `T2(n) = 2n² − 1`, `T3(n) = 4n³ − 3n`
   (pure 2nd and 3rd harmonics of a unit sine; note `n·|n|` is odd and makes *no* 2nd harmonic), DC-blocked;
   `h = BPF(h, 80–250 Hz)` (LR4 pair);
   `out += h × env × amount × 10^(−12/20)` summed equally into L and R.
4. Output trim: none (shelf gain is the intended level change).

Public API beyond the contract: `void setMonoBelowHz (float hz)`.

## 5. Acceptance tests (`tests/frisson/test_weight.cpp`)

- T1. amount 0: dual-mono pink through → `maxDiff == 0`.
- T2. 50 Hz sine (amp 0.3), amount 1: Goertzel at 100 Hz ≥ −30 dB re the 50 Hz
  component, and at 150 Hz ≥ −40 dB; at amount 0.5 the 100 Hz level is lower
  than at amount 1.
- T3. Sub lift: 60 Hz sine, amount 1 → level +3.5 ± 0.8 dB vs amount 0
  (shelf at 70 Hz is ~+3.5 dB at 60 Hz once harmonics are excluded; measure the
  60 Hz Goertzel bin, not RMS).
- T4. Stereo wide bass (L = 60 Hz sine, R = −L): amount 1 → side energy below
  100 Hz down ≥ 20 dB; mono sum unchanged within 0.5 dB.
- T5. Mono-below frequency 60 vs 200 Hz changes the side content at 150 Hz
  (side 150 Hz sine survives at 60, attenuated ≥ 12 dB at 200).
- T6. 1 kHz sine, amount 1 → unchanged within 0.1 dB (no mid-band effect).
- T7. Output mean (DC) < 1e-3 at amount 1 on a 50 Hz sine; all finite.
- T8. `latencySamples() == 0`.

## 6. Out of scope

Dynamic sub control (sidechain), sub synthesis from a pitch tracker, oversampling.
