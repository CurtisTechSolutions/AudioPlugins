# VoxNote — PRD

Status: shipped v0.2 (VST3, Windows/macOS/Linux)

## 1. Summary

VoxNote is an audio effect that turns a hummed, sung or whistled line into
MIDI. A YIN pitch tracker feeds a note state machine with onset and release
gates. Pitch movement inside a ±Retrigger zone becomes pitch bend; movement
beyond it starts a new note. It also offers optional scale snapping and
velocity from input level, plus a built-in sine "monitor synth" for hearing
detection without routing. "VoxNote" is a **working title (placeholder)**:
rename it via `PRODUCT_NAME` in `plugins/VoxNote/CMakeLists.txt` and the
editor title, and keep `PLUGIN_CODE Vxnt`. Format: VST3 Fx|Tools, mono or
stereo audio in/out, MIDI out (`producesMidi`), no MIDI in. Code:
`plugins/VoxNote/` on `core/include/ap/PitchDetector.h` (YIN),
`PitchTracker.h`, `NoteTracker.h`, `Scales.h` and `Oscillator.h`.

## 2. Who it is for and why

Songwriters, producers and laptop beatmakers who can sing a melody or bass
line faster than they can play it, or who have no controller with them.
VoxNote sits on the mic track and its MIDI drives any instrument (e.g.
Strata), so the idea becomes an editable part instead of a vocal to
transcribe. Bending instead of retriggering keeps a wobbly voice from
spraying short notes. The monitor synth gives instant feedback while setting
Gate and Sensitivity.

## 3. Requirements

- R1. Monophonic. The mono sum of the inputs is fed sample by sample to one
  of three trackers (`tracking`). All three are prepared up front, so
  switching never allocates.
- R2. One frame per 256-sample hop (5.3 ms at 48 kHz). Each frame carries Hz,
  fractional MIDI, YIN confidence and hop RMS (dB). A 3-frame median on MIDI
  removes one-frame octave errors.
- R3. A frame is *active* when it is voiced, its confidence is ≥ minConf and
  its RMS is ≥ `gate`. A note starts after `onset` ms of consecutive active
  frames and ends after `release` ms of inactive frames. Both are rounded to
  hops, minimum 1.
- R4. While a note plays, deviation dev (cents, after `transpose`) is checked:
  - |dev| > `retrigger`, held for the onset time on the same new note:
    note-off, then note-on.
  - Otherwise, with `bend` on: bend = dev / `bend_range`, clamped to ±1, sent
    when it changes by > 0.002 and re-centred before the next note-on.
- R5. Sensitivity s sets minConf = lerp(0.9, 0.3, s) and YIN threshold =
  lerp(0.08, 0.35, s). Higher catches breathier singing but also more false
  notes.
- R6. Note = round(MIDI + transpose), or `Scale::nearest` when `snap` is on
  (ties go to the lower note), clamped to 0–127. With Snap and Bend both on,
  the bend still carries the deviation from the snapped note. Turn Bend off
  for hard snapping.
- R7. Velocity: with `vel_dynamic` on, the onset frame's RMS maps linearly
  from [`vel_min`, `vel_max`] dB to 1–127 (vel_max ≥ vel_min + 1 enforced).
  Otherwise the velocity is `vel_fixed`.
- R8. MIDI goes out on `channel`, at the sample where the deciding frame
  completed. Incoming MIDI is dropped. Pitch wheel = 8192 + bend·8191. Every
  note-on gets a note-off (on silence, on retrigger, or via R9).
- R9. `reset()` clears note state, and the next block opens with All Notes
  Off (CC123) on the current channel. The JUCE VST3 wrapper calls `reset()`
  when the host stops processing.
- R10. With Audio Thru on, input passes unchanged; mono input is copied to
  every output. With it off, output is silent apart from the monitor synth: a
  sine at note + bend, 4 ms / 60 ms one-pole envelope, at `monitor_level`.
