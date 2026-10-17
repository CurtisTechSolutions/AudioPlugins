# Curtis Tech Solutions — Audio Plugins

Four VST3 plugins built on a shared, framework-independent DSP core, with JUCE 9 doing
the plugin plumbing and UI. Windows and macOS binaries come out of GitHub Actions;
Linux builds too and is used for validation.

| Plugin | What it is | Type |
|---|---|---|
| **Strata** | Polyphonic subtractive synth — 2 PolyBLEP oscillators with unison, sub + noise, multimode SVF with drive, 2 ADSRs, 2 LFOs, glide, chorus / delay / reverb | Instrument |
| **VoxNote** | Hum or sing into the mic, get MIDI out: YIN pitch tracking, onset / release gating, retrigger vs. pitch-bend logic, scale snapping, velocity from level, built-in monitor tone | Audio FX with MIDI output |
| **Intone** | Pitch correction to a key/scale (incl. 12-note custom scale) via formant-preserving PSOLA, retune speed + amount, plus a vocal chain: low cut, de-esser, compressor, body / presence / air EQ | Audio FX |
| **Ducker** | Sidechain compressor with lookahead and SC low-cut, internal-detector mode, and a tempo-synced *ghost* ducking mode that needs no sidechain routing at all | Audio FX with sidechain input |

The names are working titles; rename in each `plugins/<Name>/CMakeLists.txt` (`PRODUCT_NAME`)
and the editor title. Keep `PLUGIN_CODE` stable once a plugin has been used in sessions,
or hosts will treat the renamed build as a different plugin.

## Layout

```
core/            Header-only DSP (namespace ap), no JUCE dependency, unit-tested
  include/ap/    PitchDetector (YIN), PitchTracker, NoteTracker, PitchShifter (PSOLA),
                 Oscillator (PolyBLEP + LFO), Filters (biquad, smoother), Dynamics,
                 Scales, Tempo (divisions + ghost-duck envelope), Math
common/          JUCE-side shared code: LookAndFeel, generic parameter panels,
                 meters/readouts, editor base, parameter-layout helpers
plugins/         One folder per plugin: PluginProcessor + PluginEditor + CMakeLists
tests/           ap_tests (core unit tests) and host/ (loads each built .vst3 through
                 JUCE's headless host and checks behaviour end to end)
cmake/           JUCE fetch (pinned tag) and the ap_add_plugin() helper
.github/         CI: build + test + pluginval on Windows / macOS / Linux, upload VST3s
```

## Building

Requirements: CMake ≥ 3.22, a C++20 compiler (MSVC 2022, Xcode 15+, GCC 13 / Clang 16+), Ninja
recommended. JUCE is fetched automatically (pinned in `cmake/JUCE.cmake`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Plugins land in `build/plugins/<Name>/<Name>_artefacts/Release/VST3/<Name>.vst3`.
Add `-DAP_COPY_PLUGINS=ON` to copy them into the user plugin folder after each build.

Options:

| Option | Default | Purpose |
|---|---|---|
| `AP_BUILD_PLUGINS` | ON | Build the JUCE plugins (OFF = core + unit tests only) |
| `AP_BUILD_TESTS` | ON | Unit tests and the VST3 host tests |
| `AP_BUILD_STANDALONE` | OFF | Also build Standalone apps (needs ALSA on Linux) |
| `AP_COPY_PLUGINS` | OFF | Copy plugins to the system plugin folder after build |
| `AP_LINUX_MINIMAL_X11` | OFF | Build without libxrandr/xinerama/xcursor/xi dev packages |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | – | Use a local JUCE checkout instead of fetching |

Linux dependencies (Debian/Ubuntu): `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxext-dev libxi-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev
libgl1-mesa-dev`.

### Windows (WSL2 note)

Build the Windows VST3s natively (Visual Studio 2022 Build Tools + CMake), not inside WSL —
a Linux `.so` won't load in a Windows DAW:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Or just push and download the `vst3-windows` artifact from the Actions run.

### macOS

The CI build is a universal (arm64 + x86_64) binary, ad-hoc signed only. Gatekeeper will
quarantine a downloaded bundle; clear it once:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Strata.vst3
```

Proper Developer-ID signing + notarization is a TODO before sharing with other people.

## Testing

- `ap_tests` — core DSP unit tests (pitch detection accuracy, PSOLA pitch/level, note
  state machine, scales, filters, gain computer, tempo envelope).
- `host_<Plugin>` — `VST3HostTest` loads the built `.vst3` through JUCE's headless VST3
  host and checks real behaviour: the synth plays C4 in tune and decays to silence,
  VoxNote turns a 224 Hz tone into A3 with pitch bend and a matching note-off, Intone
  pulls a sharp A3 to 220 Hz and snaps to C major, Ducker ducks >6 dB from the sidechain
  and pumps on the beat in ghost mode with the right latency.
- CI additionally runs [pluginval](https://github.com/Tracktion/pluginval) at strictness 10
  on every plugin on every platform.

## Using the plugins

**VoxNote** is an *audio effect that outputs MIDI*. Put it on the vocal/mic track, then on
an instrument track set MIDI input to "VoxNote" (Ableton: *MIDI From → track → VoxNote*;
Reaper: route MIDI from the FX track; Bitwig/FL: MIDI output of the device). Turn on
*Monitor Synth* to hear what it detects without routing anything. Detection runs a window
of ~20–40 ms behind the audio; the *Tracking* preset trades low-note range for latency.
Logic Pro's AU effects can't emit MIDI; this is VST3-only by design.

**Intone** reports its latency (~50 ms at 48 kHz) to the host, so tracks stay aligned when
playing back. For live monitoring use the host's low-latency mode or a direct-monitoring
path. *Retune Speed* 0 ms = hard tune; 20–60 ms is natural; *Amount* blends correction.
*Scale = Custom* uses the 12 note buttons.

**Ducker** — in *External sidechain* mode route the kick into the plugin's sidechain input
(the header shows a green dot once the host connects it). *Ghost* mode needs no routing:
it ducks on every *Division* boundary using the host tempo and transport (or *Free BPM*
when stopped).

## Licensing

See `LICENSE`. JUCE is used under its EULA (free up to the lowest revenue tier; tiers are
by total company revenue, not plugin revenue). Distributing VST3 builds requires accepting
Steinberg's VST 3 licence. Nothing here is GPL-contaminated, so going commercial later only
requires picking the right JUCE tier.
