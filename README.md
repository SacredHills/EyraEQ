# EyraEQ

A professional parametric equalizer (VST3 + Standalone) with built-in audio analysis and corrective
EQ: choose the instrument, let EyraEQ listen, and it proposes real EQ bands for the problem
frequencies it measured. The EQ is the product; the correction features are optional.

The full product specification lives in [DEVELOPMENT_PROMPT.md](DEVELOPMENT_PROMPT.md).

![EyraEQ editor](Documentation/Images/editor.png)

## Status

| Phase | Scope | Status |
|------:|-------|--------|
| 1 | Functional EQ: JUCE/CMake project, VST3 + Standalone, EQ engine, spectrum analyser, UI, presets, A/B, undo, bypass | **Done.** Builds, tests and pluginval (strictness 10) pass on Linux, macOS and Windows (see [testing report](Documentation/TestingReport.md)). |
| 2 | Audio analysis: problem-frequency and resonance detection, analysis buffer | **Done.** Listen-then-analyse workflow, detection of resonances, ringing, hum, build-ups, rumble and DC, shown in a collapsible panel and on the spectrum (see [Analysis](Documentation/Analysis.md)). |
| 3 | Instrument recognition | **Dropped at the owner's request.** Automatic recognition (a local YAMNet model) was built and measured, but was not reliable enough on real tracks; the user now chooses the instrument. No ONNX Runtime or model files are needed. |
| 4 | Corrective EQ generating real bands | **Done.** Instrument-aware corrections from the measured problems: Gentle / Moderate / Aggressive, Transparent / Polished, amount, number of bands, Preserve character, auto high-pass; applied automatically or approved one by one; undoable (see [Correction](Documentation/Correction.md)). |
| 5 | Dynamic EQ, resonance suppression, advanced analyser options | Not started (M/S and per-band stereo placement were brought forward into Phase 1) |
| 6 | Installers (.exe, .dmg/.pkg, .AppImage/.deb), signing, DAW compatibility testing | Not started |


## What works now (Phase 1)

