# Claude Code Development Prompt: Intelligent Professional EQ VST3

## Project Overview

Build a complete, professional-grade parametric equalizer VST3 plugin with integrated intelligent instrument detection and automatic corrective EQ.

The plugin should combine the functionality of a high-end professional parametric EQ with an offline, locally running intelligent audio analysis system.

The central concept:

**A fully featured professional EQ that can listen to an audio recording, identify the instrument, detect frequency problems, and automatically generate corrective EQ settings — while allowing the user to retain complete manual control.**

This is not an AI application with a basic EQ attached. The primary product is a professional parametric equalizer. Intelligent correction is an integrated feature of the EQ.

The finished product must be a functional, production-quality audio plugin, not a prototype or visual mockup.

---

# 1. Development Requirements

### Framework and technologies

Use:

- C++20
- JUCE framework
- CMake build system
- VST3 plugin format
- ONNX Runtime for optional local machine-learning inference
- FFT-based frequency analysis
- Custom DSP for equalization and dynamic processing
- Cross-platform audio processing architecture

Use current stable, compatible versions of the required dependencies.

Keep dependencies manageable and document their licenses.

### Supported platforms

Build for:

- Windows 10 and 11 (64-bit)
- macOS (Apple Silicon and Intel where supported)
- Linux (64-bit)

### Required deliverables

Produce:

- Windows VST3 plugin
- Windows standalone application
- Windows installer (.exe)
- macOS VST3 plugin
- macOS standalone application
- macOS installer (.dmg or .pkg)
- Linux VST3 plugin
- Linux standalone application
- Linux installer (.AppImage and/or .deb)

Use appropriate platform-native packaging tools.

Use CMake presets and automated build scripts.

The project must be structured so that builds can be reproduced on all supported operating systems.

Do not claim that an installer works unless it has actually been built and tested.

---

# 2. Plugin Design Philosophy

The plugin should have the visual sophistication of a premium professional audio equalizer.

Use high-end equalizers as general inspiration for usability and functionality, but create a completely original interface.

Do not copy FabFilter Pro-Q or any other commercial plugin's:
- Interface layout
- Graphics
- Distinctive visual elements
- Icons
- Branding
- Typography
- Color combinations
- Proprietary interaction designs

Create an original visual identity.

### Visual direction

The interface should be:

- Dark and modern.
- Minimalist but professional.
- Clean and uncluttered.
- Designed for professional mixing environments.
- Highly readable.
- Responsive and resizable.
- Visually focused on the frequency spectrum.

Use subtle colors, smooth curves, carefully designed controls, and tasteful animation.

Avoid excessive gradients, unnecessary decorations, oversized buttons, and generic AI-themed graphics.

The frequency spectrum should be the primary visual element.

---

# 3. Main EQ Engine

This is the foundation of the entire plugin.

Build a fully functional parametric equalizer with high-quality audio processing.

### EQ band functionality

Support:

- Bell filter
- Low shelf
- High shelf
- High-pass filter
- Low-pass filter
- Notch filter
- Band-pass filter
- Tilt EQ

Each band must have:

- Frequency control
- Gain control
- Q/bandwidth control
- Filter type selection
- Individual bypass
- Individual solo audition where appropriate
- Adjustable slope for applicable filters
- Dynamic EQ option

Allow the user to create, edit, move, and delete EQ bands directly on the spectrum display.

Support a generous number of simultaneous EQ bands, with the architecture allowing the number to be increased without redesigning the DSP engine.

### Filter quality

Implement stable, high-quality filters using appropriate DSP techniques.

Requirements:

- Double-precision internal calculations where beneficial.
- Proper coefficient smoothing.
- No clicks when changing parameters.
- Stable behavior at extreme Q settings.
- Proper handling of sample-rate changes.
- No denormal-related performance problems.
- No unnecessary allocations in the audio processing callback.
- No blocking operations in the real-time audio thread.

Use oversampling where genuinely necessary, particularly for nonlinear processing if introduced in future versions.

The EQ itself should remain transparent and efficient.

---

# 4. Spectrum Analyzer

Build a real-time spectrum analyzer.

Requirements:

