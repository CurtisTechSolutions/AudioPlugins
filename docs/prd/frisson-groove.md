# Frisson · GROOVE — PRD

## 1. Summary

GROOVE tightens the feel: attacks get sharper, sustain gets a little leaner,
and — when the host provides tempo — hits that land on the grid get a touch
more snap than the ones in between. Module class `frisson::GrooveModule`
(`plugins/Frisson/modules/Groove.h/.cpp`).

## 2. The response it targets

Beat perception and the urge to move are driven by onsets; sharper, more
consistent onsets make the pulse easier to lock onto. Pleasure from groove
peaks with a predictable pulse decorated by moderate syncopation (Witek et al.
2014) — a processor cannot add syncopation, but it can make the pulse clearer
without flattening what is between the beats (the "pocket" option emphasises
grid-aligned hits more and off-grid hits slightly less, which the listener
hears as "tighter" rather than "quantised").

## 3. Requirements

- R1. `setAmount(0)` is an exact bypass.
- R2. Transient detector: `ap::TransientDetector` on the mono sum with default
  params. Attack gain in dB = `6 × amount × transientMul × attackAmount()`
  (transientMul from Modulation, clamped 0.5–2); sustain gain in dB =
  `−3 × amount × sustainAmount()`. Applied as a wideband gain per sample
  (same gain to both channels).
- R3. "Snap": the attack gain additionally tilts up above 2 kHz: a high shelf
  at 2 kHz whose gain is `0.5 × attackGainDb`, so onsets get more edge than
  body. Shelf coefficients updated every 32 samples with the gain smoothed.
- R4. Pocket (default on; `setPocketEnabled(bool)`): when `Transport.hasTempo &&
  playing`, every detected onset gets a weight: 1.3 if within ±25 ms of a
  1/16-note grid line, 0.8 otherwise; the weight scales that onset's attack
  gain until the next onset. Without tempo, weight is 1.
- R5. No latency. Gains smoothed (attack 0.5 ms, release ~20 ms inside the
  detector; the applied gain uses a 1 ms one-pole so the first sample of a
  hit is not missed).
- R6. `activity()` = current attack emphasis normalised (attackGainDb / 6).
- R7. A steady signal (no onsets) is unchanged within 0.2 dB at any amount.

## 4. Design

Per block: read Transport; per sample: detector on `0.5(L+R)`; compute
attackDb/sustainDb; wideband gain = dB→lin(attackDb×weight + sustainDb);
apply; shelf gain target = 0.5×attackDb×weight; every 32 samples recompute
the shelf biquads (one per channel) from the smoothed shelf gain.

Grid alignment: `ppq_now = ppqPosition + i × bpm / (60 × sr)`; distance to the
nearest multiple of 0.25 beats in seconds = `|ppq_now − round(ppq_now/0.25)×0.25| × 60 / bpm`.

Public API beyond the contract: `void setPocketEnabled (bool)`.

## 5. Acceptance tests (`tests/frisson/test_groove.cpp`)

- T1. amount 0: stereo pink → `maxDiff == 0`.
- T2. Steady 440 Hz sine, amount 1 → RMS after 0.5 s within 0.2 dB of input.
- T3. Click train (20 ms decaying 1 kHz bursts every 250 ms on silence),
  amount 1: peak of each burst ≥ 1.5 dB higher than input; RMS of the burst
  tail (10–20 ms into the burst) lower than input (sustain reduction); 
  `activity()` reaches > 0.3 on bursts.
- T4. Pocket: with a Transport (120 bpm, playing, hasTempo) and bursts placed
  exactly on 1/16 grid lines vs bursts placed 60 ms off-grid, the on-grid
  bursts get a larger peak gain than the off-grid ones (ratio ≥ 1.2 in dB
  terms); with pocket disabled the two are equal within 0.1 dB.
- T5. Snap: for a burst, energy above 4 kHz rises more (in dB) than energy
  below 500 Hz.
- T6. `latencySamples() == 0`; finite outputs; silence in → silence out.
