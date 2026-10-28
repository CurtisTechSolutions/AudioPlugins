# Verdant — PRD

Status: in development v0.1 (VST3, Windows/macOS/Linux)

## 1. Summary

Verdant is the flagship wavetable synth. It sits alongside Strata, which stays
the light one-page subtractive synth. Two mipmapped wavetable oscillators with
warp and up to 16-voice unison, a sub and five noise colours feed two
multimode filters (11 types, serial or parallel). Four LFOs, three envelopes,
eight macros and the performance, physics and drift sources drive a 24-slot
matrix that is played by dragging a source onto a knob, and a reorderable
8-slot FX rack finishes the sound. Eight signature features go beyond the
usual feature list: a living drift engine, sample-to-patch matching, physics
mod sources, a key-aware arp and sequencer, patch breeding, sympathetic-string
memory, a spectral paint oscillator and a sound-map preset browser. Verdant is
in the class of the big modern commercial wavetable synths, but it is an
original product with its own name, sound, UI and factory content: the factory
wavetables and presets are generated in code, and no third-party wavetables,
presets, graphics or trade dress are copied.

"Verdant" is a **working title (placeholder)**: rename it via `PRODUCT_NAME` in
`plugins/Verdant/CMakeLists.txt` and the editor title. Keep `PLUGIN_CODE Vrdt`
so saved sessions still find it. Tagline: "Living wavetable synth". Format:
VST3 Instrument|Synth, MIDI in, stereo or mono out. Code: `plugins/Verdant/`
(processor, presets, background jobs, the editor in `ui/`) on a JUCE-free
engine in `plugins/Verdant/engine/` (static lib `verdant_engine`, namespace
`verdant`) built on `core/include/ap/` (Math, Filters, Scales, Tempo,
DelayLine, Saturation, PinkNoise / DriftSource, Oscillator, PitchDetector and
Dynamics).

## 2. Who it is for and why

Producers and sound designers who want one modern wavetable synth for basses,
leads, pads, plucks, keys, FX and evolving textures in any VST3 host, with
deep modulation that stays quick to use: drag a source onto a knob, see the
ring, hear it. People who would rather start from a sound than from an empty
patch: drop a sample and get a patch that gets close, breed two presets they
like into eight new ones, or walk the sound map to the neighbours of a sound.
Composers who want held notes that develop instead of looping (Alive /
Evolve, sympathetic strings) and arps and sequences that stay in key. For the
repo it is the large-scale version of the pattern Frisson started: a JUCE-free
engine on the shared core DSP, one separately tested module per feature,
behind a thin JUCE wrapper.

## 3. Requirements

### 3.1 Oscillators and sources

- R1. Two wavetable oscillators, A and B (A on, B off by default). A table
  holds 1–256 single-cycle frames of 2048 samples: one of the 16 factory
  tables (4.3) or the oscillator's User table (imported, painted or matched).
  Position (0–100 %) is the fractional frame index position × (frames − 1),
  and the oscillator crossfades the two nearest frames, so sweeping Position
  over the whole table adds no sample-to-sample step larger than 0.1.
- R2. Band limiting: every table is kept as per-octave mip levels, where
  level k holds only harmonics 1 … 1024 / 2^k (level 0 = all 1024 of a
  2048-sample frame). The oscillator plays the brightest level whose highest
  harmonic is below Nyquist at the current frequency, crossfading towards the
  next, duller level as the pitch nears the next octave, so a glide does not
  step in brightness. With Warp Off and unison 1, a saw table played at MIDI
  108 (4186 Hz) at 44.1 kHz has aliased energy < −60 dB. A full 256-frame
  table with all its levels takes ≤ 24 MB.
- R3. Warp (`{o}_warp_mode`, amount −100 – +100 %) changes how the frame is
  read. **Sync** reads it faster than the fundamental and restarts it every
  cycle (a hard-sync sweep). **Bend** bends the read phase towards the start
  or the end of the cycle, by the sign of the amount. **Squeeze** squeezes
  the cycle into part of the period, PWM-style. **Mirror** plays the cycle
  forwards, then backwards. **FM Osc** phase-modulates from the other
  oscillator and **FM Sub** from the sub. **Remap** passes the phase through
  a remap curve shaped by the amount. Warps act on the read phase without
  oversampling, so R2's figure is for Warp Off.
- R4. Unison (1–16 copies): the copies are spread evenly over ±Detune
  (cents), panned across ±Spread, and Blend sets the level of the detuned
  copies against the centre. Each oscillator is scaled by 1/√N. Every copy
  starts at Phase (0–360°) plus a random offset scaled by Random Phase
  (100 % = fully random), drawn from the voice's seed at note-on.
- R5. Pitch: the note (A4 = 440 Hz) shifted by Octave (±3), Semi (±12), Fine
  (±100 ct), the oscillator's Pitch (±48 st) and the global Pitch (±48 st),
  plus pitch bend × Bend Range, glide and the Living offsets (R27). Level and
  Pan set the output; Route sends it to Filter 1, Filter 2, Both or Direct
  (R10). Frequency and parameters are set once per control block and ramped
  per sample inside the oscillator.
- R6. Render order inside a voice: the sub, then Osc B, then Osc A. So A's
  FM Osc uses the fresh B signal, B's FM Osc uses A's output from the
  previous control block, and both can FM from the sub.
- R7. Sub: Sine, Triangle, Square or Saw, 0 / −1 / −2 octaves below the note,
  band-limited, routed Direct by default. Noise: White, Pink, Brown, Blue or
  Violet (0, −3, −6, +3, +6 dB/oct), seeded per voice, routed to Filter 1 by
  default. Both start at level 0 %.
- R8. Wavetable import: a WAV file picked with Import, or dropped on an
  oscillator panel, becomes that oscillator's User table. PCM 8 / 16 / 24 /
  32-bit and float 32 / 64-bit, mono or the first channel. A `clm ` chunk
  (`<!>2048 …`) sets the frame size, otherwise it is 2048; any other frame
  size is resampled to 2048 per frame; the frame count is clamped to 256 and
  a partial last frame is dropped. A bad file reports an error and changes
  nothing. Loading runs off the audio thread (R37). The engine also exports a
  table as a 32-bit float WAV with a `clm ` chunk.

### 3.2 Filters

- R9. Two filters per voice (Filter 1 on, Filter 2 off by default; a filter
  that is off is a bypass). Types: LP 12, LP 24, HP 12, HP 24, BP 12, BP 24
  (12 or 24 dB/oct), Notch, Comb + and Comb − (cutoff = comb frequency,
  resonance = feedback, positive or negative), Formant (cutoff sweeps the
  A-E-I-O-U vowel formants on a log mapping over its range, resonance sets
  the formant Q) and Ladder (a 4-pole transistor-ladder-style low-pass with
  saturation). Drive saturates the filter input (0 % = clean). Key Track
  100 % = 1 oct/oct about C4. Mix blends the filter's input (0 %) with its
  output (100 %).
- R10. Routing. **Serial**: sources routed to Filter 1 pass F1 then F2,
  sources routed to Filter 2 enter at F2, and Both takes the Filter 1 path.
  **Parallel**: the F1 and F2 outputs are summed, and Both feeds a source to
  both. **Direct** bypasses both filters in either mode.