- Input spectrum.
- Output spectrum.
- Adjustable FFT resolution.
- Frequency range from approximately 20 Hz to 20 kHz.
- Logarithmic frequency scale.
- Adjustable analyzer speed.
- Adjustable averaging.
- Peak and RMS display options.
- Adjustable visual decay.
- Pre-EQ and post-EQ visualization.
- Optional freeze spectrum.
- Frequency grid and readable frequency labels.

The analyzer must operate efficiently without interfering with audio processing.

Provide a smooth, professional visualization.

The EQ response curve must update immediately when filters are changed.

Allow the user to show or hide analyzer elements.

---

# 5. Intelligent Instrument Recognition

This is one of the plugin's defining features.

Build a local instrument recognition system that analyzes the incoming audio and attempts to identify the instrument automatically.

The system must not require an internet connection.

### Initial supported instrument categories

Drums:
- Kick drum
- Snare drum
- Rack tom
- Floor tom
- Toms (generic)
- Cymbals
- Overheads
- Percussion

Bass:
- Electric bass
- Acoustic bass
- Synth bass

Guitars:
- Electric guitar
- Acoustic guitar
- Clean guitar
- Distorted guitar

Other:
- Male vocal
- Female vocal
- Piano
- Acoustic instruments
- Synthesizer
- Strings
- Brass
- Unknown / unclassified

The model should distinguish between instruments where the audio characteristics provide sufficient evidence.

Do not force a classification when confidence is low.

### Recognition architecture

Use a lightweight local machine-learning model.

Possible architecture:

- ONNX Runtime
- Audio feature extraction
- Spectral features
- Mel-frequency representations
- Temporal features
- Instrument classification model

Evaluate available pretrained models and datasets, but only use models with compatible licenses and document their origin.

If a suitable pretrained model cannot legally or technically be used, establish a training pipeline for a custom model.

Do not invent a trained model or pretend that an untrained classifier is intelligent.

The project must include:
- Model loading.
- Model validation.
- Confidence scoring.
- Classification thresholds.
- Unknown instrument handling.
- Manual instrument selection.
- A model update mechanism for future versions.

### Manual override

Always allow users to manually select an instrument.

Automatic detection must never prevent manual operation.

### Detection interface

Display:

- Detected instrument.
- Confidence level.
- Detection status.
- Option to change the detected instrument.
- Analyze button.
- Reanalyze button.

For ambiguous audio, show multiple possible classifications rather than presenting an unreliable result as fact.

---

# 6. Intelligent Audio Analysis

The plugin must analyze actual audio characteristics rather than simply applying generic instrument presets.

Analyze the audio for:

- Excessive low-frequency energy.
- Subsonic rumble.
- Low-mid buildup.
- Mud.
- Boxiness.
- Harshness.
- Excessive resonances.
- Ringing frequencies.
- Narrow-band frequency problems.
- Excessive high-frequency energy.
- Tonal imbalance.

Use a combination of DSP analysis and machine-learning assistance where appropriate.

### Important requirement

Do not assume that a frequency is problematic simply because it falls within a predefined range.

For example, a tom drum may naturally have significant energy around 150–250 Hz.

That does not automatically mean it needs cutting.

The analysis must consider:
- Instrument classification.
- Fundamental frequency.
- Harmonic structure.
- Spectral balance.
- Transient behavior.
- Frequency persistence.
- Resonance characteristics.

Preserve the natural tone of the recording.

Avoid aggressive or unnecessary EQ corrections.

---

# 7. Automatic Corrective EQ

Once the instrument has been identified and analyzed, generate actual EQ bands inside the normal EQ engine.

Example workflow:

1. User inserts the plugin on a floor tom track.
2. User plays the recording.
3. User clicks Analyze.
4. Plugin identifies the instrument as a floor tom.
5. Plugin analyzes the recording.
6. It identifies excessive low-mid buildup and a narrow resonance.
7. It generates appropriate EQ bands.
8. The generated bands appear directly on the main EQ display.
9. The user can modify, disable, remove, or adjust every generated band.

The automatic correction system must create real DSP filters.

Do not create separate visual-only corrections.

### Correction controls

Include:

- Analyze
- Apply Corrections
- Reanalyze
- Reset Corrections
- Correction Amount
- Number of Corrections
- Correction Strength
- Preserve Character
- Automatic High-Pass option

### Correction intensity

Provide adjustable correction intensity:

- Gentle
- Moderate
- Aggressive

The default should be Gentle or Moderate.

### Correction modes

Provide two operating modes:

