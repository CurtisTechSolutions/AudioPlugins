# Frisson — PRD (umbrella)

## 1. Summary

Frisson is a nine-knob "sound enhancer" for mix buses and individual tracks.
Where Soundgoodizer-style tools hide a fixed recipe behind one knob, Frisson
exposes each ingredient as its own knob, named after the listener response it
is reaching for, and lets the user build the recipe. A master DOSE knob scales
the whole recipe; a MATCH button gives a loudness-matched bypass so users judge
the effect at equal loudness instead of being fooled by "louder sounds better".

Audience: producers and mix engineers who want a fast "make it feel finished"
tool that is honest about what it does. Format: VST3 (Windows/macOS), same
repo and toolchain as the other plugins.

## 2. The response it targets

Musical pleasure is driven by expectation and its resolution (Salimpoor 2011;
Ferreri 2019), chills by sudden change and crescendo (Sloboda 1991; Grewe 2007;
Huron 2006), groove by predictable-but-not-rigid rhythm (Witek 2014), and
"aliveness" by 1/f variation (Voss & Clarke 1975; Levitin 2012). A processor
cannot write the music, so Frisson works the micro-level cues the brain uses to
judge a sound as alive, full and finished. Each knob maps to one cue; the
mapping is documented in its own PRD. No claim about neurochemistry is made in
the UI or marketing copy beyond naming.

## 3. Requirements

- R1. Nine independent knobs, 0–100 %, each exactly bypassed at 0.
- R2. A master DOSE knob (0–100 %) that scales every knob's depth and the
  dry/wet blend; at DOSE 0 the plugin is a bit-exact (latency-only) bypass.
- R3. A MATCH button: when on, the plugin outputs the dry signal gain-matched
  to the processed signal's short-term loudness (BS.1770), so A/B is fair.
  The UI shows the loudness difference the processing introduced, in dB.
- R4. Each knob has an activity indicator the UI updates at ≥ 20 Hz.
- R5. Constant latency regardless of settings; reported to the host. Target
  ≤ 5 ms at 48 kHz.
- R6. Level independent: works the same on a −30 dBFS track and a −6 dBFS mix.
- R7. CPU: the full chain at all knobs 50 % must stay under 3 % of one core at
  48 kHz / 256 samples on a 2020-era laptop (measured via the host test timing).
- R8. Passes pluginval at strictness 10 and the repo's VST3 host test.
- R9. No allocations, locks or I/O on the audio thread after prepare.
- R10. Mono and stereo in/out; mono input is processed as dual mono.

## 4. Design

### 4.1 Chain

```
in ─┬─► WEIGHT ► WARMTH ► FOCUS ► GROOVE ► SHINE ► SPACE ► RUSH ─► gainL/R ─► blend ─► MATCH ─► out
    │                                                                                ▲
    ├─► CHILLS (analyses dry) ──┐                                                    │
    └─► LIFE   (time only) ─────┴─► Modulation.combined() ─► setModulation on each ──┘
    └─► dry delayed by chain latency ─────────────────────────────────────────────────┘
```

Seven **ingredients** process audio in place in the fixed order above. Two
**modulators** read the dry input once per block and return a `Modulation`
(see `plugins/Frisson/modules/Module.h`); the chain combines them and hands the
result to every ingredient before processing. Modulators only move parameters
of ingredients that are already active: CHILLS cannot add air if SHINE is 0.

### 4.2 Module contract

`plugins/Frisson/modules/Module.h` defines `frisson::Module`, `frisson::Modulator`,
`frisson::Modulation` and `frisson::Transport`. Modules are plain C++ on top of
`core/include/ap/` (no JUCE) and compile into both the plugin and
`frisson_tests`. Shared DSP available in core: `Crossover.h` (LR4, 3-band),
`Saturation.h` (TubeShaper, ExciterShaper, DcBlocker), `TransientDetector.h`,
`Loudness.h` (BS.1770 meter), `PinkNoise.h` (PinkNoise, DriftSource),
`DelayLine.h`, `Filters.h` (Biquad, Smoother), `Dynamics.h` (EnvelopeFollower,
GainComputer), `Tempo.h`.

