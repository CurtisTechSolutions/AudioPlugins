# Flagship wavetable synth — build brief

Status: approved scope, not started. This brief is the input for the PRD
(`docs/prd/<name>.md`), which the build writes first, in the same style as
`docs/prd/strata.md`, and registers in `docs/prd/README.md`. Delete this brief
once the PRD supersedes it.

## 1. What it is

A new flagship VST3 Instrument|Synth that sits **alongside** Strata (Strata
stays the simple subtractive synth). It is in the class of the big modern
wavetable synths (Serum, Massive, Pigments) but is an **original product**:
its own name, sound, UI and factory content. Do not ship or copy any
third-party wavetables, presets, graphics or trade dress; generate factory
wavetables procedurally/additively.

- Pick an evocative working-title name in the repo's naming style
  (VoxNote, Intone, Strata, Ducker, Frisson), marked as a placeholder like
  Strata's, with a new unique 4-char `PLUGIN_CODE`.
- Targets: VST3 on Windows and macOS, plus Linux in CI like the others.
- Reuse `core/include/ap/*` DSP (filters, smoothers, saturation, delay,
  dynamics, scales, tempo, pink noise) and `common/` UI where it fits; extend
  them only additively and keep every existing plugin's behaviour unchanged.

## 2. Core engine (v1)

- **Oscillators:** 2 wavetable oscillators with smooth frame morphing
  (position); mipmapped, band-limited tables (per octave, crossfaded) so
  nothing aliases audibly; per-osc warp modes (at least sync, bend ±,
  PWM-style squeeze, mirror, FM from the other osc or the sub, remap curve);
  unison up to 16 with detune / spread / blend; phase and random-phase.
- **Sub and noise** oscillators (several noise colours).
- **Wavetable import:** standard WAV files, including the common `clm `
  chunk convention for frame size (default 2048 samples per frame). Loaded
  off the audio thread.
- **Filters:** 2 filters, serial or parallel; LP/HP/BP 12 and 24 dB, notch,
  comb, formant, ladder-style LP; drive; key tracking.
- **Modulation:** 4 LFOs (incl. drawable/breakpoint shapes, tempo sync,
  retrig / free / one-shot), 3 envelopes with curve controls, velocity,
  aftertouch, mod wheel, note, random-per-note, and 4–8 macros. A full mod
  matrix with drag-to-modulate: drag a source onto any knob and a depth ring
  appears, with a live indicator showing the modulated value. Modulation must
  stay cheap across voices × unison and be smoothed (no zipper noise).
- **Voices:** polyphony 16+, mono/legato, glide.
- **FX rack** with reorderable slots: distortion (several curves), chorus,
  phaser, flanger, delay (tempo sync, ping-pong), reverb, EQ, compressor.
- **Presets:** save/load, categories and tags, init patch, an in-UI browser,
  and a deliberately designed factory bank of ~30–50 presets that show off
  the features (basses, leads, pads, plucks, keys, FX, evolving textures).

## 3. Signature features (all first-class)

1. **Living sound engine.** Per-note analog-style drift and controlled
   variation (pitch, wavetable position, filter, attack timing, stereo) with
   an "Alive" amount and a lockable / re-rollable seed, plus slow evolution
   over held notes so sustained sounds develop instead of looping. Subtle and
   musical at low settings; deterministic for a given seed.
2. **Sample-to-patch match.** Drop an audio sample on the plugin; it analyses
   pitch and spectral frames, builds a wavetable from the sample's timbre over
   time, estimates amplitude/filter envelopes and brightness, and produces a
   starting patch that gets close. Runs off the audio thread with progress
   feedback.
3. **Physics mod sources.** Mod sources driven by small simulations — bouncing
   ball (gravity, elasticity; retriggered by notes, velocity sets drop
   height), damped spring (excited by note-on), orbit/pendulum — usable
   anywhere in the matrix and animated in the UI.
4. **Key-aware modulation.** A global key/scale (reuse Intone's
   `core/include/ap/Scales.h`). Pitch modulation, a built-in arpeggiator and a
   small tempo-synced step sequencer can snap to the scale ("snap to key").
