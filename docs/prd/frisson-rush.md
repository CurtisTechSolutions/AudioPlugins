# Frisson · RUSH — PRD

## 1. Summary

RUSH is density and arousal without cheating on loudness: three-band parallel
compression for sustain and glue, a lookahead limiter at the end of the chain,
and automatic gain compensation so turning it up changes how *dense* the sound
is, not how loud. Module class `frisson::RushModule`
(`plugins/Frisson/modules/Rush.h/.cpp`). RUSH is the last ingredient in the
chain and owns the chain's latency.

## 2. The response it targets

Louder sounds better (equal-loudness contours), which is why every enhancer
secretly raises level. Dense, sustained, consistent sound is read as "big" and
"energetic" and raises arousal even at matched loudness. Parallel ("New York")
compression gives that density while keeping transients — the groove — intact.
CHILLS pushes density momentarily via `Modulation.densityMul`.

## 3. Requirements

- R1. `setAmount(0)` is a bypass *apart from* the constant lookahead delay:
  output == input delayed by `latencySamples()`, bitwise.
- R2. Latency is constant: `latencySamples() = round(1.5 ms × sr)`, independent
  of amount and settings.
- R3. Three bands via `ap::ThreeBandSplitter` at 120 Hz and 2.5 kHz, per channel.
- R4. Per band, parallel compression: `comp = Compress(band)` with an RMS
  detector (10 ms / 120 ms on the mono-sum band), ratio 4:1, knee 6 dB,
  threshold = band long-term RMS (3 s one-pole) − 6 dB, makeup so the
  compressed path's long-term RMS matches the band's long-term RMS (clamped
  to +12 dB); `band_out = band + mix × comp` where
  `mix = amount × densityMul` (densityMul clamped 0.5–2, mix clamped 0..1.5).
- R5. Limiter on the sum: lookahead 1.5 ms (the module's latency), peak
  detector with the lookahead so the ceiling is never exceeded, release 60 ms,
  ceiling `setCeilingDb(−3..0)` default −0.3 dBFS. The limiter is active at
  any amount > 0 (ceiling applies even at low amounts).
- R6. Auto gain (`setAutoGain(bool)`, default on): a slow (3 s) loudness
  match that trims the output so the processed short-term RMS equals the
  input's, clamped to −12..0 dB. Must not react to short transients (3 s).
- R7. Level independent (thresholds are relative to running band levels).
- R8. `activity()` = max(limiter gain reduction, mean band GR) / 12 dB.
- R9. No allocations after prepare; all smoothing inside.

## 4. Design

```
in ─► delay(1.5 ms) ─────────────────────────────────────────────┐
in ─► split(120, 2.5k) ─► per band: [ band + mix·comp(band) ] ─► Σ ─► autoGain ─► limiter(lookahead from the delayed path) ─► out
```

Implementation hint: run the band processing on the *delayed* signal and use
the un-delayed signal for the limiter's lookahead detector; that way the
limiter sees 1.5 ms into the future and the whole module has one delay.
Detectors read the mono sum; gains apply equally to L/R.

Public API beyond the contract: `setCeilingDb(float)`, `setAutoGain(bool)`.

## 5. Acceptance tests (`tests/frisson/test_rush.cpp`)

- T1. amount 0: stereo pink → output equals input shifted by `latencySamples()`
  exactly (`maxDiff == 0` after aligning), latency == round(0.0015 × 48000) = 72.
- T2. Ceiling: 1 kHz sine at +3 dBFS peak, amount 0.5, ceiling −0.3 → output
  peak ≤ −0.3 dBFS + 0.05 dB after the first block.
- T3. Density: pink with alternating 400 ms sections at −12 and −24 dBFS,
  amount 1, autoGain off → the quiet/loud RMS ratio (in dB) shrinks by ≥ 3 dB
  compared with amount 0 (measured after 2 s of settling, excluding 50 ms
  around each step).
- T4. Auto gain: pink at −20 dBFS, amount 1, autoGain on → after 6 s the
  output's 1 s RMS is within ±1 dB of the input's. With autoGain off it is
  higher.
- T5. Transients survive: click train through amount 1 → each click's peak is
  within −3 dB of the amount-0 peak (parallel, not serial, compression).
- T6. densityMul 2 vs 1 at amount 0.5: more compression of the quiet sections.
- T7. Level independence: T3 at −40/−52 dBFS gives the same ratio change ±1 dB.
- T8. finite outputs; silence → silence; `activity()` in 0..1.
