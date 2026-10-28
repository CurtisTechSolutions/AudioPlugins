# Product requirements — Curtis Tech Solutions audio plugins

One PRD per product, plus one per Frisson knob (each knob is a separately
buildable, separately tested module).

| Document | Product | Status |
|---|---|---|
| [strata.md](strata.md) | Strata — polyphonic subtractive synth | shipped v0.1 |
| [verdant.md](verdant.md) | Verdant — flagship wavetable synth | in development v0.1 |
| [voxnote.md](voxnote.md) | VoxNote — voice-to-MIDI | shipped v0.1 |
| [intone.md](intone.md) | Intone — pitch correction + vocal chain | shipped v0.1 |
| [ducker.md](ducker.md) | Ducker — sidechain + ghost ducking | shipped v0.1 |
| [frisson.md](frisson.md) | Frisson — reward-system sound enhancer (umbrella) | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-weight.md](frisson-weight.md) | WEIGHT knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-warmth.md](frisson-warmth.md) | WARMTH knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-focus.md](frisson-focus.md) | FOCUS knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-groove.md](frisson-groove.md) | GROOVE knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-shine.md](frisson-shine.md) | SHINE knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-space.md](frisson-space.md) | SPACE knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-rush.md](frisson-rush.md) | RUSH knob | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-chills.md](frisson-chills.md) | CHILLS modulator | built v0.1 (unit + host tests, pluginval 10) |
| [frisson-life.md](frisson-life.md) | LIFE modulator | built v0.1 (unit + host tests, pluginval 10) |

## Template

Every PRD has the same sections so they can be compared and reviewed quickly:

1. **Summary** — one paragraph: what it is, who it is for.
2. **The response it targets** — the listener experience and the evidence behind it (only for Frisson knobs).
3. **Requirements** — numbered, testable statements (R1, R2, …).
4. **Design** — DSP/architecture, parameters with IDs, ranges and defaults.
5. **Acceptance tests** — what the unit/host tests must prove.
6. **Out of scope / later**.