- R11. **Latency.** `getLatencySamples()` = 0 and MIDI is not back-dated.
  Each frame describes audio `PitchTracker::latencySamples()` earlier:
  816 / 1168 / 1880 samples (≈ 17 / 24 / 39 ms at 48 kHz) for
  High / Mid / Low. A note-on therefore lands that much, plus the onset time,
  after the sung onset (the README says ~20–40 ms). Recorded MIDI may need
  nudging earlier.
- R12. **MIDI output from an audio effect.** Host MIDI reaches an instrument
  only in hosts that route MIDI out of an effect (Ableton "MIDI From",
  Reaper, Bitwig). FL Studio's VST3 wrapper often drops it (users reported
  "no MIDI at all"), and Logic / GarageBand cannot route it; R14 covers those.
- R13. Output is finite for any input, with no allocation after
  `prepareToPlay` (frame callbacks run inline). State is APVTS XML with root
  `VOXNOTE`, and parameter IDs and names are frozen. Passes `host_VoxNote` and
  pluginval strictness 10 on all three OSes.
- R14. **Direct to a MIDI port.** Every event that goes to the host also goes
  to an optional system MIDI port (`MidiPortOut`), chosen in the editor's
  *MIDI out* card: any MIDI output on the system (a loopMIDI port on
  Windows, an IAC bus on macOS) or, on macOS / Linux, VoxNote's own virtual
  port "VoxNote". The audio thread only pushes to a wait-free queue
  (`ap::SpscQueue`, 1024 events, dropped when full); a high-priority sender
  thread, running only while a port is open, sends each event at its block
  start time + sample offset, so spacing within a block is kept. Before a
  port is closed or changed every note it started is turned off. Offline
  (non-real-time) renders do not send to the port. The choice is not a
  parameter: it is saved as the state-tree properties `midiPortId` /
  `midiPortName` and reopened on the message thread after
  `setStateInformation` (by name when the ID changed between sessions; shown
  as "(not found)" when the port is missing).
- R15. **Bypass.** While the host bypasses VoxNote audio passes through, no
  MIDI is produced, and a note sounding at the moment of bypass gets its
  note-off (and the bend is re-centred), on the host and on the port.

## 4. Design

```
in ─► mono sum ─► PitchTracker[tracking] (YIN, hop 256, median-3) ─► Frame{time,Hz,midi,conf,rmsDb}
   ─► NoteTracker (gate/conf ─► onset ─► bend | retrigger ─► release) ─► MidiBuffer @ sample i
                                                                    └─► monitor sine ─┐
in ─► Audio Thru (or clear) ─────────────────────────────────────────────────── (+) ─► out
```

Tracking presets (`kPresets`), with the YIN block (window + τmax) at 48 kHz:
Low (bass) 40–1000 Hz, window 2048, block 3248; Mid (voice) 60–1600 Hz, 1024,
1824; High (whistle) 100–3000 Hz, 640, 1120. `updateTrackerParams()` runs once
per block, converting ms to hops (48 kHz defaults: onset 3, release 8). The
retrigger hold equals the onset count.

### 4.1 Parameters (20)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `tracking` | Tracking | Low (bass) / Mid (voice) / High (whistle) | Mid (voice) | |
| `gate` | Gate | −80 – −10 dB | −45 | hop RMS |
| `sensitivity` | Sensitivity | 0–100 % | 55 | R5 |
| `onset` | Onset | 5–120 ms | 15 | also the retrigger hold |
| `release` | Release | 10–300 ms | 40 | |
| `retrigger` | Retrigger | 30–100 ct | 60 | bend-zone half-width |
| `transpose` | Transpose | −24 – +24 st | 0 | applied before snapping |
| `snap` | Snap To Scale | on/off | off | |
| `key` | Key | C … B | C | |
| `scale` | Scale | Chromatic, Major, Natural/Harmonic/Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major/Minor Pentatonic, Blues, Whole Tone | Chromatic | 14 entries, no Custom |
| `bend` | Pitch Bend | on/off | on | |
| `bend_range` | Bend Range | 1–24 st | 2 | must match the receiving synth |
| `vel_dynamic` | Dynamic Vel | on/off | on | |
| `vel_fixed` | Fixed Vel | 1–127 | 100 | |
| `vel_min` | Vel Min | −60 – −20 dB | −40 | → velocity 1 |
| `vel_max` | Vel Max | −30 – 0 dB | −8 | → velocity 127 |
| `channel` | MIDI Channel | 1–16 | 1 | |
| `audio_thru` | Audio Thru | on/off | on | |
| `monitor` | Monitor Synth | on/off | off | |
| `monitor_level` | Monitor Level | −40 – 0 dB | −12 | |