5. **Patch breeding.** Pick two presets as parents and generate 6–8 offspring
   by crossing parameter groups plus small, musically scaled mutations
   (respecting ranges and curves; mutation-amount control). Audition children,
   save favourites, breed again from any child; generation history with undo.
   Deterministic for a given seed; applying a child is click-free.
6. **Sympathetic memory.** A stereo resonator body (tuned comb/modal bank)
   whose tunings follow the last N notes played, with a memory length so older
   notes fade out of the body, optionally snapped to the key. Controls: amount,
   memory length, decay, brightness, damping, number of strings/modes. Rings
   along like sitar or piano sympathetic strings. Stable (no runaway feedback),
   CPU-bounded, allocation-free, glitch-free retuning. The PRD decides whether
   it is a special FX slot or a post-voice global stage.
7. **Spectral paint oscillator.** A canvas in time × harmonic space (brushes,
   eraser, smoothing; or drop in an image) converted into a wavetable — each
   column becomes a frame via additive / inverse-FFT synthesis, band-limited
   per mipmap — selectable for either oscillator and scannable by position and
   modulation. Conversion off the audio thread; painted tables are saved in
   the preset state.
8. **Sound map browser.** Presets laid out on a 2D map by sonic similarity
   (render a short note per preset offline, extract features such as spectral
   centroid, attack, noisiness and movement, reduce to 2D deterministically,
   e.g. PCA or MDS). Click between two presets to blend them (parameter
   interpolation with sensible handling of discrete parameters); hover to
   audition. The map is cached and recomputed when presets change.

## 4. UI — it has to look as good as the competition

- A polished, modern, original design on par with the top commercial
  wavetable synths: custom dark theme with one strong accent colour (distinct
  from Strata's amber), crisp vector rendering, consistent spacing and
  typography, custom knobs with modulation rings, HiDPI, resizable at a fixed
  aspect ratio.
- Live visualisers: 3D/stacked wavetable view with the current frame moving
  with modulation; filter response curve; drawable LFO/envelope editors;
  animated physics sources; output oscilloscope/spectrum.
- Paged layout (e.g. OSC / MOD / FX / ARP-SEQ / BREED / BROWSER-MAP),
  drag-to-modulate, right-click menus, double-click to reset, value popups.
- Efficient painting: cache static layers, throttle repaints.
- Add a small headless target that renders each editor page to PNG at 1× and
  2× so the design can be reviewed and iterated without a DAW.

## 5. Tests and CI

- Wire the plugin into the root CMake, `ci.yml`, `build-plugins.yml` and
  pluginval (strictness 10) wherever the other plugins are listed.
- Host test in `tests/host/VST3HostTest.cpp` following the existing pattern
  (pitch check, finite output, state round-trip, release decays).
- Unit tests for: wavetable band-limiting, morph continuity, unison, filter
  stability, mod-matrix routing and smoothing, living-engine determinism,
  physics sources bounded and deterministic, scale snapping, arp/seq timing,
  WAV import incl. `clm `, sample-to-patch on synthetic signals, breeding
  determinism and range safety, sympathetic-memory stability and tuning,
  paint→wavetable band-limiting and frame count, sound-map determinism and
  blend round-trip, preset save/load round-trip, and a CPU budget check
  (8 voices × 4 unison clearly faster than realtime in Release).
- No allocations or locks on the audio thread.

## 6. How to build it

- Orchestration: **Fable** plans, splits the work, reviews and integrates;
  **Opus 5.5** executors implement the pieces in parallel with clear file
  ownership. Only the orchestrator pushes.
- Branch: `feat/flagship-synth`. Independent of `feat/hum-to-track`; keep
  edits to shared files (root CMake, workflows, test CMake, host test) small
  and additive so the two merge cleanly.
- PR title per `CONTRIBUTING.md` (Conventional Commits), e.g.
  `feat(<name>): add flagship wavetable synth`. Open the PR; don't merge.
