# Ducker — PRD

Status: shipped v0.1 (VST3, Windows/macOS/Linux)

## 1. Summary

Ducker is a ducking compressor with three modes. **External sidechain** is
the classic kick→bass duck, with lookahead and a sidechain low cut.
**Internal** is a plain peak compressor keyed from its own input. **Ghost
(tempo)** is a tempo-synced gain envelope that pumps on every note division
from the host transport, with no sidechain routing at all. "Ducker" is a
**working title (placeholder)**: rename it via `PRODUCT_NAME` in
`plugins/Ducker/CMakeLists.txt` and the editor title, and keep
`PLUGIN_CODE Duck`. Format: VST3 Fx|Dynamics; main bus mono or stereo (input
and output must match), an optional "Sidechain" input bus (mono or stereo),
no MIDI. Code: `plugins/Ducker/` on `core/include/ap/Dynamics.h`
(EnvelopeFollower, GainComputer), `Tempo.h` (NoteDivision, GhostDuckEnvelope)
and `Filters.h`.

## 2. Who it is for and why

Electronic, hip-hop and pop producers who want bass, pads or a bus to breathe
with the kick. Sidechain routing is where beginners get stuck, and it works
differently in every DAW. Ghost mode removes it: pick a division and a depth,
and the pump locks to the grid even with the kick muted. External mode covers
the cases where the duck has to follow a real performance, such as
voice-over-music for podcasts and streams.

## 3. Requirements

- R1. External mode: the detector takes the per-sample max |x| over the
  sidechain channels, after a 12 dB/oct HP at `sc_hpf`. That feeds a peak
  envelope (`attack`, `release`) and a soft-knee gain computer (`threshold`,
  `ratio`, `knee`).
- R2. Internal mode is the same detector fed from the main input.
- R3. **Sidechain availability depends on host routing.** The bus is
  declared stereo and enabled by default, but only the host decides what
  reaches it.
  - The header shows "Sidechain input connected" (green dot) when the host
    reports the bus enabled with more than 0 channels. Otherwise it shows
    "No sidechain input (host routing)".
  - If the bus is disabled, External falls back to keying from the main input
    and behaves like Internal.
  - If the bus is enabled but nothing is routed, it carries silence: no
    ducking, while the dot can still be green.
  - The README covers routing in each DAW.
- R4. Ghost mode: gain = `GhostDuckEnvelope::gainAtBeat(ppq, bpm, ghost_div,
  ghost_offset)`, restarting on every division boundary.
  - It ramps linearly from unity to −`ghost_depth` dB over `ghost_attack`,
    holds for `ghost_hold`, then recovers over `ghost_release`.
  - `ghost_curve` blends the recovery from linear to (1−x)^2.5.
  - If attack + hold + release is longer than the division, it never fully
    recovers.
- R5. Ghost timing:
  - Transport playing: host PPQ and BPM per sample, so the pump is
    beat-locked through loops and jumps.
  - Transport stopped: phase free-runs at the host BPM when the host reports
    one, and at `ghost_bpm` otherwise.
  - The detector and the sidechain are ignored in this mode.
- R6. Lookahead (0–10 ms) delays the main path, not the detector, in every
  mode. Latency = round(ms·fs/1000) samples, re-published whenever it changes
  (240 at 5 ms and 48 kHz). Some hosts only apply a new latency after the
  transport restarts.
- R7. Gain is smoothed by a 0.5 ms one-pole; mix and makeup by 20 ms each.
  out = delayed × (1 + (gain·makeup − 1)·mix). So Mix 0 % is a bypass with
  latency only, and makeup scales with mix.
- R8. SC Listen outputs the raw sidechain (no low cut, no delay). It applies
  only in External mode with the bus enabled. A mono sidechain is repeated on
  both output channels.
- R9. Gain is unity when nothing triggers a duck. Ghost Depth 0 dB gives a
  flat line. Output is finite for any input.
- R10. The UI shows the block's maximum gain reduction and the sidechain
  status. Sidechain peak level is published but not drawn.
- R11. Gain is linked across both channels. No allocation after
  `prepareToPlay`. State is APVTS XML with root `DUCKER`. Parameter IDs and
  names are frozen.
- R12. Passes `host_Ducker` and pluginval strictness 10 in CI on all three OSes.
- R13. **Factory presets.** 27 presets in three categories: Sidechain (kick
  pump, bass, snare and hat clearance, voice-over and podcast beds), Internal
  (bus glue, drum smash, levelling, transient taming) and Ghost (tempo-synced
  pumps from 1/1 to 1/16, triplet and dotted, offbeat). Factory presets
  (`Presets.cpp`, through `ap::plug::Programs`) are the host program list.
  Program 0 is "Default" (every parameter at its default, the state a new
  instance opens in); loading any preset first returns every parameter to its
  default, so nothing carries over. The current program number is saved in the
  state (attribute `program`). The editor's preset strip steps through the
  bank or picks from a menu grouped by category, and marks the name with `*`
  once a parameter differs from the preset. The order is frozen once released:
  new presets are appended. `host_Ducker` loads every program and checks it
  changes parameters and plays finite audio.

