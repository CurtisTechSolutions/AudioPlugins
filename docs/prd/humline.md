# Humline — PRD

Status: in progress v0.1 (VST3; unit + host tests pass on Linux; Windows/macOS CI and pluginval 10 pending)

## 1. Summary

Humline is an instrument you hum into. It chains the three existing products
into one device: VoxNote's voice-to-MIDI tracking, Intone's key/scale
quantisation and Strata's synth voice. A mic on its input bus becomes a tuned,
optionally grid-quantised MIDI line that plays an embedded synth straight away
and is also emitted as MIDI, so the same line can drive any other instrument.
A **Capture** button records the detected notes of a take, and the take can be
saved as a `.mid` file or dragged out of the editor into the DAW. "Humline" is
a **working title (placeholder)**: rename it via `PRODUCT_NAME` in
`plugins/Humline/CMakeLists.txt` and the editor title, and keep
`PLUGIN_CODE Huml`. Format: VST3 Instrument|Synth with a mono or stereo main
audio *input* bus, stereo or mono output, MIDI out (`producesMidi`), no MIDI
in. Code: `plugins/Humline/` on `core/include/ap/PitchTracker.h`,
`NoteTracker.h`, `Scales.h`, `Tempo.h`, the new `NoteQuantizer.h`
(`RhythmQuantizer`, `NoteCapture`) and the shared synth voice in
`common/include/ap_synth/SynthVoice.h` (moved out of Strata, unchanged).

## 2. Who it is for and why

Songwriters and producers who can hum a hook faster than they can play it.
VoxNote already turns a voice into MIDI, but using it means routing MIDI from
an effect to an instrument track, choosing a synth, and cleaning up the notes
afterwards. Humline removes every one of those steps for the "quick idea"
case: drop it on the mic track, pick a key and a timbre, hum, hear the line
played back in tune, hit Capture and drag the clip into the arrangement. The
MIDI output stays available for the day the built-in timbres are not enough.

## 3. Requirements

- R1. Monophonic. The mono sum of the input bus feeds one of three
  `PitchTracker`s (`tracking`, same presets as VoxNote: Low 40–1000 Hz /
  window 2048, Mid 60–1600 / 1024, High 100–3000 / 640, hop 256, 3-frame
  median). All three are prepared up front, so switching never allocates.
- R2. Note segmentation is `ap::NoteTracker` with: gate = `gate`,
  minConf = lerp(0.9, 0.3, `sensitivity`), YIN threshold =
  lerp(0.08, 0.35, `sensitivity`), onset hold = retrigger hold = `min_note`
  (in hops, min 1), release = `release` (hops, min 1), retrigger zone fixed at
  ±60 ct, transpose = 12·`octave`, dynamic velocity from the onset frame's RMS
  mapped from [−40, −8] dB to 1–127 (`vel_dynamic` on) or `vel_fixed`.
- R3. **Scale snapping with strength.** Note = `quantizeNote(midi + 12·octave,
  scale, snap)` where `quantizeNote` pulls the fractional MIDI value toward
  `Scale::nearest` by `snap`/100 and rounds: 0 % is chromatic, 100 % always
  lands on the scale, and in between an out-of-scale note survives only when
  the voice sits close to its centre. `NoteTracker::Params::snapStrength`
  carries this; at 1.0 it is identical to the old `snapToScale` behaviour, so
  VoxNote is unchanged. Scales are the 14 non-custom `ap::ScaleType`s rooted
  on `key`.
- R4. With `bend` on, deviation inside the retrigger zone is sent as pitch
  bend over `bend_range` semitones (to MIDI out and to the synth), re-centred
  before every note-on, exactly as VoxNote. Default is **off** so the line is
  hard-tuned, which is the point of the plugin.
- R5. **Rhythmic quantise (live).** With `grid` ≠ Off, `ap::RhythmQuantizer`
  moves each note-on to the nearest grid line of the host clock: if the
  nearest line is already past, the note starts immediately (late snap);
  otherwise it is deferred until that line. A deferred note that gets its
  note-off before it started is dropped (it was shorter than the wait). A new
  note-on replaces a pending one (monophonic). Note-offs are never deferred.
  Grid values: Off, 1/4, 1/8, 1/16, 1/8 T, 1/16 T (beats from
  `ap::noteDivisionBeats`). The clock is host PPQ/BPM while the transport
  plays; otherwise a free-running clock at the host BPM (120 if none), as in
  Ducker's ghost mode.