- R11. The filters are stable everywhere: cutoff and resonance sweeps in
  every type give no NaN or blow-up at 44.1 kHz and at 192 kHz. LP 24 rolls
  off at ≈ −24 dB/oct. The editor draws each response from
  `filterMagnitudeDb`.

### 3.3 Modulation

- R12. Four LFOs. Shapes: Sine, Triangle, Saw Up, Saw Down, Square, S&H and
  Custom, a breakpoint shape (≥ 2 points, x rising from 0 to 1, y 0..1, a
  curve −1..1 per segment) drawn in the editor, saved in the patch and passed
  to the engine through the hand-off. Rate 0.01–50 Hz, or with Sync one cycle
  per note division (12 divisions, 1/1 … 1/8 D) at the host tempo. Modes:
  **Retrig** restarts the phase at Start Phase on every note-on, per voice;
  **Free** gives all voices one phase derived from the transport (ppq when
  synced and playing, otherwise a running clock); **One-shot** runs once from
  note-on, then holds its final value. Fade In ramps the LFO in after
  note-on, Smooth low-passes it, and Polarity is Bipolar (−1..1) or Unipolar
  (0..1).
- R13. Three AHDSR envelopes; Env 1 is the amp envelope and the voice frees
  itself when it ends. Attack 0–10 s, hold 0–5 s, decay and release
  1 ms – 10 s, sustain 0–100 %; each stage lasts its set time within 5 %.
  Attack Curve > 0 gives a fast start (logarithmic rise), < 0 a slow start.
  Decay Curve, which also shapes the release, < 0 gives an exponential
  (natural) fall and > 0 the inverse. Every curve is monotonic. Attack, Decay
  and Release are modulation destinations, latched per voice at note-on.
  Output 0..1.
- R14. Sources and ranges: LFO 1–4 −1..1 (0..1 when Unipolar); Env 1–3 0..1;
  Velocity, Aftertouch, Mod Wheel, Random (drawn per note), Macro 1–8, Ball
  and Seq Mod 0..1; Pitch Bend, Note, Spring, Orbit and Alive −1..1, with
  Note = (MIDI note − 60) / 60, clamped.
- R15. Eight macros (0–100 %), renameable (the names are saved in the patch).
  They are host-automatable sources, not destinations.
- R16. Matrix: 24 slots of Source, Destination (None or one of the 68
  modulatable parameters, 4.2) and Amount (−100 – +100 %, i.e. −1..1). For
  each destination, n = the knob position in its normalised (skewed) 0..1
  domain + Σ amount × source over the slots that target it; n is clamped to
  0..1, then converted back to plain units. Modulation therefore moves knobs
  through their own travel, and slots that share a destination add up.
  Per-voice destinations are evaluated per voice, with that voice's values
  of the per-voice sources. The global destinations (Master, Pitch, the FX
  and Sympathetic parameters) use the global sources (macros, wheel, bend,
  aftertouch, Free LFOs) plus the per-voice sources of the most recently
  triggered voice.
- R17. Control rate: sources and the matrix run every 32 samples per voice,
  and oscillators, filters and gains ramp linearly to the new values per
  sample. Every control change is smoothed too: after an instant cutoff jump
  the cutoff the filter uses changes by no more than 1 % per sample. No
  zipper noise.
- R18. Drag-to-modulate: dropping a source chip (from the MOD page, or the
  source strip on each page) on a modulatable knob creates a matrix slot for
  that pair, or reuses the one that exists, and opens an inline depth ring
  that is dragged to set the amount. With all 24 slots in use the drop is
  refused. Each knob draws a thin ring per routed source in the source's
  colour, showing the depth bipolar from the knob position, and a live dot at
  the last-triggered voice's modulated value.

### 3.4 Voices, MIDI and output

- R19. Voices 1–32 (default 16); all 32 exist from `prepare` on. New notes
  take only the first `voices`; when those are busy the oldest of them is
  stolen, click-free, and never a voice above the limit (Strata's v0.1
  defect does not repeat). Voice Mode: Poly; Mono (one voice, every note
  retriggers); Legato (one voice, overlapping notes move the pitch without
  retriggering the envelopes). Glide (0–2000 ms) slides from the previous
  note's pitch in log frequency. Bend Range 0–24 st.
- R20. MIDI: note on/off on any channel with velocity, pitch bend, mod wheel,
  channel and polyphonic aftertouch, sustain pedal and All Notes Off.
- R21. Each voice is filtered, multiplied by Env 1 and panned; each oscillator
  is scaled by 1/√unison and each voice by 0.35 for headroom. Master
  (−60 – +12 dB) is a gain smoothed over 20 ms. Mono out = (L + R) / 2.
  Latency is 0; the reported tail is the longest release plus the FX tail.
  Output is always finite. At the defaults (300 ms release) output falls
  below 1e-4 RMS within 3.2 s of note-off.

### 3.5 FX rack

- R22. Eight slots (`fx1_type` … `fx8_type`, each None or one of 8 effects)
  run in slot order 1 → 8 after the sympathetic stage and before Master. Each
  effect has one set of parameters, so a type that appears in more than one
  slot is processed only in its first slot. Dragging a slot in the strip
  reorders the chain by rewriting the slot types. Every slot is None in the
  init patch.
- R23. Effects: **Distortion** (Soft, Hard, Tube, Fold, Crush or Diode curve;
  drive, tone, mix); **Chorus** (rate, depth, feedback, mix); **Phaser** (4,
  6, 8 or 12 stages; rate, depth, feedback, mix); **Flanger** (rate, depth,
  feedback ±100 %, mix); **Delay** (1–2000 ms, or synced: the division's
  length in beats × 60 / bpm at the host tempo; feedback, ping-pong, low and
  high cut, mix); **Reverb** (size, decay, damping, pre-delay, width, mix);
  **EQ** (low shelf, mid peak with Q, high shelf, ±18 dB each);
  **Compressor** (threshold, ratio, attack, release, makeup, mix). Every
  effect stays stable (finite, no runaway feedback) and allocation-free.

### 3.6 Presets

- R24. Factory bank: 40 ± 10 presets built in code, in the categories Bass,
  Lead, Pad, Pluck, Keys, FX and Texture, each with tags, designed to show off
  the engine and the signature features. Each renders non-silent and
  unclipped (peak < 1) at velocity 100. They are listed first and are the
  host's program list (`getNumPrograms()` = the number of factory presets).
- R25. User presets are `.vrdt` files holding the state XML (R35) in
  `<userDocuments>/Curtis Tech Solutions/Verdant/Presets/<Category>/<Name>.vrdt`.
  Save asks for name, category, tags and author. Init loads the init patch
  (every parameter at its default, name and category "Init"). Prev / next
  step through the list.
- R26. The browser lists category, tags, name and author, with a category
  filter, tag chips and a search box that matches all four.

### 3.7 Signature features

