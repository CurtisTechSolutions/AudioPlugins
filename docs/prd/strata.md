# Strata — PRD

Status: shipped v0.1 (VST3, Windows/macOS/Linux)

## 1. Summary

Strata is a polyphonic subtractive synth. Two band-limited oscillators (A has
up to 4-voice unison), a sub and noise feed a multimode SVF with drive. It has
2 ADSRs, 2 LFOs, glide, and chorus / delay / reverb, all on one page with no
mod matrix to learn. "Strata" is a **working title (placeholder)**: rename it
via `PRODUCT_NAME` in `plugins/Strata/CMakeLists.txt` and the editor title.
Keep `PLUGIN_CODE Strt` so saved sessions still find it. Format: VST3
Instrument|Synth, MIDI in, stereo or mono out. Code: `plugins/Strata/` with
the voice in `common/include/ap_synth/SynthVoice.h` (shared with Humline,
unchanged when it moved there) on `core/include/ap/` Oscillator, Filters and
Math, plus JUCE Synthesiser, TPT SVF, Chorus, DelayLine and Reverb.

## 2. Who it is for and why

Producers and songwriters who want pads, leads, basses and plucks from a
light, predictable poly synth in any VST3 host. Learners who want the
textbook path: oscillator → filter → amp, with envelopes and LFOs on fixed
destinations. It is also the natural target for VoxNote's MIDI, and it
exercises the shared oscillator, LFO and smoother DSP in a real host.

## 3. Requirements

- R1. Polyphony 1–16 (`voices`). All 16 voices are allocated up front, and
  free voices are taken only from the first `voices`. **Known v0.1 defect:**
  once those are busy, `PolySynth` calls JUCE's `findVoiceToSteal`, which
  scans all 16 voices and returns an idle one above the limit. So at any
  setting below 15, up to 15 voices can sound, `voices` = 1 is not mono, and
  debug builds assert. Fix in v0.2: steal only inside the active range.
- R2. Each oscillator plays the note (A4 = 440 Hz) × its octave/semi/fine
  offset. Glide, bend (±`bend_range` st) and vibrato are applied in log
  frequency. Frequencies update every 8 samples.
- R3. Oscillators are band-limited: PolyBLEP saw and square, and a triangle
  integrated from the band-limited square. Pulse width is clamped to 5–95 %.
- R4. Unison is on osc A only. The 1–4 copies are spread over ±`uni_detune`
  ct, panned equal-power over ±`uni_spread`, and scaled by 1/√N.
- R5. Filter: a 12 dB/oct TPT SVF (LP/BP/HP) per channel per voice.
  Cutoff = `f_cutoff` · 2^(6·`f_env`·fEnv·velF + 3·`f_lfo`·LFO2 + `f_key`·(note−60)/12),
  clamped to 20 Hz – 0.45·fs and updated every 16 samples. Q = lerp(0.5, 8, res²).
  Drive is tanh(x·d)/√d before the filter, with d = 1 + 6·drive, and is
  skipped at 0 %.
- R6. Amp and filter envelopes are linear juce::ADSRs whose settings latch at
  note-on. A voice frees itself when its amp envelope ends. Amp gain =
  lerp(1, vel, `vel_amp`); velF = lerp(1, vel, `vel_filter`). Each voice is
  scaled by 0.35 for headroom.
- R7. MIDI: note on/off on any channel, pitch wheel, sustain/sostenuto and
  All Notes Off (all via juce::Synthesiser). CC1 adds up to 50 ct of LFO 1
  vibrato (read per block). Aftertouch, MPE and other CCs are ignored.
- R8. FX order is fixed: chorus → delay → reverb → master. Chorus runs only
  on stereo output, while its mix > 0. The delay keeps running after its mix
  reaches 0 until the tail is < 1e-5. Master (20 ms) and delay time (80 ms)
  are smoothed so they do not click.
