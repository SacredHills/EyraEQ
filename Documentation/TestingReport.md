# Testing report

Newest phase first.

## Phase 4: corrective EQ (2026-11-22, branch `phase-4-corrective-eq`)

Automatic instrument recognition (Phase 3) was removed at the owner's request; the user chooses the
instrument. The plug-in no longer needs ONNX Runtime or model files (the host test checks the bundle
contains neither).

| Suite | Result |
|---|---|
| `EyraEQTests` (Release) | **all checks passed** (about 560), including 12 corrective-EQ test groups |
| `EyraEQHostTest` (Release) | **33 passed** |

What the correction tests cover is listed in [Correction.md](Correction.md#testing). Not yet tested:
correction quality on real multitrack recordings (the tests use synthetic signals with known
problems), and the panel in real DAWs.

The Phase 3 section below is kept for the record; that code is no longer part of the plug-in.

## Phase 3: instrument recognition (2026-11-21, branch `phase-3-instrument-recognition`)

### Update: drum recognition fix (2026-11-22)

Drums were not recognised (the model hears lone kicks as "Knock", snares as "Sound effect"; sparse
hits produced an empty long-term spectrum). Recognition is now two-stage: the model picks the family,
measurements pick the drum (see [AI-Models](AI-Models.md#drums)).

| Check | Result |
|---|---|
| `EyraEQTests`, Release and ASan/UBSan/LSan Debug, with the model | **464 checks passed, 0 failed**, no sanitizer errors |
| 30 local drum loops through `EyraEQAnalyze` (in-sample, not redistributable) | 14 detected, all correct; 15 "possibly" incl. the right drum; 1 "Drums"; 0 wrong |
| Synthetic kick / floor tom / rack tom / snare / hi-hat through the real analysis (CI) | all identified |
| NSynth pitched notes, retuned (valid → test) | 92% → **84%** correct when detected (44 test notes), 4% detected |

The figures below are from before this update.

| Suite | Result |
|---|---|
| `EyraEQTests` (Release, with ONNX Runtime 1.32.0 and the model) | **425 checks passed, 0 failed** |
| `EyraEQTests` without a runtime available | 417 passed; model tests report "skipped: ONNX Runtime library not found" (CI requires them) |
| `EyraEQHostTest` (Release) | **33 passed** (adds: bundle contains the runtime, model and taxonomy) |
| Both suites under ASan + UBSan + LSan (Debug), with the model | **all passed**; no sanitizer errors, no leaks, no JUCE assertions |

CI (GitHub Actions run 44987917750): Linux x64, macOS (Apple Silicon runner) and Windows x64 all
**pass**. On each one the official ONNX Runtime 1.32.0 was downloaded and bundled, the unit tests ran
*with* instrument recognition required (the TensorFlow-reference and self-test checks cannot skip), the
host test checked the bundle, and **pluginval strictness 10 reported SUCCESS**.

### Model conversion and verification (CI, `.github/workflows/model.yml`)

- YAMNet converted from Google's official weights (Apache 2.0); upstream checksums recorded.
- Python copy of the front end + ONNX vs the TensorFlow reference: log-mel error 2.3e-5, score error 7.7e-7.
- **Plug-in C++ front end + ONNX Runtime vs the TensorFlow reference** (unit test): log-mel within
  8e-6, scores within 1e-6. The same check runs as a self-test whenever the model loads.

### Recognition accuracy (NSynth, thresholds tuned on the valid split, measured on the test split)

| 1,177 single notes | Result |
|---|---|
| Correct when "detected" | **92%** |
| Detected with confidence | 7% |
| "Not certain" with candidates (contain the right instrument) | 61% (49%) |
| Not recognised | 32% |

Per-family details: [Validation/nsynth-validation.md](Validation/nsynth-validation.md).

### Behaviour tests

- Decision rules: detected (high / medium), not certain, not recognised, more audio needed; test
  values derived from the taxonomy's thresholds.
- Refinements: male / female / undecided vocal, rack / floor tom, clean / distorted guitar, synth bass.
- Model manager: newer user model preferred; tampered model rejected by checksum, with fallback to the
  bundled one; missing files reported.
- Missing model or runtime: "unavailable" with the reason; analysis and EQ unaffected.
- Through the plug-in: recognition runs after a capture; pink noise is not "detected" as an instrument;
  silence is never detected; results consistent at 16 and 48 kHz.
- The manual instrument choice is saved and restored with the session.

### Not yet tested (Phase 3)

- Drum recognition and the pitch-based refinements on real recordings (no suitable openly licensed data in CI yet).
- Real multitrack stems in general; NSynth single notes are a harder, narrower test.
- Loading inside real DAWs on macOS / Windows (CI covers the unit tests with the bundled runtime,
  the bundle contents, and pluginval).
- Intel Macs and macOS versions before 14: Microsoft ships no ONNX Runtime 1.32 build for them, so
  recognition reports "unavailable" there by design; not run in CI.

## Phase 2: audio analysis (2026-11-21, branch `phase-2-audio-analysis`)

| Suite | Result |
|---|---|
| `EyraEQTests` (Release) | **372 checks passed, 0 failed** |
| `EyraEQHostTest` (Release) | **29 passed, 0 failed** |
| `EyraEQTests` under ASan + UBSan + LSan (Debug) | **370 passed, 0 failed** (the 2 speed checks are skipped in debug and sanitizer builds); no sanitizer errors, no leaks, no JUCE assertions |

CI (GitHub Actions run 44974368202): Linux x64, macOS Universal and Windows x64 all **pass**: build,
`ctest` (unit tests + VST3 host test), and **pluginval strictness 10: SUCCESS** on all three.

### Detection tests on synthetic signals with known answers

| Signal | Expected | Result |
|---|---|---|
| Band-limited pink noise (30 Hz–18 kHz) | no findings, not pitched, not percussive | 0 findings |
| + bell 1.7 kHz, +12 dB, Q 10, at 44.1 / 48 / 96 kHz | resonance within 2% | 1698 / 1714 / 1717 Hz, about 10 dB, Significant |
| + bell 250 Hz, +6 dB, Q 1 | build-up named "mud" | "Low-mid buildup (mud)", 250 Hz, +4.0 dB |
| + bell 3.5 kHz, +6 dB, Q 1.2 | build-up named "harshness" | Harshness, 3.17 kHz, +3.7 dB |
| + 12 Hz sine | rumble | found; DC-offset signal gives DC, not rumble |
| + 60 / 120 / 180 Hz | mains hum, harmonics not repeated as resonances | "Mains hum (60 Hz)" |
| 196 Hz harmonic tone (25 harmonics) | pitch 196 Hz, no resonances, no build-up | pitch 196.0 Hz, 0 findings |
| Same tone + formant bell 600 Hz, +7 dB | boxiness found | "Boxiness", 630 Hz, +4.5 dB |
| Tom-like hits, 150–162 Hz gliding body | percussive, body not called mud | percussive, pitch 153 Hz, 0 findings |
| Drum hits with 180 Hz body + 1.13 kHz overtone ringing 0.3 s | ringing at 1.13 kHz; body not ringing; no build-up | "Ringing at 1.13 kHz", 10× slower decay; nothing else |
| Same hits without the overtone | no ringing, no resonance | 0 findings |
| Bell 2.2 kHz +14 dB active 1.3 s of every 4 s | intermittent resonance | 2183 Hz, present in 39% |
| 1 s of audio / silence / 1 s in 5 s of silence | insufficient / silent | as expected; active time measured 1.0 s |

### Pipeline tests (through the plug-in)

- Listen during playback, then automatic stop at 15 s of signal: resonance at 2.4 kHz found.
- Stop & analyse refused below 2 s of signal and allowed after; cancel keeps the previous result; a
  sample-rate change cancels listening; clear resets.
- File analysis (24-bit WAV) finds the resonance; an unreadable file gives a clear message.
- `processBlock` performs **zero heap allocations while listening** (all allocation functions
  instrumented, with a self-check).

### Performance

- Analysis of 30 s of 48 kHz audio: about 0.9 s on one core (Release), on the worker thread.

### Not yet tested (Phase 2)

- Analysis has been tested on synthetic signals only, not yet on a library of real recordings.
  Real-world tuning of thresholds (especially for dense mixes and polyphonic material) is still open.
- The interactive panel (buttons, list clicks) is covered by rendering tests and code review, not by
  automated UI interaction.

## Phase 1: functional EQ

Date: 2026-11-21 · Branch `phase-1-functional-eq`

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

### Continuous integration (GitHub Actions, run 44966178176, Phase 1)

| Platform | Runner | Build | `ctest` (unit + VST3 host test) | pluginval, strictness 10 |
|---|---|---|---|---|
| Linux x64 | ubuntu-24.04 | pass | pass | **SUCCESS** |
| macOS Universal (arm64 + x86_64) | macos-latest | pass | pass | **SUCCESS** |
| Windows x64 | windows-2022 (VS 2022) | pass | pass | **SUCCESS** |

### Unit tests (`EyraEQTests`)

| Area | What is verified | Measured |
|---|---|---|
| EQ core | bell / shelf gain, cut corner and slope for all eight slopes, notch depth, Dynamic and Spectral behaviour, latency | see `Tests/EqCoreTests.cpp` |
| Real-time safety | heap allocations in `processBlock` over 400 blocks with parameter changes, type / enable / dynamics switches and listen | 0 (counter verified by a self-check) |
| Parameters | 106 automatable parameters, host bypass, value text | pass |
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

- macOS and Windows have been built and tested only on CI runners, not on physical machines.
  On macOS only the runner's native architecture (arm64) was executed; the x86_64 slice is built but not run.
- **No DAW testing yet** (Reaper, Ableton Live, Bitwig, Cubase, Logic via a future AU, FL Studio).
  Planned for Phase 6.
- **No audio-device testing of the Standalone app** (the test machine has no sound card).
- **Interactive mouse behaviour** (dragging, wheel, menus) is not covered by automated tests. It
  was reviewed in code only; the rendered UI was checked from screenshots.
- **Installers** do not exist yet (Phase 6).