- R27. **Living sound engine.** Alive (0–100 %, default 15 %) gives each note
  its own small, seeded variation. At 100 %: pitch ±10 ct, wavetable position
  ±0.08 (of the 0..1 range), filter cutoff ±0.5 oct, attack time ×0.7–1.3 and
  pan ±0.25, uniform, drawn from hash(living seed, note counter) and scaled
  linearly by Alive. Evolve (0–100 %, default 0 %) adds slow drift over the
  held note from `ap::DriftSource` (fastest component 0.5 Hz). At 100 %:
  pitch ±5 ct, position ±0.15, cutoff ±0.5 oct and pan ±0.2, linear in
  Evolve. The drift is also the Alive mod source (−1..1). The seed is saved
  with the patch (default 0x5EED1234) along with a lock flag, and the header
  can re-roll or lock it. The same seed and the same MIDI give a bit-identical
  render. Alive and Evolve at 0 % add nothing.
- R28. **Sample-to-patch match.** An audio sample dropped on the BROWSE
  page's drop zone is analysed off the audio thread, with a progress bar and
  cancel; Apply or Discard follows. Pitch comes from `ap::YinPitchDetector`
  (the median over voiced frames). Spectral frames taken at the pitch period
  become a User table of ≤ 64 frames for Osc A, which follows the sample's
  timbre over time. The amplitude envelope sets Env 1 (attack, decay,
  sustain, release), brightness sets Filter 1 cutoff, noisiness sets the
  noise level, and matrix slot 1 routes Env 2 → Osc A Position so the table
  is scanned through the note. The result is a full patch built from the
  defaults, with the detected pitch and a confidence. On a synthetic decaying
  bright saw the pitch is within 1 %, and Filter 1 cutoff lands above that
  of a dull one.
- R29. **Physics mod sources.** Three small simulations per voice, stepped
  once per control block (dt = 32 / sample rate), deterministic, bounded and
  allocation-free, and animated on the MOD page:
  - **Ball** (height, 0..1): dropped at note-on from a height set by
    velocity, falls under Ball Gravity, bounces with Ball Bounce elasticity
    and comes to rest.
  - **Spring** (displacement, −1..1): a damped spring kicked at note-on in
    proportion to velocity; Spring Stiffness sets how fast it swings, Spring
    Damping how fast it settles.
  - **Orbit** (−1..1): a point circling the centre, restarted at note-on;
    Orbit Speed sets the rate and Orbit Shape the path. The source is its x
    position; the view draws x and y.
- R30. **Key-aware modulation.** A global Key and Scale (the 14 core scales,
  Chromatic … Whole Tone) serve the arpeggiator, the step sequencer and the
  sympathetic strings. Snapping goes to the nearest scale note (ties to the
  lower); scale-degree steps move along the scale from the snapped note.
  - Arpeggiator, first in the MIDI chain: Up, Down, Up-Down, Down-Up, Random
    (seeded), As Played or Chord over 1–4 octaves at a note-division rate,
    with Gate (5–100 % of a step), Swing and Snap to Key. When it is off,
    MIDI passes through unchanged.
  - Step sequencer, after the arp: 16 steps with Pitch (−24 – +24), Velocity,
    Gate (on/off) and Mod lanes; the first Seq Steps (1–16) loop at a
    note-division rate, with Gate, Swing and Snap to Key (on by default).
    While notes are held, each step with its gate on retriggers them,
    transposed by the step's pitch and at the step's velocity. With snap on
    the pitch is in scale degrees from the held note, snapped into the key;
    with snap off it is in semitones. The Seq Mod source is the current
    step's Mod value (0..1), stepped and smoothed over 2 ms.
  - Timing, for both: when the host is playing and reports a tempo, step k
    starts at ppq = k × the division's length in beats, and Swing delays the
    odd steps by swing × 0.5 step. Otherwise they free-run from the first
    note at the host tempo, or 120 bpm without one. At 120 bpm every step
    lands within 1 sample.
- R31. **Patch breeding.** Two parents (the current patch or any preset), a
  Mutation amount and a seed give 8 children. Crossover is per parameter
  group: each group comes whole from one parent picked by a seeded coin,
  then each parameter in it has a 25 % chance of coming from the other
  parent. Mutation adds N(0, mutation × 0.15) to Float and Int parameters in
  the normalised domain (clamped, Int rounded) and flips a Choice or Bool to
  the other parent's value with p = mutation × 0.2. Voices, Voice Mode,
  Master and the oscillator tables never mutate (a table is inherited with
  its User table), and a matrix slot's source and destination stay together.
  Children are named "<A> × <B> #k". The same parents, settings and seed give
  the same children, every value stays in range, and applying a child is
  click-free (R36). A child can be auditioned, saved as a user preset or made
  parent A for the next generation; the history keeps each generation, with
  Undo.
- R32. **Sympathetic memory.** A global stereo stage after the voice sum and
  before the FX rack: a bank of tuned strings (feedback comb / modal: a
  fractional delay with a damping low-pass in the loop) that rings along
  with what is played, like sitar or piano sympathetic strings. The strings
  are tuned to the last Memory notes (1–8; most recent first, older notes
  fade out of the body), one string per remembered note and at most Strings
  (1–12) at once, optionally snapped to the key. Amount is the wet gain
  (0 % = exact bypass); Decay sets the ring time, Brightness the tone and
  Damping the loss in each string. Loop gain is < 1 at every setting and the
  output is soft-clipped, so it cannot run away; retuning crossfades, so it
  is glitch-free; it is allocation-free and its CPU is bounded by Strings.
  Its response peaks at the remembered notes and dies away after notes stop.
- R33. **Spectral paint oscillator.** A canvas of 64 columns (time) × 128
  rows (harmonics 1–128, high at the top), amplitude 0..1 per cell with
  optional phase, painted with brush, eraser and smoothing, or filled from
  an image dropped on it (luminance → amplitude). Each column becomes one
  frame by additive / inverse-FFT synthesis, so the table has 64 frames and
  nothing above harmonic 128, and the usual mip levels band-limit it (R2).
  It becomes the oscillator's User table, scanned by Position and modulation
  like any other. Conversion runs off the audio thread; the canvas and the
  table are saved in the patch.
- R34. **Sound map browser.** Each preset is rendered offline as a short
  note and described by five features: spectral centroid, attack time,
  noisiness, movement and loudness. A deterministic PCA (power iteration from
  a fixed start, sign fixed so that x rises with the centroid) lays them out
  on a 2D map (0..1), coloured by category. Hover shows the name and, after
  300 ms, auditions the preset on a C3 note; click loads it; a click-drag
  from one point to another gives a blend slider that applies the blend
  live. Blend: Float parameters interpolate in the normalised domain, Int
  parameters round, Choice and Bool take A below 0.5 and B from 0.5, and
  user tables come from the chosen side; t = 0 gives A and t = 1 gives B.
  The map is built off the audio thread and cached in
  `<userApplicationData>/Curtis Tech Solutions/Verdant/soundmap-<hash>.json`,
  so it is recomputed only when the presets change.

### 3.8 State, threading and performance

