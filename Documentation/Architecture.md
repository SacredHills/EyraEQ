# Architecture

## Threads and data flow

```
              host / audio thread                      message (UI) thread
  ┌──────────────────────────────────────┐     ┌──────────────────────────────────────┐
  │ PluginProcessor::processBlock         │     │ Editor timer (60 Hz)                  │
  │   ParameterReader (atomics) ──┐       │     │   SpectrumAnalyzer::update  (FFT)     │
  │                               ▼       │     │   SpectrumDisplay::refreshCurves      │
  │   EQEngine::process ── pre tap ──► AnalyzerFifo (SPSC, lock-free) ──►│              │
  │                     └─ post tap ─► AnalyzerFifo ─────────────────────►│              │
  └──────────────────────────────────────┘     │ Parameter edits ─► APVTS ─► host      │
                                               │ EditHistory / ABComparison / Presets  │
                                               └──────────────────────────────────────┘
```

- **Audio thread** reads parameters only through `std::atomic<float>` pointers
  (`Parameters/ParameterReader`). It never allocates, locks or waits. Scratch buffers are sized in
  `prepareToPlay`; if a host passes a larger block than announced, the engine processes it in
  chunks instead of allocating.
- **Analyser transfer** is a single-producer / single-consumer `AbstractFifo`. When the UI is
  closed or slow, the audio thread simply drops samples.
- **UI** reads parameter values on the message thread and recomputes the response curves only when
  a band actually changes. It uses exactly the same `dsp::design()` code as the audio path, so the
  drawn curve is the real filter response at the current sample rate.
- **Undo, A/B and presets** run on the message thread only and change parameters through
  `setValueNotifyingHost` inside gestures, so hosts record automation and stay in sync.

**Analysis** (Phase 2) runs on a low-priority worker thread (`Analysis/AnalysisController`). The
pre-EQ signal is sent to both the analyser FIFO and a capture FIFO (`TapFanout`); the capture FIFO
accepts data only while listening. The worker allocates the capture buffer, drains the FIFO, tracks
progress and runs `analyseAudio()`. Results reach the UI through a lock-protected `shared_ptr`
polled by the editor timer. Later phases (instrument inference) will use the same worker.

## Modules

| Folder | Contents | Depends on |
|---|---|---|
| `DSP/` | `FilterDesign` (coefficient design), `EQBand` (smoothing, crossfades, biquad cascade), `EQEngine` (signal path, M/S, solo, gains, bypass) | `juce_audio_basics` only |
| `Analysis/` | `AnalyzerFifo`, `SpectrumAnalyzer`; `AudioAnalysis` (orchestrator) with `TransientAnalyzer`, `PitchEstimator`, `TonalBalanceAnalyzer`, `ResonanceDetector`, `SpectralTools`; `AnalysisController` (capture + worker) | `juce_dsp`, `juce_audio_formats` |
| `Parameters/` | parameter layout, IDs, `ParameterReader` | `juce_audio_processors` |
| `State/` | `EditHistory`, `ABComparison`, `PresetManager` | `juce_audio_processors` |
| `UI/` | look and feel, `SpectrumDisplay`, `BandControlPanel`, `TopBar`, `EditorContext` | `juce_gui_basics` |

The DSP layer has no GUI or plug-in dependencies and is tested on its own.

## Parameters

Every band has eight host-automatable parameters (`b{n}_used`, `_bypass`, `_type`, `_freq`,
`_gain`, `_q`, `_slope`, `_place`). Global parameters: `input_gain`, `output_gain`, `bypass`. IDs
carry version hint 1. The number of bands is `EQEngine::kMaxBands`; increasing it adds parameters
without any DSP change. Changing it breaks session compatibility for automation lanes, so it should
be settled before the first public release.

Solo is not a parameter: it is a monitoring aid and is neither saved nor automated.

## Saved state

`getStateInformation` writes one XML document:

```
<EyraEQState version="1" preset="...">
  <Parameters> ... APVTS state ... </Parameters>
  <UIState fftOrder=".." speed=".." ... width=".." height=".."/>
  <ABComparison active="0|1"> <Slot index=".."> <P id=".." v=".."/> ... </Slot> </ABComparison>
</EyraEQState>
```

Unknown or corrupt data is ignored. Loading a session never fails because of a missing child.

## Undo model

`EditHistory` stores complete parameter snapshots. It listens to parameter *gesture starts* on the
message thread, so every UI control is undoable without extra wiring, and dragging several nodes or
loading a preset becomes one step. Programmatic changes (undo itself, preset loading, A/B) run
inside a `ScopedSuspend`. Master bypass is excluded from undo and A/B.

## Decisions

- **Analogue-matched filters** rather than plain bilinear (RBJ) biquads, so curves keep their shape
  near Nyquist without needing oversampling, which keeps the EQ zero-latency and transparent. See `DSP.md`.
- **Double precision** for coefficients, filter state and the whole internal signal path.
- **Series topology with per-band stereo placement**: each band switches between the L/R and M/S
  domains only when needed. The conversion is exact, so mixing placements costs nothing in quality.
- **No intelligent UI until it works**: the specification forbids fake controls and simulated
  results, so the analysis panel arrived only with working analysis (Phase 2), and there is no
  instrument display until recognition works (Phase 3).
- **Analysis is a pure function** (`analyseAudio(samples, rate)`), so it can be tested on synthetic
  signals with known answers and reused for file analysis.
