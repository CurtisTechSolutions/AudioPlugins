# Intone — PRD

Status: shipped v0.1 (VST3, Windows/macOS/Linux)

## 1. Summary

Intone is pitch correction and a compact vocal chain in one insert. It tracks
a monophonic vocal with YIN and pulls it to the nearest note of a key and
scale (14 presets or a 12-note custom scale) using a formant-preserving PSOLA
shifter, with Retune Speed and Amount controls. A low cut, de-esser,
compressor and body / presence / air EQ follow. "Intone" is a **working title
(placeholder)**: rename it via `PRODUCT_NAME` in
`plugins/Intone/CMakeLists.txt` and the editor title, and keep
`PLUGIN_CODE Intn`. Format: VST3 Fx|Pitch Shift, mono→mono or
stereo→stereo, no MIDI. Code:
`plugins/Intone/` on `core/include/ap/PitchDetector.h` (YIN), `PitchTracker.h`
(tracker + `PitchTimeline`), `PitchShifter.h` (`PsolaShifter`), `Scales.h`,
`Dynamics.h` and `Filters.h`.

## 2. Who it is for and why

Home-studio vocalists, producers and podcasters who want a tuned, finished
vocal from one plugin rather than a tuner plus four chain plugins. Use a 20–60 ms
retune with Amount below 100 % for transparent correction, or 0 ms for the
hard-tuned effect. Formant preservation stops a corrected voice from sounding
chipmunk-like. The chain defaults (compressor at −18 dB, 3:1; low cut at
80 Hz) make a raw vocal usable without any tweaking.

## 3. Requirements

- R1. Detection: the mono sum is fed sample by sample to one of three
  trackers (`tracking`), hop 128. Frames go into a `PitchTimeline` (the last
  64). The YIN threshold is lerp(0.08, 0.35, sensitivity). There is no level
  gate.
- R2. Every 32 samples the control step takes the frame closest to the
  shifter's `outputTime()`, so correction lines up with the delayed audio.
- R3. Target = `Scale::nearest(det + transpose)` (ties go to the lower note).
  - The target is smoothed by a one-pole with a time constant of `speed` ms
    (0 = instant).
  - At each phrase start the smoother begins at the sung pitch.
  - corrected = det + `amount`·(smoothed − det), and the PSOLA ratio =
    2^((corrected − det)/12), clamped to 0.25–4.
- R4. Unvoiced frames (consonants, breaths, silence) pass through
  unshifted: unity grains overlap-add back to the input. Amount 0 % leaves the
  pitch untouched, and that includes Transpose.
- R5. Formants are preserved: grains are two periods taken from the input.
  Level stays within −15 / +20 % for ratios 0.85–1.2.
- R6. Custom scale (Scale = Custom): `note_0`…`note_11` are absolute pitch
  classes C…B, rotated to the key root. An empty mask behaves as chromatic.
- R7. **Latency is about 50 ms**, fixed at 2.5 periods of 50 Hz: 2205 samples
  at 44.1 kHz, 2400 at 48 kHz, 4800 at 96 kHz. It is reported once in
  `prepareToPlay`, so playback is aligned by the host's delay compensation.
  Live monitoring through Intone is 50 ms late. The README tells singers to
  use direct monitoring or the host's low-latency mode.
- R8. Mix blends the wet path (shifter + chain) with dry delayed by the same
  latency. So Mix 0 % bypasses the chain too, and partial Mix on a corrected
  note can comb-filter. Use Amount for gentle correction.
- R9. The chain runs per channel after the shifter (dynamics unlinked):
  1. 12 dB/oct HP at `low_cut`.
  2. De-esser: the band above `deess_freq` (low = x − HP(x)) is ducked at
     6:1, 6 dB knee, peak detector 1/40 ms, threshold lerp(0, −50 dB,
     `deess_amount`).
  3. RMS compressor (`comp_*`, 6 dB knee) + makeup.
  4. Body peak 250 Hz Q 1, presence peak 3.2 kHz Q 1.2, air shelf 11 kHz.
  5. Mix and Output, both smoothed over 20 ms.
- R10. The UI shows detected pitch → target note, compressor gain reduction
  (channel 0) and output peak.
- R11. Detection and shifting must share one sample clock. **Known v0.1
  defect:** `reset()` clears the timeline and shifters but not the trackers.
  The JUCE VST3 wrapper calls it when the host stops processing. After that,
  tracker time runs ahead of shifter time, the lookup returns the oldest
  stored frame, and correction lags the audio by about 140 ms (Mid, 48 kHz)
  until the next `prepareToPlay`. This was reproduced with a scratch build of
  the core headers. Switching Voice Range mid-session also breaks the
  alignment. Idle trackers are not fed, so the new tracker's clock is behind
  the shifter's, the lookup falls back to the newest frame, and correction
  runs 16–36 ms ahead of the audio (depending on range) until the next
  `prepareToPlay`. Fix in v0.1.x: reset all trackers in `reset()` and on a
  range switch, or stamp frames from one plugin-owned counter.
- R12. Output is finite for any input. No allocation after `prepareToPlay`.
  State is APVTS XML with root `INTONE`. Parameter IDs and names are frozen.
- R13. Passes `host_Intone` and pluginval strictness 10 in CI on all three OSes.

## 4. Design