- R35. State is APVTS XML with root `VERDANT`; any other root tag is ignored.
  A child `<EXTRAS>` element holds the patch text of everything that is not
  a parameter: name, category, tags, author, macro names, LFO shapes, user
  tables (16-bit PCM in base64, with the frame count), paint canvases (8-bit,
  base64) and the living seed and lock. A patch survives a serialize →
  deserialize round trip with bit-exact values; unknown ids are ignored on
  load. Parameter IDs and display names are frozen once the host test and
  the factory presets use them.
- R36. Applying a patch (a preset, a breed child, a blend) on the message
  thread sets every parameter through `setValueNotifyingHost`, then posts
  the extras (tables, LFO shapes, seed) to the engine through the hand-off.
  The engine's smoothing makes it click-free.
- R37. No allocation, locks, I/O or exceptions on the audio thread after
  `prepare`. Wavetables and LFO shapes reach it through a lock-free
  single-producer / single-consumer hand-off: the audio thread swaps a
  pointer and returns the retired object to a queue the message thread
  drains. Wavetable import, sample matching, paint → table and the sound map
  run as background jobs. Everything random is seeded and deterministic.
- R38. CPU: 8 voices × 4 unison × 2 oscillators with both filters render 2 s
  at 48 kHz in < 25 % of real time (Release build).

### 3.9 Editor

- R39. Base size 1120 × 720, resizable at a fixed aspect ratio (scale
  0.7–2.0, as a transform on a fixed-size content component), with crisp,
  HiDPI-clean vector drawing and an original dark theme with a Verdant-green
  accent (4.6). Nothing is copied from a commercial product.
- R40. Header (56 px, full width): logo and name; the preset bar (prev, a
  name button that opens a menu of factory and user presets by category,
  next, Save, Init); page tabs OSC · MOD · FX · ARP/SEQ · BREED · BROWSE; the
  output meter; the Alive seed re-roll / lock.
- R41. Knobs show a value popup while dragged, reset to default on
  double-click and have a right-click menu (reset, remove modulations, enter
  value), plus the modulation rings, live dot and drop target of R18.