**Transparent Mode**

Focus on removing unwanted frequency buildup and resonances while preserving the natural character of the instrument.

**Polished Mode**

Apply corrective EQ with optional subtle tonal enhancement.

The user must be able to switch between these modes.

### User approval mode

Allow two workflows:

1. Automatic: Apply the proposed corrections automatically.
2. Suggestions: Display proposed corrections and let the user approve them individually.

Never make irreversible changes to the original audio.

---

# 8. Dynamic EQ and Resonance Control

Include dynamic EQ functionality.

This is particularly important for controlling frequencies that become problematic only at certain moments.

Each suitable EQ band should support:

- Dynamic mode on/off.
- Threshold.
- Attack.
- Release.
- Maximum reduction.
- Dynamic range.
- Optional upward dynamic behavior if technically appropriate.

### Resonance suppression

Implement a dedicated resonance detection and suppression system.

It should:

1. Analyze narrow frequency peaks.
2. Distinguish persistent tonal content from problematic resonances where possible.
3. Identify potential ringing frequencies.
4. Create corresponding narrow EQ bands.
5. Allow dynamic reduction.
6. Allow the user to control suppression strength.

Avoid excessive resonance suppression that makes instruments sound unnatural.

---

# 9. Professional EQ Controls

Include the following features:

### Stereo processing

- Stereo mode.
- Left channel.
- Right channel.
- Mid/Side mode.
- Independent channel processing where appropriate.

### Additional controls

- Input gain.
- Output gain.
- Master bypass.
- A/B comparison.
- Undo/redo.
- Copy/paste EQ settings.
- Reset EQ.
- Preset browser.
- Save preset.
- Load preset.
- Factory presets.

### Audio handling

Support:

- Mono.
- Stereo.
- Multiple sample rates.
- Variable DAW buffer sizes.
- Proper plugin state restoration.
- Host automation for important parameters.
- Correct latency reporting where applicable.

The plugin must work correctly in common DAWs.

---

# 10. Main Interface Layout

Create a single integrated EQ interface.

The main window should have the following structure.

### Top section

- Plugin logo/name.
- Preset selector.
- Undo/redo.
- A/B.
- Master bypass.
- Settings.

### Main spectrum area

This should occupy most of the interface.

Display:
- Live frequency spectrum.
- EQ response curve.
- Editable EQ nodes.
- Individual band visualization.
- Frequency and gain information.
- Analyzer overlays.

Users should be able to:
- Double-click to create an EQ band.
- Drag a band to change frequency and gain.
- Use mouse scrolling or dedicated controls to adjust Q.
- Right-click a band for additional options.
- Delete bands.
- Select multiple bands where appropriate.

### Bottom EQ control section

Display detailed controls for the currently selected band:

- Filter type.
- Frequency.
- Gain.
- Q.
- Dynamic mode.
- Bypass.
- Additional band settings.

### Intelligent Analysis section

Integrate a compact but clearly identifiable intelligent analysis panel.

Display:

- Detected instrument.
- Confidence.
- Analyze button.
- Correction mode.
- Correction intensity.
- Suggested corrections.
- Apply button.

The intelligent analysis panel must not overwhelm the main EQ interface.

Allow the user to collapse it to maximize the spectrum area.

### Responsive interface

Support:
- Resizable window.
- Multiple interface scaling levels.
- High-DPI displays.
- Windows scaling.
- macOS Retina displays.
- Appropriate minimum window size.

Make the interface look polished at different window sizes.

---

# 11. Audio Processing Architecture

Keep the software modular.

Suggested architecture:

**Audio Engine**
- AudioProcessor
- EQEngine
- FilterBank
- DynamicEQ
- StereoProcessor
- OversamplingManager (only where needed)

**Analysis Engine**
- FFTAnalyzer
- SpectrumAnalyzer
- FrequencyAnalyzer
- ResonanceDetector
- TonalBalanceAnalyzer

**Intelligence Engine**
- InstrumentClassifier
- FeatureExtractor
- ModelManager
- ConfidenceEvaluator
- CorrectionGenerator

**User Interface**
- MainEditor
- SpectrumDisplay
- EQBandComponent
- BandControlPanel
- IntelligentAnalysisPanel
- PresetManager
- SettingsPanel

**Data**
- Preset files.
- Plugin state.
- Model files.
- User preferences.