- R9. After note-off at defaults (300 ms release), output drops below 1e-4 RMS
  within 3.2 s. Output is always finite. Latency is 0 and the reported tail
  is 4 s.
- R10. State is APVTS XML with root `STRATA`; any other tag is ignored.
  Parameter IDs and display names are frozen (sessions and the host test rely
  on them). No allocation on the audio thread after `prepareToPlay`; the only
  lock is juce::Synthesiser's uncontended one.
- R11. Passes `host_Strata` and pluginval strictness 10 in CI on Windows,
  macOS and Linux.

## 4. Design

```
MIDI ─► PolySynth (16 × Voice: glide(log2 Hz) + bend + LFO1 vibrato w/ fade + mod wheel)
  OscA ×1–4 ─┐
  OscB ──────┼─► Σ ─► drive ─► SVF L/R ─► × ampEnv·velA·0.35    cutoff ◄─ fEnv·velF, LFO2, key
  Sub, Noise ┘
Σ voices ─► Chorus ─► ping-pong Delay ─► Reverb ─► Master ─► out   (mono out = (L+R)/2)
```

`updateSettings()` snapshots parameters once per block. Glide is a one-pole on
log2(Hz) that starts from the previous note-on's pitch. The LFOs are per
voice and key-synced: LFO 1 goes to pitch, LFO 2 to cutoff. Chorus: 12 ms
centre, depth 0.35, feedback 0.1. Delay: two linear-interpolated lines (max
2.1 s); `delay_mix` is the send, `delay_fb`×0.95 is cross-fed L↔R, and the wet
signal sits over full dry. Reverb: damping 0.4, width 1, wet `reverb_mix`×0.6,
dry 1.

### 4.1 Parameters (53)

Hz and ms knobs centre at √(min·max); ms knobs that start at 0 centre at 20 %
of max. LFO rates centre at 2 Hz, chorus rate at 1 Hz. "X / Y" rows list two
IDs and their defaults in order.

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `a_wave` / `b_wave` | Wave A / B | Sine / Triangle / Saw / Square | Saw / Square | |
| `a_oct` / `b_oct` | Octave A / B | −2 – +2 | 0 / 0 | |
| `a_semi` / `b_semi` | Semi A / B | −12 – +12 | 0 / 0 | |
| `a_fine` / `b_fine` | Fine A / B | −100 – +100 ct | 0 / 7 | |
| `a_level` / `b_level` | Level A / B | 0–100 % | 80 / 40 | |
| `a_pw` / `b_pw` | Width A / B | 0–100 % | 50 / 50 | clamped 5–95 % |
| `uni_voices` | Unison | 1–4 | 1 | osc A only |
| `uni_detune` | Detune | 0–100 ct | 12 | outer copies at ±value |
| `uni_spread` | Spread | 0–100 % | 50 | |
| `sub_level` | Sub Level | 0–100 % | 0 | |
| `sub_wave` | Sub Wave | Sine / Square | Sine | |
| `sub_oct` | Sub Octave | −1 / −2 | −1 | |
| `noise_level` | Noise | 0–100 % | 0 | white, ×0.5 |
| `f_type` | Filter Type | Low-pass / Band-pass / High-pass | Low-pass | |
| `f_cutoff` | Cutoff | 20–20 000 Hz | 4000 | |
| `f_res` | Resonance | 0–100 % | 20 | |
| `f_env` | Env Amount | −100 – +100 % | 40 | ±6 oct |
| `f_key` | Key Track | 0–100 % | 50 | 100 % = 1 oct/oct about C4 |
| `f_lfo` | LFO 2 Amount | −100 – +100 % | 0 | ±3 oct |
| `f_drive` | Drive | 0–100 % | 0 | |
| `amp_a` / `amp_d` | Attack / Decay | 1–5000 ms | 5 / 200 | |
| `amp_s` | Sustain | 0–100 % | 80 | |
| `amp_r` | Release | 1–8000 ms | 300 | |
| `fenv_a` / `fenv_d` | F Attack / F Decay | 1–5000 ms | 5 / 300 | |
| `fenv_s` | F Sustain | 0–100 % | 20 | |
| `fenv_r` | F Release | 1–8000 ms | 300 | |
| `lfo1_shape` / `lfo2_shape` | LFO 1 / 2 Shape | Sine / Triangle / Saw Up / Saw Down / Square / S&H | Sine / Sine | |
| `lfo1_rate` / `lfo2_rate` | LFO 1 / 2 Rate | 0.05–20 Hz | 5 / 1 | no tempo sync |
| `lfo1_depth` | Vibrato | 0–100 ct | 0 | + mod wheel × 50 ct |
| `lfo1_fade` | Vib Fade | 0–3000 ms | 0 | |
| `voices` | Voices | 1–16 | 8 | soft limit (R1) |
| `glide` | Glide | 0–2000 ms | 0 | |
| `vel_amp` / `vel_filter` | Vel > Amp / Vel > Filter | 0–100 % | 70 / 50 | |
| `bend_range` | Bend Range | 0–24 st | 2 | |
| `master` | Master | −60 – +12 dB | −6 | |
| `chorus_mix` | Chorus | 0–100 % | 0 | |
| `chorus_rate` | Chorus Rate | 0.1–5 Hz | 0.8 | |
| `delay_time` | Delay Time | 10–2000 ms | 375 | free time, no sync |
| `delay_fb` | Feedback | 0–100 % | 35 | ×0.95 |
| `delay_mix` | Delay | 0–100 % | 0 | send |
| `reverb_size` | Room Size | 0–100 % | 50 | |
| `reverb_mix` | Reverb | 0–100 % | 0 | |