- R6. **Synth.** Notes (post-quantiser) drive an `ap::synth::PolySynth`
  (Strata's voice) with polyphony 4 so release tails overlap the next note.
  `timbre` selects a base patch (Soft Lead, Saw Lead, Pluck, Pad, Bass, Bell;
  waves, levels, filter type, filter envelope, decay/sustain). `synth_cutoff`,
  `synth_res`, `synth_attack`, `synth_release` and `glide` replace the
  corresponding fields of the chosen patch; `bend_range` is the synth's bend
  range. Output is scaled by `synth_level` (20 ms smoother). No chorus /
  delay / reverb (use the MIDI out and Strata for that).
- R7. **MIDI out.** With `midi_out` on, note-on/off and bend go out on
  `channel` at the sample they were decided; All Notes Off (CC123) opens the
  first block after `reset()`. With it off, the host MIDI buffer is empty but
  the synth still plays.
- R8. **Dry voice.** The input is delayed by the reported latency (R10) and
  mixed in at `dry_mix` (20 ms smoother). Mono input is copied to every
  output.
- R9. **Capture.** While *Capture* is on (an editor button, not a parameter),
  every note that gets a note-off is appended to an `ap::NoteCapture`
  (capacity 4096 notes, preallocated; the audio thread writes, the editor
  reads a consistent snapshot via a generation counter). Each note stores
  MIDI note, velocity, and start/end both in samples and in beats, **taken
  from the pre-quantiser stream and back-dated by the reported latency**, so
  the take lines up with the humming rather than with the detector. Notes
  shorter than `min_note` are not stored. *Clear* empties the take (on the
  audio thread, via a flag).
- R10. **Latency.** `getLatencySamples()` = `PitchTracker::latencySamples()`
  of the active tracker + the `min_note` hold in samples, published whenever
  it changes (as Ducker does for lookahead). The dry path and the capture use
  the same number. The synth and MIDI out are *not* delayed any further, so
  with host compensation on playback they land on the sung onsets; while
  monitoring live they lag by that much (≈ 24 ms + Min Note at 48 kHz, Mid).
- R11. **Export.** *Save .mid…* opens an async `juce::FileChooser`; the drag
  handle writes the same file to the temp folder and starts an external file
  drag (`DragAndDropContainer::performExternalDragDropOfFiles`), which drops
  into any DAW that accepts `.mid` files. The file is a type-0 `juce::MidiFile`
  at 960 PPQ with a tempo event (BPM at capture time), a time-signature event
  and one note-on/off pair per captured note. Times are relative to the start
  of the bar containing the first note, so the phrase keeps its position
  inside the bar. When `grid` ≠ Off, the export applies the *offline*
  quantise: starts move to the nearest grid line (earlier or later), lengths
  to the nearest grid multiple, minimum one grid. Overlaps caused by the
  quantise are trimmed so no two notes overlap (monophonic clip).
- R12. Output is finite for any input, with no allocation on the audio thread
  after `prepareToPlay` (frame callbacks run inline, the MIDI buffers are
  pre-sized, the capture is preallocated). State is APVTS XML with root
  `HUMLINE`; the take is not part of the state. Parameter IDs and names are
  frozen. Passes `host_Humline` and pluginval strictness 10 on all three
  OSes.
- R13. **Hosting.** An instrument with an audio input appears in each DAW
  differently: Ableton shows an *Audio From* chooser on the device (pick the
  mic track), Bitwig offers the sidechain/audio-input selector, Reaper and FL
  Studio simply run it on the mic track like any effect. Logic Pro is out
  (AU, and AU instruments do not emit MIDI); the MIDI output works in hosts
  that route MIDI from a VST3 instrument (Ableton "MIDI From", Reaper,
  Bitwig, FL Studio).

## 4. Design

```
in ─► mono sum ─► PitchTracker[tracking] ─► NoteTracker (gate, hold = min_note, snap strength, octave)
                                              │  events @ sample i (emission time)
                                              ├─► NoteCapture  (time − latency, beat − latency·bps)  ─► .mid (offline grid)
                                              └─► RhythmQuantizer (live grid: late snap / defer / drop)
                                                     ├─► MidiBuffer out (channel, if midi_out)
                                                     └─► PolySynth (timbre + overrides) ─► × synth_level ─┐
in ─► DelayLine (latency) ─► × dry_mix ─────────────────────────────────────────────────────── (+) ─► out
```

`updateParams()` runs once per block: tracker switch (with clock hand-over as
Intone does), `NoteTracker::Params`, latency (`setLatencySamples` only when
the value changes), the synth `Settings` (timbre patch, then overrides), and
the grid. The block's beat clock is `{startSample, startBeat, beatsPerSample}`;
pending quantiser events are advanced sample by sample inside the tracker
loop so their due times keep sample accuracy.

### 4.1 Parameters (24)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `tracking` | Tracking | Low (bass) / Mid (voice) / High (whistle) | Mid (voice) | R1 |
| `gate` | Gate | −80 – −10 dB | −45 | hop RMS |
| `sensitivity` | Sensitivity | 0–100 % | 55 | R2 |
| `min_note` | Min Note | 20–200 ms | 60 | onset + retrigger hold, capture filter, part of latency |
| `release` | Release | 10–300 ms | 50 | |
| `key` | Key | C … B | C | |
| `scale` | Scale | Chromatic … Whole Tone (14) | Major | no Custom |
| `snap` | Snap | 0–100 % | 100 | R3 |
| `octave` | Octave | −2 – +2 | 0 | ×12 st |
| `bend` | Pitch Bend | on/off | off | R4 |
| `bend_range` | Bend Range | 1–24 st | 2 | MIDI out and synth |
| `grid` | Grid | Off / 1/4 / 1/8 / 1/16 / 1/8 T / 1/16 T | Off | R5, R11 |
| `timbre` | Timbre | Soft Lead / Saw Lead / Pluck / Pad / Bass / Bell | Soft Lead | R6 |
| `synth_cutoff` | Cutoff | 20–20 000 Hz | 3000 | |
| `synth_res` | Resonance | 0–100 % | 20 | |
| `synth_attack` | Attack | 1–2000 ms | 5 | |
| `synth_release` | Synth Release | 1–4000 ms | 250 | |
| `glide` | Glide | 0–500 ms | 20 | |
| `synth_level` | Synth Level | −60 – +6 dB | −6 | |
| `dry_mix` | Dry Voice | 0–100 % | 0 | R8 |
| `midi_out` | Send MIDI | on/off | on | R7 |
| `channel` | MIDI Channel | 1–16 | 1 | |
| `vel_dynamic` | Dynamic Vel | on/off | on | |
| `vel_fixed` | Fixed Vel | 1–127 | 100 | |

**Editor:** `EditorBase`, 820 px, 2 columns, rose `#FF5E8A`, with a
PitchReadout (detected → played note) in the header. Left column: Detection
(with input meter), Pitch, Rhythm. Right column: Synth (with output meter),
Output, and the custom *Capture* panel (Capture toggle, Clear, Save .mid…,
drag handle, "n notes · b beats" readout).

## 5. Acceptance tests

- `host_Humline` (`tests/host/VST3HostTest.cpp`, 48 kHz / 512):
  - `commonChecks`: latency ≥ 0; finite on silence; state round trip (R12).
  - `testHumline`: latency between 10 and 300 ms (R10). Key C / Major, Bend
    off: 1 s of a 6-harmonic 224 Hz tone (+31 ct over A3) then silence gives
    1–3 note-ons, last note 57, note-offs = note-ons, no bends (R2–R4, R7);
    the synth output while the tone plays is audible and measures 220 ± 3 Hz
    (R6); after silence the output decays below 1e-3 RMS (R6). Send MIDI off:
    no MIDI events (R7). Dry Voice 100 % / Synth Level −60 dB with a 220 Hz
    tone: the output level is within 3 dB of the input (R8). Snap 100 % with
    Key C / Scale Major Pentatonic and a 311 Hz tone (D#4): the note is in
    the scale (R3).
- `ap_tests`, `test_quantize.cpp`:
  - `quantize_note_strength`: 0 % = chromatic, 100 % = `Scale::nearest`,
    50 % pulls 61.3 to 62, 25 % keeps 61.0 as 61 (R3).
  - `note_tracker_snap_strength_default_is_hard_snap`: `snapToScale` with
    the default strength equals the old behaviour (R3 regression guard).
  - `rhythm_quantizer_late_snap_and_defer`: a note-on 10 % past a 1/8 line
    starts immediately; one 40 % before the next line is deferred to it with
    sample accuracy (R5).
  - `rhythm_quantizer_drops_short_pending_and_replaces`: a note-off before
    a deferred note-on cancels it; a second note-on replaces the pending one
    (R5).
  - `rhythm_quantizer_off_passes_through`: grid Off emits at the input time.
  - `note_capture_records_and_backdates`: notes get start/end in samples and
    beats, a note shorter than the minimum is dropped, `clear()` bumps the
    generation (R9).
  - `note_capture_offline_quantize`: starts snap to the nearest line (both
    directions), lengths to grid multiples ≥ 1 grid, overlaps trimmed (R11).
  - `humline_pipeline_end_to_end`: a synthetic hummed phrase (C4 −20 ct,
    E4 +30 ct, G4, with a 15 ms blip to F#4 and a 60 ms breath gap) through
    `PitchTracker` → `NoteTracker` (hold 60 ms) → `NoteCapture` in C major
    yields exactly C4, E4, G4 with durations within 15 % of the sung ones
    (R1–R3, R9).
- Existing suites stay green: `ap_core_tests`, `frisson_module_tests`,
  `host_Strata` (the shared voice move must not change Strata).
- CI: pluginval strictness 10 on all three OSes (R12).

## 6. Out of scope / later

- Polyphonic (chord) detection; a custom 12-note scale; key/scale learn from
  the take; swing and humanise for the grid; triplet/dotted grids beyond the
  two listed.
- Chorus/delay/reverb on the built-in synth, user presets, MIDI input to play
  the synth from a keyboard.
- Back-dating the *live* MIDI (impossible in real time); a true offline
  "print" mode that renders the whole track at once.
- Saving the take inside the plugin state; multiple takes; undo.
- AU/CLAP, a Logic-compatible variant, and Developer-ID signing and
  notarization.
- Known v0.1 gaps:
  - The drag handle depends on the OS/host accepting an external file drag
    (works in the major DAWs on Windows/macOS; on Linux it depends on the
    host's XDND support). *Save .mid…* always works.
  - A tempo change mid-take is not recorded; the file carries the BPM at the
    first captured note.
  - Changing Min Note or Tracking changes the reported latency at run time;
    hosts that only read latency at transport start will be off by the
    difference until they restart.