Use a thread-safe architecture.

All expensive analysis and model inference must run outside the real-time audio callback.

Use buffered audio analysis and safe communication between processing, analysis, and UI threads.

Never block the audio thread waiting for AI inference.

Ensure that plugin state changes are handled safely.

---

# 12. Analysis Workflow

The plugin must provide a practical analysis workflow.

Since a VST3 plugin normally receives audio from the DAW rather than directly uploading a file, implement analysis through playback.

Workflow:

1. Insert the plugin on an instrument track.
2. Play the track or loop the relevant section.
3. Click Analyze.
4. The plugin captures an appropriate analysis buffer.
5. The analysis engine examines the recording.
6. Instrument detection runs.
7. Frequency problems are identified.
8. Suggested corrections are generated.
9. The user applies or rejects the corrections.

Provide a clear indication of how much audio has been analyzed.

Allow the user to analyze again when the source material changes.

The system must not assume that a short audio fragment is sufficient for reliable instrument identification.

Support offline file analysis in the standalone application if practical, using standard audio formats.

---

# 13. Preset System

Include useful factory presets for both EQ operation and intelligent correction.

Examples:

- Clean Vocal
- Vocal Presence
- Kick Tightening
- Kick Weight
- Snare Clarity
- Rack Tom Control
- Floor Tom Control
- Overhead Cleanup
- Bass Clarity
- Bass Mud Removal
- Acoustic Guitar Clarity
- Electric Guitar Cleanup
- Piano Clarity
- General Mix Cleanup

Presets must be starting points, not fixed rules.

Allow users to save their own presets.

Save all EQ band settings and relevant plugin state.

---

# 14. Performance and Reliability

This is a professional audio tool.

Prioritize reliability and sound quality over unnecessary features.

Requirements:

- No audible clicks or pops during normal parameter changes.
- No audio dropouts caused by analysis.
- No blocking calls in the audio callback.
- No unnecessary memory allocations in real-time processing.
- Proper thread synchronization.
- Proper handling of plugin loading and unloading.
- No crashes when changing sample rates.
- No crashes when changing buffer sizes.
- No memory leaks.
- No unstable filter configurations.
- No excessive CPU consumption.

Test with mono and stereo audio.

Test different sample rates and buffer sizes.

Ensure the EQ remains usable if the machine-learning model fails to load.

The entire manual EQ must continue working without the AI model.

---

# 15. Testing and Validation

Implement automated tests.

### DSP tests

Test:
- Filter frequency accuracy.
- Filter gain accuracy.
- Q accuracy.
- Filter stability.
- Dynamic EQ behavior.
- Stereo operation.
- Mid/Side processing.
- Sample-rate changes.
- Parameter smoothing.

### Intelligent analysis tests

Test:
- Instrument classification.
- Confidence handling.
- Unknown instrument handling.
- Frequency problem detection.
- Correction generation.
- Correction intensity.
- False positive reduction.

Use representative audio samples with appropriate licensing.

### Plugin tests

Test:
- VST3 loading.
- Plugin state saving.
- Plugin state restoration.
- Host automation.
- Mono and stereo operation.
- DAW compatibility.
- Standalone operation.

Create a testing report documenting actual results.

Do not mark a feature complete merely because it compiles.

---

# 16. Build System and Installers

Set up a reproducible cross-platform build system.

Use:

- CMake.
- JUCE.
- Platform-specific build scripts.
- CMake presets.
- Automated testing.
- Automated packaging.

### Windows

Build using Visual Studio 2022 or a compatible supported compiler.

Produce:
- VST3 plugin.
- Standalone executable.
- Windows installer.

### macOS

Build using Xcode and supported Apple toolchains.

Produce:
- VST3 plugin.
- Standalone application.
- DMG or PKG installer.

Support Apple Silicon and Intel builds where practical.

Include appropriate signing and notarization instructions. Do not pretend unsigned builds are notarized.

### Linux

Build using a supported Linux toolchain.

Produce:
- VST3 plugin.
- Standalone executable.
- AppImage and/or DEB package.

Document dependencies and installation paths.

### Important

A Windows machine cannot natively produce a properly tested macOS installer.

Create separate platform build scripts and GitHub Actions workflows using appropriate operating-system runners.

Make it possible to build and package each platform independently.

---

# 17. Project Structure