## 4. Design

```
SC ───► HP(sc_hpf) ─┐ External + bus enabled
main ─► HP(sc_hpf) ─┴─► max|x| ─► EnvFollower(peak) ─► GainComputer ─┐
host ppq/bpm or free-run ─► GhostDuckEnvelope(div, offset, shape) ───┤ Ghost
                                                                     ▼ smooth 0.5 ms
main ─► lookahead delay (≤ 10 ms) ──────────────► × (1 + (g·makeup − 1)·mix) ─► out  (or SC Listen)
```

`updateParams()` runs once per block. Gain is computed once per sample and
applied to both channels.

### 4.1 Parameters (19)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `mode` | Mode | External sidechain / Internal / Ghost (tempo) | External sidechain | |
| `threshold` | Threshold | −60 – 0 dB | −24 | External / Internal |
| `ratio` | Ratio | 1–30 :1 | 6 | skew centred at 6:1 |
| `knee` | Knee | 0–18 dB | 6 | |
| `attack` | Attack | 0.1–100 ms | 2 | |
| `release` | Release | 5–1000 ms | 120 | |
| `lookahead` | Lookahead | 0–10 ms | 0 | latency, R6 |
| `sc_hpf` | SC Low Cut | 20–1000 Hz | 20 | detector only |
| `listen` | SC Listen | on/off | off | R8 |
| `makeup` | Makeup | 0–24 dB | 0 | scaled by mix |
| `mix` | Mix | 0–100 % | 100 | parallel ducking |
| `ghost_div` | Division | 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4 T, 1/8 T, 1/16 T, 1/2 D, 1/4 D, 1/8 D | 1/4 | |
| `ghost_depth` | Depth | 0–48 dB | 12 | |
| `ghost_attack` | G Attack | 1–100 ms | 5 | |
| `ghost_hold` | G Hold | 0–500 ms | 40 | |
| `ghost_release` | G Release | 10–2000 ms | 180 | |
| `ghost_curve` | Curve | 0–100 % | 50 | 0 = linear, 100 = (1−x)^2.5 |
| `ghost_offset` | Offset | 0–100 % | 0 | phase within the division |
| `ghost_bpm` | Free BPM | 40–240 bpm | 120 | used only when the host gives no tempo |

**Editor:** `EditorBase`, 640 px wide, one column, blue accent `#5AC8FA`, with
a GR meter in the header. Sections: Detector (with the sidechain status
line), Output, and Ghost Ducking (tempo-synced, no sidechain needed).

## 5. Acceptance tests

- `host_Ducker` (`tests/host/VST3HostTest.cpp`, 48 kHz / 512,
  `enableAllBuses()`, playhead 120 bpm playing):
  - `commonChecks`: latency ≥ 0; silence gives finite output; state round
    trip (R11).
  - `testDucker` (External, −40 dB, 10:1, Mix 100 %): the sidechain bus is
    exposed and enabled with ≥ 3 input channels (R3); output is finite; a
    1 kHz tone at 0.5 with a silent sidechain gives RMS 0.354 ± 0.03, i.e.
    unity (R9); adding 100 Hz at 0.9 on the sidechain ducks it by > 6 dB (R1).
    Ghost mode, 24 dB, 1/4: in 10 ms windows, max RMS > 0.3 and
    min < 0.2 × max (R4, R5). Lookahead 5 ms reports 240 ± 2 samples (R6).
- `ap_tests`:
  - `test_dynamics.cpp`: `gain_computer_hard_knee`,
    `gain_computer_soft_knee_is_smooth_and_monotonic`,
    `gain_computer_limiter_ratio` and `envelope_follower_tracks` (R1).
  - `test_dynamics.cpp`: `ghost_envelope_shape` (boundary unity, floor through
    hold, half depth mid-release, beat lookup 120 bpm 1/4) and
    `note_division_table` (R4).
  - `test_synthesis.cpp`: `biquad_lowpass_attenuates_highs` and
    `smoother_converges` (R7).
- Missing, add in v0.2: Internal mode, SC Listen, free-run while stopped,
  Offset and Curve, the disabled-sidechain fallback, a mono sidechain.

## 6. Out of scope / later

- Multiband or frequency-selective ducking, a MIDI-triggered ghost mode, a
  drawable ghost envelope, and a transient-only detector.
- A sidechain level meter and a GR history display. RMS detection and an
  unlinked stereo mode.
- User presets, AU/CLAP, and Developer-ID signing and notarization.
- Known v0.1 gaps:
  - Ghost mode with Lookahead > 0: the envelope is computed at input time
    but applied to delayed audio. After delay compensation the dip lands
    early by the lookahead time. Ghost should ignore lookahead; until then,
    leave it at 0.
  - SC Listen skips Mix and the lookahead delay, so toggling it shifts timing
    by the lookahead.
  - The README says Ghost mode uses "Free BPM when stopped". In fact the
    host tempo wins whenever the host reports one (R5).