**EQ engine**
- 24 bands (a compile-time constant; the engine is not tied to the number).
- Types: bell, low shelf, high shelf, high pass, low pass, notch, band pass, tilt.
- High / low pass slopes: 6, 12, 18, 24, 36, 48, 72 and 96 dB/oct, with resonance (Q) control.
- Per-band stereo placement: stereo, left, right, mid or side.
- Per-band bypass and solo (audition the band's region).
- Analogue-matched filter design in double precision: no bilinear "cramping" near Nyquist.
- Click-free: continuous parameters are smoothed; type / slope / placement changes and
  enable / bypass use a short S-curve crossfade. Measured transients stay at least 80 dB below the signal.
- Zero latency, no allocation or locking on the audio thread, denormal-safe.
- Input gain, output gain, master bypass (exposed to the host as its bypass parameter).

**Spectrum analyser**
- Pre-EQ (input) and post-EQ (output) spectra, each can be hidden.
- Resolution 2048–32768 points, speed, averaging, RMS or peak mode, peak decay, display
  slope (0 / 3 / 4.5 / 6 dB/oct), freeze.
- Lock-free audio-to-UI transfer; the analysis runs on the UI thread and can never stall audio.

**Interface**
- Spectrum-centred, resizable (860×520 up to 3200×2000) with size presets, HiDPI via JUCE.
- Double-click to create a band, drag nodes (Shift = fine), mouse wheel for Q, Shift-click or lasso
  for multi-select (selected bands move together), double-click a node to bypass, right-click for
  type / slope / placement / bypass / solo / delete, Delete key removes the selection.
- Band panel with type, frequency, gain, Q, slope, placement, bypass, solo and delete.
- Undo / redo (Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z), A/B comparison with copy, copy / paste of EQ
  settings (Ctrl/Cmd+C / V), reset.
- Presets: 14 factory starting points (plus an empty Default) and user presets (saved as `.eyrapreset` XML files).

## What works now (Phase 2)

- **Analyze** listens to the playing track (pre-EQ), shows how much real signal has been heard, and
  analyses automatically after 15 s of signal (or on request after 2 s). The standalone app can also
  analyse an audio file.
- Detects resonances, intermittent resonances, ringing after transients, mains hum, broad build-ups
  (low end, mud, boxiness, honk, harshness, excess highs), sub-sonic rumble and DC offset. Each
  finding comes with its measured evidence, a severity and a confidence.
- Protects the natural sound: harmonics and the source's own fundamental region are not reported,
  and pitched sources are judged by their harmonic envelope.
- Reports pitch, percussive or sustained character, and spectral tilt.
- Findings are listed in a collapsible side panel and marked on the spectrum, together with the
  average spectrum of the analysed audio.
- All analysis runs on a worker thread; the audio thread is unaffected (verified by tests).

## What works now (Phase 4): corrective EQ

1. Choose the instrument (24 choices, or General).
2. Play the track and press **Analyze** (or analyse a file in the standalone app).
3. Every finding shows the proposed move ("Cut 3.5 dB at 824 Hz, Q 8") or why it is left alone.
   Tick the ones you want and press **Apply corrections**, or turn on *Apply automatically*.
4. The corrections are ordinary EQ bands on the main display: adjust, bypass or delete them.
   **Reset** removes them; everything is undoable.

Controls: Transparent (cuts only) or Polished (plus subtle enhancement where nothing is wrong);
Gentle / Moderate / Aggressive; Amount (0–150%); Max bands (1–12); Preserve character (smaller or no
cuts in regions that make up the instrument's sound); Auto high-pass (kept below the lowest notes).

## Building

Requirements: CMake 3.22+, a C++20 compiler, and Git (JUCE 9.0.6 is fetched automatically).

```bash
# Linux
Scripts/build-linux.sh          # Release build, unit tests and VST3 host test
# macOS (Universal)
Scripts/build-macos.sh
# Windows (Visual Studio 2022)
powershell -ExecutionPolicy Bypass -File Scripts\build-windows.ps1
```

To build offline against a local JUCE checkout, set `JUCE_DIR` (or `-JuceDir` on Windows).
Platform details, Linux package dependencies and plug-in install locations are in
[Documentation/Building.md](Documentation/Building.md).

## Testing

`ctest` runs two executables:
- `EyraEQTests`: filter accuracy and stability, engine behaviour (gain accuracy, sample rates,
  block sizes, M/S, clicks, solo, NaN/denormals), processor state, presets, undo, A/B, analyser
  calibration, editor rendering, and the analysis engine on synthetic signals with known answers,
  including false-positive checks.
- `EyraEQHostTest`: loads the built `EyraEQ.vst3` through JUCE's VST3 hosting layer and checks
  scanning, layouts, host parameters, processing, state restore, editor, bypass and repeated
  load / unload.

CI (`.github/workflows/build.yml`) also runs [pluginval](https://github.com/Tracktion/pluginval) at
strictness 10 on all three platforms.

## Project layout

```
Source/
  DSP/          filter design, band, engine (no JUCE GUI dependencies)
  Analysis/     spectrum analyser, audio analysis (pitch, transients, tonal balance,
                resonances) and the capture / worker controller
  Correction/   instrument profiles and the correction generator (findings -> EQ bands)
  Parameters/   parameter layout and lock-free parameter reader
  State/        presets, undo history, A/B
  UI/           look and feel, spectrum display, band panel, top bar
  PluginProcessor.*, PluginEditor.*
Resources/FactoryPresets/   generated by Scripts/generate_factory_presets.py
Tests/          unit tests and the VST3 host test
Tools/          EyraEQAnalyze: the plug-in's analysis and corrections on audio files (command line)
Scripts/        build scripts, factory preset generator
Documentation/  architecture, DSP, analysis, correction, building, testing report
```

## Licensing notes

EyraEQ is licensed under the **GNU AGPL v3.0** (`LICENSE`), matching its use of [JUCE](https://juce.com)
under JUCE's free AGPLv3 licence. Anyone who distributes EyraEQ (including binaries) must make the
complete corresponding source code available under the same terms. Third-party components and their
licences are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

"VST" is a trademark of Steinberg Media Technologies GmbH.