- R42. Pages:
  - **OSC**: Osc A and Osc B panels (table selector; a stacked / 3D
    wavetable view with the current frame highlighted and moving with
    modulation; position; warp mode and amount; octave, semi, fine and
    pitch; level and pan; unison; phase; Paint, which opens the paint canvas
    for that oscillator, and Import, a WAV chooser that also takes
    drag-and-drop); Sub & Noise; Filter 1 and Filter 2 with response curves;
    the routing selector; an Env 1 strip.
  - **MOD**: Env 1–3 (editors with draggable curves); LFO 1–4 (shape menu,
    breakpoint drawing for Custom, sync and division, mode, phase, fade,
    smooth, polarity); Macros 1–8 (renameable); Physics (an animated ball /
    spring / orbit view with their knobs); Alive / Evolve; the 24-slot
    matrix list (source, destination, amount); the source chips.
  - **FX**: the 8-slot strip (drag to reorder, type menu), the selected
    effect's panel below it, and the Sympathetic panel (amount, memory,
    decay, brightness, damping, strings, snap) with a small string-activity
    display.
  - **ARP/SEQ**: the arp panel; the step grid (16 steps × pitch, velocity,
    gate and mod lanes, playing step highlighted); the global key / scale
    selector.
  - **BREED**: parent A / B pickers, mutation amount, Breed → 8 child tiles
    (click = audition and apply, star = save as user preset, "breed from
    this" = make it parent A), the generation counter and Undo.
  - **BROWSE**: preset list with category filter, tag chips, search and the
    Save dialog (name, category, tags, author); the sound map; the "Drop a
    sample to match" zone with a progress bar and Apply / Discard.
  - Every page has the output scope / spectrum in a corner (toggle).
- R43. Painting: static layers are cached in images, one shared timer
  repaints at ≤ 30 Hz, and visualisers run only while their page is shown.
- R44. A headless console target, `verdant_render <outDir>`, renders every
  page at 1× and 2× (factory preset 0, no window) to
  `<outDir>/<page>@1x.png` and `<page>@2x.png`, and runs without a display
  on Linux CI, so the design can be reviewed without a DAW. It is built when
  `AP_BUILD_PLUGINS` and `AP_BUILD_RENDER` (default ON) are on, and stays out
  of the release zip.

### 3.10 Tests and CI

- R45. Passes `verdant_tests` (one file per engine area, asserting the
  numbers in this PRD), `host_Verdant` and pluginval strictness 10 in CI on
  Windows, macOS and Linux.

## 4. Design

```
MIDI/host ─► Arp ─► StepSeq ─► VoiceAllocator (≤ 32 voices, poly / mono / legato, glide)
  per voice, in control blocks of 32 samples (render order: Sub, Osc B, Osc A):
    Sub (4 waves) ────────────────────┐
    Osc B (table, warp, unison ≤ 16) ─┤
    Osc A (table, warp, unison ≤ 16) ─┼─► route ─► F1 ─(serial)─► F2 ─► × Env 1 × 0.35 ─► pan
    Noise (5 colours) ────────────────┘           (parallel: F1 + F2; Direct skips both)
  mod: LFO 1–4, Env 1–3, Velocity, Aftertouch, Mod Wheel, Pitch Bend, Note, Random,
       Macro 1–8, Ball, Spring, Orbit, Alive, Seq Mod ─► 24-slot matrix ─► 68 destinations
Σ voices ─► Sympathetic body (global) ─► FX rack (8 slots, reorderable) ─► Master ─► out
```

`Engine::process` takes the block's MIDI events with their sample offsets.
They pass the arpeggiator and the step sequencer, then the voice allocator.
Each voice renders in control blocks of 32 samples (`kControlInterval`): its
sources advance, `ModMatrix::apply` turns the base values and the 24 routes
into modulated values, and the oscillators, filters and gains ramp to them per
sample. Each oscillator is scaled by 1/√unison and each voice by 0.35; Env 1
is the amp. The voice sum goes through the sympathetic body, the FX rack and
the 20 ms Master smoother. For the editor the engine publishes a
`VisualState` of atomics (oscillator positions, active voices, the summed
normalised offset of each destination for the last-triggered voice, physics,
envelope and LFO values, the sequencer step, the output peak) and a lock-free
ring of the last 2048 output samples for the scope.

| Files | Holds |
|---|---|
| `engine/Common.h` | `kControlInterval` 32, `kMaxVoices` 32, `kMaxUnison` 16, `kMaxBlock` 4096; `Transport`, `MidiEvent` |
| `engine/Params.h/.cpp` | the parameter table (4.1), the normalised mapping, instance helpers, the 68 destinations |
| `engine/FFT.h` | radix-2 complex FFT (power-of-two sizes ≤ 65 536) with real helpers |
| `engine/HandOff.h` | the lock-free SPSC hand-off with its retire queue (R37) |
| `engine/Wavetable.h`, `WavetableOscillator.h`, `WavetableFactory.h` | tables and mip levels (R1, R2); the oscillator with warp and unison (R3–R6); the 16 factory tables (4.3) |
| `engine/WavetableIO.h` | WAV import and export with `clm ` (R8) |
| `engine/SpectralPaint.h` | canvas → table, image → canvas, smoothing (R33) |
| `engine/SampleMatch.h` | sample-to-patch (R28) |
| `engine/Filter.h`, `Envelope.h`, `Lfo.h` | the voice filter and its response curve, the AHDSR, the LFO (R9–R13) |
| `engine/ModMatrix.h` | routes and the summing rule (R16) |
| `engine/Living.h`, `Physics.h`, `KeyAware.h` | Alive / Evolve (R27); Ball, Spring, Orbit (R29); scale snapping, `Arpeggiator`, `StepSequencer` (R30) |
| `engine/Fx.h`, `Sympathetic.h` | `FxRack` and its eight effect classes (R22, R23); the sympathetic body (R32) |
| `engine/Voice.h`, `Engine.h` | voices and allocation, `VisualState`, `renderPreview` (offline, allocates) |
| `engine/Patch.h`, `Breeding.h`, `FactoryPresets.h`, `SoundMap.h` | the patch and its text format (R35); breeding and its history (R31); the factory bank (R24); features, PCA and blend (R34) |
| `PluginProcessor`, `PresetManager`, `BackgroundTasks` | APVTS from the parameter table, state, engine driving; factory and user presets; off-thread jobs (import, sample match, paint → table, sound map) |
| `PluginEditor`, `ui/` | header, page host and resizing; `Theme`, `ModKnob`, the six pages and the visualisers (4.6) |
| `render/RenderPages.cpp` | `verdant_render` (R44) |

The engine is plain C++ with no JUCE, built once as the static lib
`verdant_engine` (the `plugins/Frisson/modules/` pattern) and linked into the
plugin and into `verdant_tests` (`tests/verdant/`, harness
`tests/TestHarness.h`). Both `engine/*.cpp` and `tests/verdant/test_*.cpp` are
globbed, so a new module or test file needs no CMake edit.

### 4.1 Parameters (347)

`engine/Params.h/.cpp` is the single source of truth; the processor builds its
APVTS layout from it with the same ranges and centres, so host, UI and engine
agree. Kind: Float (plain units), Int, Choice (stored as its index) or Bool.
"(centre x)" marks a skewed knob with x at mid-travel (the
`juce::NormalisableRange::setSkewForCentre` mapping); the other ranges are
linear. That skewed 0..1 knob travel is the normalised domain the matrix
works in (R16). Mod = a matrix destination.

Instanced groups are written once with a prefix: `{o}` = `a` / `b` (Osc A /
Osc B), `{f}` = `f1` / `f2`, `{e}` = `env1` / `env2` / `env3`, `{l}` =
`lfo1` … `lfo4`, and `{n}` is a macro, slot or step number (`macro{n}` 1–8,
`mod{n}` 1–24, `seq{n}` 1–16, `fx{n}` 1–8). For the prefixed groups the Name
column is the label inside the group's panel; the host sees it qualified with
the instance ("Osc A Position", "Filter 2 Cutoff", "Env 1 Attack", "LFO 3
Rate"). Numbered rows give the host name with n filled in ("Mod 7 Source",
"Step 12 Pitch", "FX Slot 3"). Every other parameter uses its name as listed
("Master", "Delay Mix", "Alive"). The groups below drive the editor panels
and breeding crossover.

**Global** (8)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `voices` | Voices | Int | 1–32 | 16 | | polyphony limit (R19) |
| `voice_mode` | Voice Mode | Choice | Poly, Mono, Legato | Poly | | |
| `glide` | Glide | Float | 0–2000 ms (centre 100) | 0 | | |
| `bend_range` | Bend Range | Int | 0–24 st | 2 | | |
| `master` | Master | Float | −60 – +12 dB | −6 | yes | smoothed over 20 ms |
| `pitch` | Pitch | Float | −48 – +48 st | 0 | yes | both oscillators |
| `key_root` | Key | Choice | C, C#, D, D#, E, F, F#, G, G#, A, A#, B | C | | global key (R30) |
| `key_scale` | Scale | Choice | Chromatic, Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major Pentatonic, Minor Pentatonic, Blues, Whole Tone | Chromatic | | the 14 `ap::ScaleType` scales, no Custom |

**Osc A / Osc B** (`{o}` = `a` / `b`; 2 × 18)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `{o}_on` | On | Bool | off / on | A on, B off | | |
| `{o}_table` | Table | Choice | the 16 factory tables (4.3), User | Basic Shapes | | User = this oscillator's imported, painted or matched table |
| `{o}_pos` | Position | Float | 0–100 % | 0 | yes | |
| `{o}_warp_mode` | Warp | Choice | Off, Sync, Bend, Squeeze, Mirror, FM Osc, FM Sub, Remap | Off | | R3 |
| `{o}_warp` | Warp Amount | Float | −100 – +100 % | 0 | yes | |
| `{o}_oct` | Octave | Int | −3 – +3 | 0 | | |
| `{o}_semi` | Semi | Int | −12 – +12 | 0 | | |
| `{o}_fine` | Fine | Float | −100 – +100 ct | 0 | yes | |
| `{o}_pitch` | Pitch | Float | −48 – +48 st | 0 | yes | |
| `{o}_level` | Level | Float | 0–100 % | 80 | yes | |
| `{o}_pan` | Pan | Float | −100 – +100 % | 0 | yes | |
| `{o}_uni_voices` | Unison | Int | 1–16 | 1 | | |
| `{o}_uni_detune` | Detune | Float | 0–100 ct | 15 | yes | |
| `{o}_uni_spread` | Spread | Float | 0–100 % | 50 | yes | |
| `{o}_uni_blend` | Blend | Float | 0–100 % | 75 | yes | |
| `{o}_phase` | Phase | Float | 0–360° | 0 | | |
| `{o}_rand_phase` | Random Phase | Float | 0–100 % | 100 | | |
| `{o}_route` | Route | Choice | Filter 1, Filter 2, Both, Direct | Filter 1 | | R10 |

**Sub & Noise** (7)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `sub_level` | Sub Level | Float | 0–100 % | 0 | yes | |
| `sub_wave` | Sub Wave | Choice | Sine, Triangle, Square, Saw | Sine | | |
| `sub_oct` | Sub Octave | Choice | 0, −1, −2 | −1 | | octaves below the note |
| `sub_route` | Sub Route | Choice | Filter 1, Filter 2, Both, Direct | Direct | | |
| `noise_level` | Noise | Float | 0–100 % | 0 | yes | |
| `noise_color` | Noise Colour | Choice | White, Pink, Brown, Blue, Violet | White | | |
| `noise_route` | Noise Route | Choice | Filter 1, Filter 2, Both, Direct | Filter 1 | | |

**Filter 1 / Filter 2** (`{f}` = `f1` / `f2`; 2 × 7 + 1)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `{f}_on` | On | Bool | off / on | F1 on, F2 off | | off = bypass |
| `{f}_type` | Type | Choice | LP 12, LP 24, HP 12, HP 24, BP 12, BP 24, Notch, Comb +, Comb −, Formant, Ladder | LP 24 | | R9 |
| `{f}_cutoff` | Cutoff | Float | 20–20 000 Hz (centre √(20 · 20 000) ≈ 632) | 12 000 | yes | |
| `{f}_res` | Resonance | Float | 0–100 % | 15 | yes | |
| `{f}_drive` | Drive | Float | 0–100 % | 0 | yes | |
| `{f}_key` | Key Track | Float | 0–100 % | 0 | | 100 % = 1 oct/oct about C4 |
| `{f}_mix` | Mix | Float | 0–100 % | 100 | yes | |
| `filter_routing` | Filter Routing | Choice | Serial, Parallel | Serial | | one parameter, in group "Filter 1" |

**Env 1 / Env 2 / Env 3** (`{e}` = `env1` / `env2` / `env3`; Env 1 = amp; 3 × 7)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `{e}_a` | Attack | Float | 0–10 000 ms (centre 250) | 5 | yes | latched at note-on |
| `{e}_h` | Hold | Float | 0–5000 ms (centre 100) | 0 | | |
| `{e}_d` | Decay | Float | 1–10 000 ms (centre 400) | 300 | yes | latched at note-on |
| `{e}_s` | Sustain | Float | 0–100 % | 80 / 0 / 0 | | Env 1 / 2 / 3 |
| `{e}_r` | Release | Float | 1–10 000 ms (centre 400) | 300 | yes | latched at note-on |
| `{e}_acurve` | Attack Curve | Float | −100 – +100 % | 0 | | > 0 fast start, < 0 slow start |
| `{e}_dcurve` | Decay Curve | Float | −100 – +100 % | −40 | | < 0 exponential, > 0 inverse; shapes the release too |

**LFO 1–4** (`{l}` = `lfo1` … `lfo4`; 4 × 9)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `{l}_shape` | Shape | Choice | Sine, Triangle, Saw Up, Saw Down, Square, S&H, Custom | Sine | | Custom = breakpoints in the patch |
| `{l}_rate` | Rate | Float | 0.01–50 Hz (centre 1) | 1 | yes | |
| `{l}_sync` | Sync | Bool | off / on | off | | |
| `{l}_div` | Division | Choice | 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4 T, 1/8 T, 1/16 T, 1/2 D, 1/4 D, 1/8 D | 1/4 | | the 12 `ap::NoteDivision` names |
| `{l}_mode` | Mode | Choice | Retrig, Free, One-shot | Retrig | | |
| `{l}_phase` | Start Phase | Float | 0–100 % | 0 | | |
| `{l}_fade` | Fade In | Float | 0–5000 ms (centre 250) | 0 | | |
| `{l}_smooth` | Smooth | Float | 0–100 % | 0 | | |
| `{l}_polarity` | Polarity | Choice | Bipolar, Unipolar | Bipolar | | |

**Macros** (8)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `macro{n}` | Macro n | Float | 0–100 % | 0 | | sources, not destinations |

**Matrix** (24 × 3)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `mod{n}_src` | Mod n Source | Choice | the 27 sources (4.2) | None | | |
| `mod{n}_dst` | Mod n Dest | Choice | None + the 68 destinations (4.2) | None | | |
| `mod{n}_amt` | Mod n Amount | Float | −100 – +100 % | 0 | | −1..1 in the normalised domain |

**Physics** (6)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `ball_gravity` | Ball Gravity | Float | 0–100 % | 50 | | |
| `ball_bounce` | Ball Bounce | Float | 0–100 % | 70 | | |
| `spring_stiffness` | Spring Stiffness | Float | 0–100 % | 50 | | |
| `spring_damping` | Spring Damping | Float | 0–100 % | 30 | | |
| `orbit_speed` | Orbit Speed | Float | 0–100 % | 50 | | |
| `orbit_shape` | Orbit Shape | Float | 0–100 % | 30 | | |

**Alive** (2)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `alive` | Alive | Float | 0–100 % | 15 | | per-note variation (R27) |
| `evolve` | Evolve | Float | 0–100 % | 0 | | drift over held notes |

**Arp** (7)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `arp_on` | Arp | Bool | off / on | off | | |
| `arp_mode` | Arp Mode | Choice | Up, Down, Up-Down, Down-Up, Random, As Played, Chord | Up | | |
| `arp_oct` | Arp Octaves | Int | 1–4 | 1 | | |
| `arp_div` | Arp Rate | Choice | the note divisions (as `{l}_div`) | 1/8 | | |
| `arp_gate` | Arp Gate | Float | 5–100 % | 80 | | |
| `arp_swing` | Arp Swing | Float | 0–100 % | 0 | | |
| `arp_snap` | Arp Snap to Key | Bool | off / on | off | | |

**Seq** (6 + 16 × 4)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `seq_on` | Seq | Bool | off / on | off | | |
| `seq_steps` | Seq Steps | Int | 1–16 | 8 | | |
| `seq_div` | Seq Rate | Choice | the note divisions | 1/16 | | |
| `seq_gate` | Seq Gate | Float | 5–100 % | 80 | | |
| `seq_swing` | Seq Swing | Float | 0–100 % | 0 | | |
| `seq_snap` | Seq Snap to Key | Bool | off / on | on | | |
| `seq{n}_pitch` | Step n Pitch | Int | −24 – +24 st | 0 | | scale degrees when snapped |
| `seq{n}_vel` | Step n Velocity | Float | 0–100 % | 100 | | |
| `seq{n}_gate` | Step n Gate | Bool | off / on | on | | |
| `seq{n}_mod` | Step n Mod | Float | 0–100 % | 50 | | the Seq Mod source |

**FX** (8 + 44)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `fx{n}_type` | FX Slot n | Choice | None, Distortion, Chorus, Phaser, Flanger, Delay, Reverb, EQ, Compressor | None | | R22 |
| `dist_type` | Dist Type | Choice | Soft, Hard, Tube, Fold, Crush, Diode | Soft | | |
| `dist_drive` | Dist Drive | Float | 0–100 % | 30 | yes | |
| `dist_tone` | Dist Tone | Float | 200–20 000 Hz (centre 3000) | 8000 | yes | |
| `dist_mix` | Dist Mix | Float | 0–100 % | 100 | yes | |
| `chorus_rate` | Chorus Rate | Float | 0.05–10 Hz (centre 1) | 0.8 | yes | |
| `chorus_depth` | Chorus Depth | Float | 0–100 % | 40 | yes | |
| `chorus_feedback` | Chorus Feedback | Float | 0–100 % | 10 | | |
| `chorus_mix` | Chorus Mix | Float | 0–100 % | 50 | yes | |
| `phaser_rate` | Phaser Rate | Float | 0.02–10 Hz (centre 0.5) | 0.3 | yes | |
| `phaser_depth` | Phaser Depth | Float | 0–100 % | 60 | yes | |
| `phaser_feedback` | Phaser Feedback | Float | 0–100 % | 30 | | |
| `phaser_stages` | Phaser Stages | Choice | 4, 6, 8, 12 | 6 | | |
| `phaser_mix` | Phaser Mix | Float | 0–100 % | 50 | yes | |
| `flanger_rate` | Flanger Rate | Float | 0.02–10 Hz (centre 0.5) | 0.25 | yes | |
| `flanger_depth` | Flanger Depth | Float | 0–100 % | 50 | yes | |
| `flanger_feedback` | Flanger Feedback | Float | −100 – +100 % | 40 | | |
| `flanger_mix` | Flanger Mix | Float | 0–100 % | 50 | yes | |
| `delay_time` | Delay Time | Float | 1–2000 ms (centre 200) | 375 | yes | when not synced |
| `delay_sync` | Delay Sync | Bool | off / on | off | | |
| `delay_div` | Delay Division | Choice | the note divisions | 1/8 | | when synced |
| `delay_feedback` | Delay Feedback | Float | 0–100 % | 35 | yes | |
| `delay_pingpong` | Ping-Pong | Bool | off / on | on | | |
| `delay_lowcut` | Delay Low Cut | Float | 20–2000 Hz (centre 200) | 100 | | |
| `delay_highcut` | Delay High Cut | Float | 1000–20 000 Hz (centre 5000) | 8000 | | |
| `delay_mix` | Delay Mix | Float | 0–100 % | 30 | yes | |
| `reverb_size` | Reverb Size | Float | 0–100 % | 60 | yes | |
| `reverb_decay` | Reverb Decay | Float | 0–100 % | 50 | yes | |
| `reverb_damp` | Reverb Damping | Float | 0–100 % | 40 | | |
| `reverb_predelay` | Reverb Pre-delay | Float | 0–200 ms (centre 40) | 10 | | |
| `reverb_width` | Reverb Width | Float | 0–100 % | 100 | | |
| `reverb_mix` | Reverb Mix | Float | 0–100 % | 25 | yes | |
| `eq_low_gain` | EQ Low | Float | −18 – +18 dB | 0 | yes | |
| `eq_low_freq` | EQ Low Freq | Float | 30–500 Hz (centre 120) | 120 | | |
| `eq_mid_gain` | EQ Mid | Float | −18 – +18 dB | 0 | yes | |
| `eq_mid_freq` | EQ Mid Freq | Float | 200–8000 Hz (centre 1000) | 1000 | yes | |
| `eq_mid_q` | EQ Mid Q | Float | 0.3–8 (centre 1) | 0.8 | | |
| `eq_high_gain` | EQ High | Float | −18 – +18 dB | 0 | yes | |
| `eq_high_freq` | EQ High Freq | Float | 2000–18 000 Hz (centre 6000) | 8000 | | |
| `comp_threshold` | Comp Threshold | Float | −60 – 0 dB | −18 | | |
| `comp_ratio` | Comp Ratio | Float | 1–20 :1 (centre 4) | 4 | | |
| `comp_attack` | Comp Attack | Float | 0.1–100 ms (centre 10) | 10 | | |
| `comp_release` | Comp Release | Float | 10–1000 ms (centre 100) | 100 | | |
| `comp_makeup` | Comp Makeup | Float | 0–24 dB | 0 | | |
| `comp_mix` | Comp Mix | Float | 0–100 % | 100 | | |

**Sympathetic** (7; a global stage before the FX rack)

| ID | Name | Kind | Range | Default | Mod | Notes |
|---|---|---|---|---|---|---|
| `symp_amount` | Sympathetic | Float | 0–100 % | 0 | yes | 0 = exact bypass |
| `symp_memory` | Memory | Int | 1–8 notes | 4 | | |
| `symp_decay` | Symp Decay | Float | 0–100 % | 50 | yes | |
| `symp_bright` | Symp Brightness | Float | 0–100 % | 50 | yes | |
| `symp_damp` | Symp Damping | Float | 0–100 % | 30 | | |
| `symp_strings` | Strings | Int | 1–12 | 6 | | |
| `symp_snap` | Symp Snap to Key | Bool | off / on | off | | |

Total: 347 parameters (Global 8, Osc 2 × 18, Sub & Noise 7, Filters 2 × 7 + 1,
Envelopes 3 × 7, LFOs 4 × 9, Macros 8, Matrix 24 × 3, Physics 6, Alive 2,
Arp 7, Seq 6 + 16 × 4, FX 8 + 44, Sympathetic 7), of which 68 are modulation
destinations (4.2).

### 4.2 Modulation sources and destinations

Sources (the `mod{n}_src` choices, in order, 27 entries): None, LFO 1, LFO 2,
LFO 3, LFO 4, Env 1, Env 2, Env 3, Velocity, Aftertouch, Mod Wheel, Pitch
Bend, Note, Random, Macro 1 … Macro 8, Ball, Spring, Orbit, Alive, Seq Mod.
Ranges are in R14. Modulation-ring colours: LFOs `#5AD0FF`, envelopes
`#F28CB1`, macros and performance sources `#F2B134`, physics and Alive
`#B48CF2`.

Destinations (the `mod{n}_dst` choices: None, then these 68 in parameter-table
order, with qualified names such as "Osc A Position" or "Filter 2 Cutoff"):

| Group | Destinations | Count |
|---|---|---|
| Global | Master, Pitch | 2 |
| Osc A, Osc B | Position, Warp Amount, Fine, Pitch, Level, Pan, Detune, Spread, Blend | 2 × 9 |
| Sub & Noise | Sub Level, Noise | 2 |
| Filter 1, Filter 2 | Cutoff, Resonance, Drive, Mix | 2 × 4 |
| Env 1–3 | Attack, Decay, Release (latched at note-on) | 3 × 3 |
| LFO 1–4 | Rate | 4 × 1 |
| FX | Dist Drive, Dist Tone, Dist Mix, Chorus Rate, Chorus Depth, Chorus Mix, Phaser Rate, Phaser Depth, Phaser Mix, Flanger Rate, Flanger Depth, Flanger Mix, Delay Time, Delay Feedback, Delay Mix, Reverb Size, Reverb Decay, Reverb Mix, EQ Low, EQ Mid, EQ Mid Freq, EQ High | 22 |
| Sympathetic | Sympathetic, Symp Decay, Symp Brightness | 3 |

Master, Pitch, the FX and the Sympathetic destinations (27) are global (R16);
the other 41 are per voice.

### 4.3 Factory content

Wavetables (the `{o}_table` choices 0–15; 16 = User): Basic Shapes, Analog
Saws, Pulse Width, Harmonic Stack, Odd Harmonics, Formant Vowels, Bell, Organ,
Digital Glass, FM Sweep, Sync Sweep, Noise Bloom, Growl, Pluck Decay, Choir,
Sub Bass. All are built additively in code (deterministic, no third-party
content), and each is non-silent with peak ≤ 1. Basic Shapes morphs smoothly
over ≥ 16 frames from a sine (frame 0) through triangle, saw and square to
narrowing pulses.

Presets: 40 ± 10 from `factoryPresets()` (R24). Factory preset 0 is what the
render target draws; the host test loads preset 1.

### 4.4 FX rack

Types (the `fx{n}_type` choices): None, Distortion, Chorus, Phaser, Flanger,
Delay, Reverb, EQ, Compressor. Distortion curves: Soft, Hard, Tube, Fold,
Crush, Diode. `FxRack` takes the modulated parameter values and the transport
once per block, runs the slots in order and skips repeated types (R22). The
effect classes (`Distortion`, `Chorus`, `Phaser`, `Flanger`, `StereoDelay`,
`Reverb`, `ThreeBandEq`, `Compressor`) are public so the tests target them
directly. The rack's tail feeds the reported tail (R21).

### 4.5 State and files

- Plugin state: APVTS root `VERDANT` with the `<EXTRAS>` child (R35). Patch
  text is line-oriented: `verdant-patch 1`, `name …`, one
  `param <id> <value>` line per parameter in plain units, and
  `blob <key> <base64>` lines for tables, canvases and shapes.
- Presets: a `.vrdt` file is the same XML. User presets live in
  `<userDocuments>/Curtis Tech Solutions/Verdant/Presets/<Category>/<Name>.vrdt`
  (JUCE `File::userDocumentsDirectory`); factory presets come from code and
  are listed first.
- Sound-map cache:
  `<userApplicationData>/Curtis Tech Solutions/Verdant/soundmap-<hash>.json`.

### 4.6 Editor

| Theme role (`ui/Theme.h`) | Colour |
|---|---|
| Background | `#0F1114` |
| Panel / panel edge | `#171A1F` / `#242932` |
| Text / dim text | `#E8ECEF` / `#8A939E` |
| Accent (Verdant green) / accent dim | `#3DDC97` / `#1E6B4B` |
| Warning | `#F2B134` |
| Modulation rings | LFO `#5AD0FF`, Env `#F28CB1`, macro and performance `#F2B134`, physics and Alive `#B48CF2` |

Fonts: JUCE default sans, 11–20 px. The content is a fixed 1120 × 720
component, scaled by a transform (0.7–2.0): the 56 px header (R40) above a
page host that shows one of the six pages (R42). Pages are `Page` components
from `makeOscPage`, `makeModPage`, `makeFxPage`, `makeArpSeqPage`,
`makeBreedPage` and `makeBrowsePage` (`ui/Pages.h`), told when they are shown
or hidden so their visualisers can stop. Visualisers: WavetableView,
FilterResponseView, LfoEditor, EnvelopeEditor, PhysicsView, Scope (scope /
spectrum), SoundMapView, PaintCanvas and StepGrid. `ModKnob` (R41) is the one
knob used everywhere.

## 5. Acceptance tests

- `host_Verdant` (`tests/host/VST3HostTest.cpp`, tag `Verdant`, 48 kHz / 512,
  `enableAllBuses()` like Strata):
  - `commonChecks`: latency ≥ 0; silence gives finite output; a non-empty
    state blob that restores and keeps processing (R21, R35).
  - `testVerdant`: C4 at velocity 100 on the default patch is finite, with
    RMS > 0.01 and YIN 261.63 ± 4 Hz (R5). After note-off plus 300 blocks
    the last 4 blocks are < 1e-4 RMS (R21). A C–E–G chord has > 1.2 × the
    RMS of one note (R19). Voices = 1 steals: the same Goertzel check as
    Strata, the second note's band > 4 × the first's (R19). Factory preset 1
    loaded through the program list (`getNumPrograms()` = the number of
    factory presets, `setCurrentProgram`) still gives finite sound (R24).
- `verdant_tests` (JUCE-free, `tests/verdant/test_*.cpp`, one file per
  engine area):
  - FFT: forward → inverse round trip (`test_fft.cpp`).
  - Wavetables: a saw table at MIDI 108, 44.1 kHz has aliased energy
    < −60 dB (R2); a position sweep has no sample discontinuity > 0.1 (R1);
    unison count, level and stereo (R4); every factory table is non-silent
    with peak ≤ 1, and Basic Shapes frame 0 ≈ a sine (4.3).
  - WAV import, including the `clm ` chunk and non-2048 frame sizes (R8).
  - Paint → wavetable: frame count and harmonic limit (R33).
  - Sample-to-patch: on a synthetic decaying bright saw the pitch is within
    1 % and Filter 1 cutoff is above a dull saw's (R28).
  - Filters: no NaN or blow-up for cutoff / resonance sweeps at 44.1 kHz and
    192 kHz in every type; LP 24 slope ≈ −24 dB/oct (R9, R11).
  - Envelopes: times within 5 %, monotonic curves (R13). LFOs: shapes,
    synced period, one-shot (R12).
  - Mod matrix: routing, clamping and smoothing, with no step > 1 % per
    sample on a cutoff jump (R16, R17).
  - Engine: polyphony limit and stealing, legato, glide time, pitch accuracy
    (YIN within 0.5 %), release decays below 1e-4, and the same seed gives a
    bit-identical render (R19, R21, R27).
  - Living offsets within the R27 bounds and deterministic. Physics bounded
    and deterministic, and the ball comes to rest (R29).
  - Key-aware: scale snapping, arp order and timing, sequencer timing at
    120 bpm within 1 sample (R30).
  - FX: stability and the synced delay time (R23). Sympathetic: the peak
    sits at the remembered note and the body decays after notes stop (R32).
  - Breeding: determinism, range safety and group inheritance (R31).
  - Patch: serialize → deserialize round trip with bit-exact values (R35).
  - Factory presets: every one renders non-silent and unclipped (R24).
  - Sound map: deterministic, and `blendPatches` end points (R34).
- CPU budget (in `verdant_tests`): 8 voices × 4 unison × 2 oscillators with
  both filters, 2 s at 48 kHz, in < 25 % of real time (R38). It is measured in
  Release; the timing assertion is skipped when `NDEBUG` is not defined.
- CI: `Verdant` is in the `PLUGINS` list of `build-plugins.yml`, so pluginval
  strictness 10 runs on Windows, macOS and Linux, and the `ci.yml` package
  check expects `Verdant.vst3` in every platform zip (R45).
- Headless render: `verdant_render <outDir>` writes `<page>@1x.png` and
  `<page>@2x.png` for all six pages from factory preset 0, with no display
  (Linux CI), for design review (R44).

## 6. Out of scope / later

- MPE and per-note expression beyond polyphonic aftertouch; AU and CLAP;
  macOS Developer-ID signing and notarisation (CI signs ad hoc only).
- Oversampling: distortion, warp and filter drive run at the host rate in
  v0.1, which is why R2's figure is for Warp Off.
- Key-snapped pitch modulation from the matrix (v0.1 snaps the arp, the
  sequencer and the sympathetic strings), a Custom scale mask (Intone's
  12-note scale) and microtuning (Scala, MTS-ESP).
- A pendulum physics source next to Orbit, and a separate filter-envelope
  estimate in sample matching (v0.1 carries the change of timbre over time in
  the table scan).
- A preset cloud, online sharing and preset packs.
- A full wavetable editor (per-frame drawing, formula tables, table export
  from the UI), more matrix slots, modulating matrix amounts, per-voice FX
  and multiple outputs.
- The engine is built so each signature feature is a separately tested
  module (`Living`, `SampleMatch`, `Physics`, `KeyAware`, `Breeding`,
  `Sympathetic`, `SpectralPaint`, `SoundMap`, each with its own test file),
  so any of them can be improved or replaced later without touching the rest.
