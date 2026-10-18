# Frisson · WARMTH — PRD

## 1. Summary

WARMTH adds a consonant harmonic series and gentle fullness to the low-mid
band so sources sound "expensive" and comfortable rather than thin. Module
class `frisson::WarmthModule` (`plugins/Frisson/modules/Warmth.h/.cpp`).

## 2. The response it targets

Partials that line up as a harmonic series are heard as consonant and
pleasant; partials that beat inside a critical band are heard as rough
(Plomp & Levelt 1965). Even-order harmonics at low level reinforce the
series and are the classic "tube warmth". Fullness at 150–250 Hz reads as body
without the boxiness of 300–500 Hz. Light compression on that band keeps it
from poking out, which the ear reads as "sitting right".

## 3. Requirements

- R1. `setAmount(0)` is an exact bypass.
- R2. Operates on the 80–800 Hz band only (3-band LR4 split at 80 and 800 Hz);
  the bands outside pass through phase-coherently (via the splitter's
  all-pass compensation) and unchanged in magnitude.
- R3. Saturation on the band: `ap::TubeShaper` with bias 0.5 and
  `drive = 1 + 2 × amount × driveMul` (driveMul from Modulation, clamped
  0.5–2), followed by `ap::DcBlocker`. 2nd harmonic must exceed 3rd for all
  amounts on a 200 Hz sine at −6 dBFS.
- R4. Fullness: peak EQ at 200 Hz × eqFreqMul (clamped 0.8–1.25), Q 0.9,
  gain 0 to +1.5 dB × amount, applied to the band.
- R5. Glue: RMS compressor on the band, detector 30 ms / 150 ms, threshold set
  relative to the band's 3 s running RMS (threshold = long-term − 3 dB),
  ratio from 1:1 (amount 0) to 2:1 (amount 1), knee 6 dB, automatic makeup
  so the band's long-term RMS is unchanged within ±1 dB.
- R6. Band level after saturation is compensated so the band's RMS stays
  within ±1 dB of its input at any amount (measure the small-signal gain of
  the shaper and divide it out).
- R7. No latency. Level independent (R5 uses a relative threshold).
- R8. `activity()` = glue gain reduction normalised (0 at 0 dB, 1 at 6 dB).
- R9. Smoothed amount/modulation changes.

## 4. Design

```
in ──► ThreeBandSplitter(80, 800) ──► low ─────────────────────────────┐
                                  ├─► mid ─► Tube(bias .5, drive) ─► DC ─► Peak(200 Hz) ─► Glue ─►(+)─► out
                                  └─► high ────────────────────────────┘
```

Per channel: own splitter, shaper state, DC blocker, peak biquad; the glue
detector reads the mono sum of the band so stereo stays stable. Band
compensation gain = 1 / (shaper small-signal gain is already 1 by
construction; measure RMS in/out over the block and apply a slow (300 ms)
correction clamped to ±3 dB) — or simpler: trust the normalised shaper and
only correct the compressor makeup. Either way T4 must pass.

Public API beyond the contract: none.

## 5. Acceptance tests (`tests/frisson/test_warmth.cpp`)

- T1. amount 0: stereo pink → `maxDiff == 0`.
- T2. 200 Hz sine at −6 dBFS, amount 1: Goertzel 400 Hz ≥ −35 dB re 200 Hz;
  400 Hz level > 600 Hz level. At amount 0.3 the 400 Hz level is lower than at 1.
- T3. DC: mean of output < 1e-3 on the 200 Hz sine, amount 1.
- T4. Level: band RMS (80–800 Hz, measured with `bandRms` on 2 s of pink)
  within ±1.5 dB of the input at amount 0.5 and 1 (after a 1 s settle).
- T5. Bands outside 80–800: pink through at amount 1 → `bandRms` 30–60 Hz and
  1.5–10 kHz within ±0.3 dB of input.
- T6. Glue works: pink with a 10 dB louder 500 ms section in the middle →
  with amount 1 the loud/quiet band-RMS ratio is smaller than at amount 0 by
  ≥ 1.5 dB. `activity()` > 0.2 during the loud section, < 0.05 in the quiet
  tail.
- T7. driveMul 2 via `setModulation` increases the 400 Hz harmonic vs driveMul 1.
- T8. `latencySamples() == 0`; all outputs finite.