File layout per knob: `plugins/Frisson/modules/<Name>.h` + `<Name>.cpp`
(class `frisson::<Name>Module` or `frisson::<Name>Modulator`), tests in
`tests/frisson/test_<name>.cpp` using `tests/TestHarness.h` and
`tests/frisson/TestSignals.h`. Both folders are globbed by CMake.

### 4.3 Parameters (processor level)

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `dose` | Dose | 0–100 % | 40 | master depth + blend |
| `weight` | Weight | 0–100 % | 25 | |
| `warmth` | Warmth | 0–100 % | 20 | |
| `focus` | Focus | 0–100 % | 30 | |
| `groove` | Groove | 0–100 % | 15 | |
| `shine` | Shine | 0–100 % | 35 | |
| `space` | Space | 0–100 % | 20 | |
| `rush` | Rush | 0–100 % | 25 | |
| `chills` | Chills | 0–100 % | 30 | |
| `life` | Life | 0–100 % | 20 | |
| `match` | Match | on/off | off | loudness-matched bypass |
| `chills_sense` | Chills Sensitivity | 0–100 % | 50 | advanced |
| `groove_pocket` | Groove Pocket | on/off | on | advanced; beat-aware emphasis when the host has tempo |
| `life_rate` | Life Rate | 0.5–2× | 1× | advanced |
| `space_mono_hz` | Space Mono Below | 60–300 Hz | 120 | advanced |
| `space_room` | Space Room | 0–100 % | 30 | advanced; early-reflection level |
| `rush_ceiling` | Rush Ceiling | −3–0 dBFS | −0.3 | advanced |
| `rush_autogain` | Rush Auto Gain | on/off | on | advanced |
| `output` | Output | −12–+12 dB | 0 | post everything, before MATCH |

Effective depth of a knob = knob × dose. Blend: `out = dry + (wet − dry) × dose`.

### 4.4 Editor

`ap::ui::EditorBase`, 1000 × 700 base (resizable 70–200 %). A 3×3 grid of
the nine knobs (rows: WEIGHT WARMTH RUSH / FOCUS SHINE SPACE / GROOVE CHILLS
LIFE), each a card with its tagline and an activity ring driven by
`Module::activity()`. DOSE is a large knob to the right; below it the A / B
card holds MATCH, the loudness delta ("+2.3 dB added by processing") and the
output meter, and an Advanced card shows the advanced parameters. Accent
colour: amber.

### 4.5 Metering

Per-knob activity (atomics), short-term LUFS of dry and wet, output peak,
chain latency. The processor runs two `ap::LoudnessMeter`s (dry, wet).

## 5. Acceptance tests

- Unit: every module's PRD tests pass in `frisson_tests`.
- Host test (`VST3HostTest … Frisson`): all knobs 0 → output equals input
  delayed by the reported latency (max abs diff < 1e-5); defaults → finite
  output, reported latency ≤ 5 ms; WEIGHT 100 → energy below 100 Hz rises;
  SHINE 100 → energy above 10 kHz rises; MATCH on → output ≈ dry at the wet
  loudness (within 1 dB after 3 s); state round-trips.
- pluginval strictness 10 passes.

## 6. Status and known issues (v0.1)

Built and verified: 74 module test cases (1,362 checks), host test through the
VST3 wrapper, pluginval strictness 10, 2.1 % of one core for the full recipe at
48 kHz / 512 on the CI-class Linux box. Not yet done: listening tests on real
music, which is what the CHILLS thresholds and the GROOVE pocket weights need
most.

- `ap::Biquad` (transposed direct form II) leaves a small residue (~−36 dB)
  when its gain coefficients jump; SHINE uses a state-variable shelf for this
  reason, the other modules smooth their coefficient updates on a 32-sample
  grid. A modulation-safe EQ in core is the v0.2 fix.
- GROOVE's attack emphasis can stack to ~+9 dB with modulation and pocket
  weighting; nothing clips inside the chain, but with RUSH at 0 there is no
  limiter after it, so hot material may need the Output trim.
- Module PRD deviations are recorded in each module's header comment and
  commit message (WEIGHT uses Chebyshev harmonics; SHINE mixes only the even
  part of the exciter; CHILLS' novelty baseline is the 0.3–8 s band maximum).

## 7. Out of scope / later

Oversampling of the saturating stages (v0.2), presets browser, AU/CLAP, a
"learn" mode that proposes a recipe from the input's spectral balance,
genre reference curves in the meter.
