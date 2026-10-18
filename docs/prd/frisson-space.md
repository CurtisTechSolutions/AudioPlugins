# Frisson · SPACE — PRD

## 1. Summary

SPACE makes the listener feel inside the sound: stereo width above the bass,
a brighter side image, and a touch of early reflection for envelopment,
without breaking mono compatibility. Module class `frisson::SpaceModule`
(`plugins/Frisson/modules/Space.h/.cpp`).

## 2. The response it targets

Listener envelopment and apparent source width are among the strongest
predictors of preference in concert-hall research (Beranek). Width above
~300 Hz adds spaciousness; width below it adds nothing audible, costs headroom
and cancels in mono. Short, sparse, decorrelated early reflections (10–25 ms)
give a sense of a room without audible reverb. CHILLS widens through
`Modulation.widthMul`.

## 3. Requirements

- R1. `setAmount(0)` is an exact bypass (widthMul and room ignored).
- R2. Width: M/S encode; `S_high = LR4_HP(S, monoBelowHz)`; `S_low = S − S_high`;
  `width = 1 + 0.8 × amount × widthMul` (widthMul clamped 0.5–2, total width
  clamped ≤ 2.2); `S' = S_low + width × S_high`; decode.
- R3. Side brightness: high shelf on `S_high` at 8 kHz, Q 0.707, +2 dB × amount.
- R4. Room (`setRoom(0..1)`, default 0.3): four taps per channel from the
  *opposite-side-biased* mono input: L gets taps at 11.3 and 17.9 ms, R at
  13.7 and 23.1 ms, each tap level `−18 dB × amount × room`, a 6 dB high shelf
  cut at 5 kHz on the tap signal, tap 2 polarity inverted. Decorrelated by
  construction (different delays per side).
- R5. Mono compatibility: for any input, `RMS(L+R)` output within ±1 dB of
  `RMS(L+R)` input (width does not touch M; room taps are low level).
- R6. Dual-mono input with room 0 passes unchanged (S is zero).
- R7. No latency (the taps are delays inside the module, the direct path is not delayed).
- R8. `activity()` = (width − 1) / 1.2, clamped 0..1.
- R9. `setMonoBelowHz(60..300)`, default 120.

## 4. Design

M/S path with one LR4 high-pass on S (band split must be complementary:
`S_low = S − S_high` so the sum is exact). Room: one `ap::DelayLine` per channel
fed with the mono input M, reads at two fixed delays, shelved, summed into the
output channels with the levels above.

Public API beyond the contract: `setMonoBelowHz(float)`, `setRoom(float)`.

## 5. Acceptance tests (`tests/frisson/test_space.cpp`)

- T1. amount 0: stereo pink → `maxDiff == 0` (room 0.3, widthMul 2 set).
- T2. Dual-mono pink, room 0, amount 1 → `maxDiff < 1e-5`.
- T3. Decorrelated stereo pink, room 0, amount 1: side energy (`bandRms` of
  L−R) 1–8 kHz up by +4.5 ± 1 dB vs input; side 30–80 Hz within 0.5 dB;
  `RMS(L+R)` within 0.3 dB.
- T4. widthMul 1.5 raises side 1–8 kHz further than widthMul 1.
- T5. Room: dual-mono click (single sample impulse) with room 1, amount 1 →
  output has non-zero samples at 11.3 ms (L) and 13.7 ms (R) ± 1 sample,
  each ≤ −17 dB re the impulse; correlation(L,R) of the tail < 0.9.
- T6. Mono compatibility: stereo pink, room 1, amount 1 → `RMS(L+R)` within 1 dB.
- T7. monoBelowHz 300 keeps the side at 200 Hz within 0.5 dB of input while
  the side at 1 kHz is boosted.
- T8. `latencySamples() == 0`; finite.