```
in ─┬─► mono sum ─► PitchTracker[range] (hop 128) ─► PitchTimeline ─► at(outputTime)
    │      nearest(det+transpose) ─► one-pole(speed) ─► ×amount ─► ratio, period ─┐
    ├─► PsolaShifter L/R (≈50 ms) ◄────────────────────────────────────────────────┘
    │      └─► LowCut ─► De-esser ─► Comp+Makeup ─► Body ─► Presence ─► Air ─┐
    └─► dry delay (= latency) ──────────────────────────────────────── Mix ──┴─► Output ─► out
```

Voice Range presets (`kPresets`): Low = 50–900 Hz with a 2048-sample window;
Mid = 70–1400 Hz with 1024; High = 120–2200 Hz with 640. The shifter's 50 Hz
floor sets both the maximum grain length and the latency.

### 4.1 Parameters (32)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `key` | Key | C … B | C | scale root |
| `scale` | Scale | Chromatic, Major, Natural/Harmonic/Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major/Minor Pentatonic, Blues, Whole Tone, Custom | Chromatic | 15 entries |
| `speed` | Retune Speed | 0–400 ms | 20 | 0 = hard tune |
| `amount` | Amount | 0–100 % | 100 | blends in pitch, not audio |
| `transpose` | Transpose | −12 – +12 st | 0 | before snapping, scaled by Amount |
| `tracking` | Voice Range | Low / Mid / High | Mid | |
| `sensitivity` | Sensitivity | 0–100 % | 50 | |
| `mix` | Mix | 0–100 % | 100 | R8 |
| `note_0` `note_1` `note_2` `note_3` `note_4` `note_5` `note_6` `note_7` `note_8` `note_9` `note_10` `note_11` | C, C#, D, D#, E, F, F#, G, G#, A, A#, B (in that order) | on/off | all on | 12 params, absolute pitch classes; used only by Custom |
| `deess_amount` | De-ess | 0–100 % | 0 | 0 % sets threshold to 0 dBFS (not a hard bypass) |
| `deess_freq` | De-ess Freq | 3000–12 000 Hz | 6500 | |
| `comp_thresh` | Threshold | −50 – 0 dB | −18 | active by default |
| `comp_ratio` | Ratio | 1–20 :1 | 3 | 1:1 = off |
| `comp_attack` | Attack | 0.5–100 ms | 10 | |
| `comp_release` | Release | 10–1000 ms | 120 | |
| `comp_makeup` | Makeup | 0–24 dB | 0 | |
| `low_cut` | Low Cut | 20–400 Hz | 80 | always in circuit |
| `body` | Body | −12 – +12 dB | 0 | |
| `presence` | Presence | −12 – +12 dB | 0 | |
| `air` | Air | −12 – +12 dB | 0 | |
| `output` | Output | −24 – +24 dB | 0 | |

**Editor:** `EditorBase`, 700 px wide, one column, purple accent `#BF5AF2`.
The header shows a PitchReadout ("A3 → A3"). Sections: Pitch Correction;
the 12-toggle Custom Scale Notes row; De-esser & Compressor with a GR meter;
Tone & Output with an output meter.

## 5. Acceptance tests

- `host_Intone` (`tests/host/VST3HostTest.cpp`, 48 kHz / 512):
  - `commonChecks`: latency ≥ 0; silence gives finite output; state round
    trip (R12).
  - `testIntone`: 0 < latency < fs/5 (R7). With Speed 0 ms, Amount 100 %,
    Mix 100 % and Chromatic, a 6-harmonic 224 Hz tone (+31 ct) comes out at
    220 ± 2.5 Hz with RMS > 0.05 (R2, R3). Amount 0 % leaves it at
    224 ± 2.5 Hz (R4). In C Major, 281.2 Hz (C# +25 ct) moves to 293.66 or
    261.63 ± 3 Hz (R3).
- `ap_tests`:
  - `test_shifter.cpp`: `psola_identity_is_delayed_passthrough`, error < 1e-3
    (R4); `psola_shifts_pitch_and_keeps_level`, ratios 0.75–2 within 1 % (R5);
    `psola_handles_unvoiced_and_period_changes`.
  - `test_pitch.cpp`: YIN tests, `pitch_tracker_streams_frames` and
    `pitch_timeline_lookup` (R1, R2).
  - `test_scales.cpp`: `scale_nearest`, including the C-only Custom mask and
    the empty mask (R3, R6).
  - `test_dynamics.cpp`: gain-computer knee and ratio tests,
    `envelope_follower_tracks` (R9).
  - `test_synthesis.cpp`: `biquad_lowpass_attenuates_highs`,
    `smoother_converges`.
- Missing, add in v0.1.x/v0.2: a host check that stops processing, resumes,
  and still sees correction (would catch R11); also the custom scale, retune
  timing, the de-esser and compressor, and mono I/O.

## 6. Out of scope / later

- A low-latency live mode (e.g. an 80 Hz floor for a shorter window). v0.1
  is ~50 ms by design (R7).
- Polyphonic or harmony correction, formant and gender shift, vibrato
  controls, MIDI-driven target notes, and graphical note editing.
- Per-section bypass switches (the de-esser and compressor are in circuit at
  their defaults), linked stereo dynamics, and oversampling.
- Presets, AU/CLAP, and Developer-ID signing and notarization.