**Editor:** `ap::ui::EditorBase`, 1040 × 640 base (resizable 70–200 %),
green `#34C759`. Header: tuner read-out (note, cents needle, Hz) and a
*MIDI out* activity light. A pitch display (the last ~6 s as a piano roll:
the detected pitch as a line, bright where it passes gate and confidence,
and the sent notes as labelled bars; scale rows tinted when Snap is on)
sits next to Detection, which has the input RMS against the Gate line and a
one-line diagnosis (waiting for input / below the gate / no steady pitch /
sending E4). Cards: Notes, Snap to scale (header switch), Pitch bend (header
switch), Velocity (Fixed vs. Min/Max dim with *Dynamic*), Output, MIDI out
(R14 port choice, status and setup hints). The processor feeds the display
through `uiFrames`, an `SpscQueue` of one `UiFrame` per hop.

## 5. Acceptance tests

- `host_VoxNote` (`tests/host/VST3HostTest.cpp`, 48 kHz / 512):
  - `commonChecks`: latency ≥ 0; finite on silence; state round trip (R13).
  - `testVoxNote`: 1 s of a 6-harmonic 224 Hz tone (+31 ct over A3), then
    40 silent blocks. Checks: MIDI appears while the tone plays; 1–3
    note-ons, so no chatter (R3, R4); the last note is 57 (A3) (R6);
    note-offs equal note-ons (R8); at least one bend (R4); output is finite.
    Audio Thru Off with a 220 Hz tone gives RMS < 1e-6 (R10). Bypassing
    while a note sounds sends its note-off, no MIDI comes out while bypassed,
    tracking resumes afterwards (R15); `reset()` mid-note is followed by All
    Notes Off (R9).
- `ap_tests`:
  - `test_pitch.cpp` (R1, R2): `yin_detects_pure_tones` (82–1047 Hz within
    0.5 %), `yin_detects_harmonic_tones_with_noise`, `yin_rejects_noise`,
    `pitch_tracker_streams_frames` (odd blocks, 330 ± 2 Hz,
    0 < latency < 4096).
  - `test_notes.cpp` (R3–R7): `note_tracker_basic_note`,
    `note_tracker_retrigger_on_pitch_change` (a 1-frame blip is ignored),
    `note_tracker_gate_and_confidence`,
    `note_tracker_pitch_bend_and_scale_snap` (+0.5 st gives bend 0.25),
    `note_tracker_velocity_mapping` (−10/−25/−40 dB give 127/64/1).
  - `test_scales.cpp`: `scale_membership`, `scale_nearest` (R6).
- `ap_tests`: `spsc_queue_*` (order, full-queue drop, wrap-around, two
  threads) for the queue behind R14 and the display.
- Not automated: the port itself (CI runners have no MIDI devices; checked
  by hand with loopMIDI / IAC), the monitor synth, transpose, channel, and
  mono in / stereo out.

## 6. Out of scope / later

- Polyphonic detection, and CC output (e.g. level → CC11, brightness → CC74).
- Latency-compensated or back-dated MIDI (R11), and an offline "print MIDI
  clip" mode.
- A custom scale (Intone's 12 toggles), key/scale learn, and note-length
  quantise.
- An AU MIDI FX variant for Logic (R14's port already works there), AU/CLAP
  in general, and Developer-ID signing and notarization.
- Known gaps:
  - Switching Tracking mid-phrase analyses the new tracker's stale buffer for
    up to one window.
  - Windows has no virtual port without extra software (loopMIDI); Windows
    MIDI Services could provide one once it ships widely.