**Editor:** `ap::ui::EditorBase`, 980 px, 2 columns, amber `#F5A623`, with an
output meter in the header. Left column: Oscillator A, Oscillator B, Sub &
Noise, Filter. Right column: Amp Envelope, Filter Envelope, LFO 1 · Pitch,
LFO 2 · Filter, Global, Effects.

## 5. Acceptance tests

- `host_Strata` (`tests/host/VST3HostTest.cpp`, 48 kHz / 512):
  - `commonChecks`: latency ≥ 0; finite output on silence; a non-empty state
    blob that restores and keeps processing (R9, R10).
  - `testStrata`: C4 at velocity 100 is finite, RMS > 0.01, YIN
    261.63 ± 4 Hz (R2). After note-off plus 300 blocks the last 4 blocks are
    < 1e-4 RMS (R9). A C–E–G chord has > 1.2 × the RMS of one note (R1).
- `ap_tests`:
  - `test_synthesis.cpp`: `oscillator_waveforms_are_bounded_and_pitched`
    (peak 0.6–1.2, |mean| < 0.05, 220 ± 1.5 Hz), `oscillator_saw_is_band_limited`
    (R3), `lfo_shapes_in_range`, `smoother_converges` (R8).
  - `test_scales.cpp`: `midi_hz_round_trip`, `db_gain_round_trip`.
- CI: pluginval strictness 10 on all three OSes (R11).
- Missing, add in v0.2: a polyphony-limit test (would catch R1), filter and
  key-track response, glide time, mod-wheel vibrato, sustain pedal, FX tails,
  mono output.

## 6. Out of scope / later

- Presets and a browser (v0.1 has one "Default" program), a mod matrix, more
  LFO destinations, tempo-synced LFOs and delay, a legato/mono mode, MPE and
  aftertouch, a 24 dB/oct filter, and oversampled drive.
- Known v0.1 gaps besides R1:
  - The fixed 4 s tail is shorter than an 8 s release or a high-feedback
    delay, so offline renders can cut off the tail.
  - Setting Reverb to 0 cuts the reverb tail instead of letting it ring out.
- AU/CLAP, and macOS Developer-ID signing and notarization (CI signs ad hoc only).
