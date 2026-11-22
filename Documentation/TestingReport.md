# Testing report: Phase 1

Date: 2026-11-21 · Commit: Phase 1 branch (`phase-1-functional-eq`)

## Environment

| | |
|---|---|
| OS | Ubuntu 24.04 (x86-64), headless with Xvfb for GUI tests |
| Compiler | GCC 13.3.0, C++20, CMake 3.28, Ninja |
| JUCE | 9.0.6 |
| Builds | Release (LTO) and Debug + AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer |

## Results

| Suite | Result |
|---|---|
| `EyraEQTests` (Release) | **290 checks passed, 0 failed** |
| `EyraEQHostTest`: built VST3 hosted through JUCE's VST3 host (Release) | **29 passed, 0 failed** |
| Both suites under ASan + UBSan + LSan (Debug) | **All passed; no sanitizer errors, no leaks, no JUCE assertions** |
| Standalone app launched under Xvfb | Runs and renders (no audio device in the test machine) |

### Unit tests (`EyraEQTests`)

| Area | What is verified | Measured |
|---|---|---|
| Bell accuracy | gain at f0 across 44.1–192 kHz, 20 Hz–18 kHz, ±1–24 dB, Q 0.1–20 | max error 0.00001 dB |
| Bell vs analogue | full grid, 30 Hz–30 kHz | ≤ 0.77 dB for f0 ≤ fs/6; worst 3.3 dB vs 12 dB bilinear |
| Q / bandwidth | digital gain at analogue half-gain edges | within 0.15 dB |
| Shelves | DC, transition (half gain), HF | within 0.05 dB (HF near Nyquist 0.6 dB) |
| HP / LP | −3.01 dB at cutoff, slope for 6–96 dB/oct, resonance | within 0.05 dB / 0.6 dB |
| Notch / BP / tilt | depth, peak, pivot, extremes | notch < −60 dB; others ≤ 0.15 dB |
| Stability | 360,000 sections over the full parameter space and 22.05–192 kHz | all stable |
| Engine gain | sine measurements vs design, 4 sample rates | within 0.05 dB |
| Transparency | no bands, and master bypass | bit-identical output |
| Block sizes | 1, 17, 333, 512, 4096 (larger than prepared) | identical output (≤ 1e-6) |
| Stereo | left, right, mid, side on mono-compatible and pure-side signals | as expected, ≤ 0.05 dB |
| Clicks | gain, frequency, type, slope, enable, disable, bypass, placement, Q, master bypass | HF transient ≤ −81 dBFS on a −20 dBFS signal (fades ≤ −105 dBFS) |
| Solo | out-of-band attenuation, in-band level | < −30 dB / within 0.1 dB |
| Robustness | 24 high-Q bands, impulse then ~23 s silence | no NaN or Inf, decays below 1e-20 |
| CPU | 24 active bands (one at 96 dB/oct), stereo, 48 kHz | ~42× real time on one core, including signal generation |
| Real-time safety | heap allocations in `processBlock` over 400 blocks with parameter changes, type / enable / placement switches and solo | 0 (counter verified by a self-check) |
| Parameters | 195 automatable parameters, host bypass, value text | pass |
| State | save / restore of all parameters, UI state, preset name, A/B slot; garbage input ignored | pass |
| Presets | all 15 factory presets load and match their files; user preset save / load / delete | pass |
| Undo / A/B / copy-paste | round trips | pass |
| Analyser | 0 / −20 / −60 dBFS sine calibration, non-blocking FIFO, peak decay, freeze | within 0.6 dB |
| Editor | creation, three window sizes, rendering, coordinate mapping | pass (screenshots reviewed) |

### VST3 host test (`EyraEQHostTest`)

Scan (one plug-in, name, manufacturer, category `Fx|EQ`), instantiation, mono and stereo layouts,
zero latency, 195 host parameters, host-set −9 dB band measuring −9.00 dB in 8 combinations
(mono/stereo × 44.1/96 kHz × 64/512-sample blocks), state transfer to a new instance with identical
processing, editor open and resize, host bypass passing audio unchanged, and 10 load / process / unload cycles.

## Not yet tested

Stated plainly so nothing is assumed:

- **macOS and Windows builds have not been compiled or run.** The CI workflow for them is in place
  but has not run yet, because this branch has not been pushed until now.
- **pluginval has not been run locally**: building it from source was not permitted in the
  development sandbox. It is configured in CI at strictness level 10 on all three platforms.
- **No DAW testing yet** (Reaper, Ableton Live, Bitwig, Cubase, Logic via a future AU, FL Studio).
  Planned for Phase 6.
- **No audio-device testing of the Standalone app** (the test machine has no sound card).
- **Interactive mouse behaviour** (dragging, wheel, menus) is not covered by automated tests. It
  was reviewed in code only; the rendered UI was checked from screenshots.
- **Installers** do not exist yet (Phase 6).