Create a clean and maintainable project structure.

Suggested layout:

```text
IntelligentEQ/
│
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
│
├── Source/
│   ├── Audio/
│   ├── DSP/
│   ├── Analysis/
│   ├── Intelligence/
│   ├── Models/
│   ├── Presets/
│   ├── UI/
│   ├── PluginProcessor.cpp
│   └── PluginEditor.cpp
│
├── Resources/
│   ├── Images/
│   ├── Fonts/
│   └── FactoryPresets/
│
├── Models/
│
├── Tests/
│
├── Scripts/
│   ├── build-windows.ps1
│   ├── build-macos.sh
│   ├── build-linux.sh
│   └── package-installers/
│
├── Installer/
│   ├── Windows/
│   ├── macOS/
│   └── Linux/
│
└── Documentation/
    ├── Architecture.md
    ├── DSP.md
    ├── AI-Models.md
    └── Building.md
```

You may adjust the structure if a better architecture is justified.

---

# 18. Development Phases

Build the application incrementally.

Do not attempt to implement everything in one enormous untested step.

### Phase 1: Functional EQ

Implement:
- JUCE project.
- CMake build.
- VST3 plugin.
- Standalone application.
- EQ engine.
- Parametric bands.
- Spectrum analyzer.
- Main interface.
- Preset system.
- A/B and bypass.

At the end of Phase 1, the product must already function as a proper standalone professional EQ.

### Phase 2: Audio Analysis

Implement:
- FFT analysis.
- Spectral analysis.
- Frequency problem identification.
- Resonance detection.
- Analysis buffer management.

### Phase 3: Instrument Recognition

Implement:
- Feature extraction.
- Local ML inference.
- Instrument classification.
- Confidence handling.
- Manual instrument selection.
- Unknown instrument handling.

### Phase 4: Intelligent Correction

Implement:
- Automatic correction generation.
- Instrument-aware correction logic.
- Correction intensity.
- Transparent and polished modes.
- Suggested corrections.
- Direct EQ band generation.

### Phase 5: Advanced Processing

Implement:
- Dynamic EQ.
- Resonance suppression.
- Mid/Side processing.
- Advanced analyzer options.
- Additional refinement.

### Phase 6: Production Release

Implement:
- Full testing.
- Performance optimization.
- DAW compatibility testing.
- Windows packaging.
- macOS packaging.
- Linux packaging.
- Documentation.
- Release builds.

At the end of every phase, ensure the application compiles and existing functionality continues to work.

---

# 19. Development Instructions for Claude Code

Follow these instructions throughout development.

1. Start by inspecting the existing development environment.
2. Check available compilers, CMake, JUCE dependencies, and installed build tools.
3. Establish the project structure.
4. Create the initial buildable application.
5. Implement features incrementally.
6. Compile and test regularly.
7. Fix errors before proceeding.
8. Never replace working functionality with placeholder code.
9. Never create fake controls that do nothing.
10. Never display simulated analysis results as real results.
11. Never claim that instrument recognition works without a functioning model.
12. Never use hardcoded instrument-specific EQ curves as a substitute for actual audio analysis.
13. Keep the manual EQ fully functional regardless of AI availability.
14. Document architectural decisions.
15. Keep the project maintainable and suitable for future commercial development.

When a feature is too large to complete in one phase, implement a working foundation and document what remains.

Do not silently omit requirements.

Do not stop after creating a GUI mockup.

Do not stop after producing code that has not been compiled.

The goal is a complete, functional professional audio plugin.

---

# 20. Final Product Vision

The finished product should feel like a professional studio equalizer first and an intelligent audio assistant second.

A user should be able to:

- Insert it on any audio track.
- Use it as a completely normal parametric EQ.
- Create and edit EQ bands manually.
- View the live frequency spectrum.
- Use dynamic EQ.
- Switch to intelligent analysis.
- Have the plugin identify the instrument.
- Analyze the recording.
- Find actual frequency problems.
- Generate corrective EQ bands.
- Adjust every generated band manually.
- Compare before and after.
- Save and recall presets.

The intelligent system should save time without taking control away from the engineer.

**Build a genuinely useful professional mixing tool, not an AI gimmick.**

Begin with Phase 1. Establish the build environment, create the project, implement the functional EQ engine and deliver a working, testable plugin before moving on to intelligent features.