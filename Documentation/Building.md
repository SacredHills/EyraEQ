# Building EyraEQ

## Common requirements

- CMake ≥ 3.22, Git, a C++20 compiler
- JUCE 9.0.6, fetched automatically at configure time (pinned by `EYRA_JUCE_TAG` in `CMakeLists.txt`).
  To use a local checkout instead: `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`
  (the build scripts accept `JUCE_DIR` / `-JuceDir`).

| Preset | Platform | Generator |
|---|---|---|
| `linux-release`, `linux-debug` | Linux | Ninja |
| `macos` (build preset `macos-release`) | macOS, Universal arm64 + x86_64 | Xcode |
| `windows` (build preset `windows-release`) | Windows x64 | Visual Studio 17 2022 |

```bash
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release      # needs a display (use xvfb-run on headless machines)
```

Outputs are in `build/<preset>/EyraEQ_artefacts/<Config>/`:
`VST3/EyraEQ.vst3` and `Standalone/EyraEQ[.app|.exe]`.

`-DEYRA_COPY_PLUGIN_AFTER_BUILD=ON` installs the VST3 into the user plug-in folder after each build.

### Command-line analyser

`EyraEQAnalyze` (built with the tests) runs the plug-in's analysis and correction logic on audio files:

```
build/linux-release/EyraEQAnalyze_artefacts/Release/EyraEQAnalyze --instrument "Snare drum" --strength Moderate snare.wav
```

## Linux

Tested on Ubuntu 24.04 with GCC 13.3. Packages:

```bash
sudo apt install build-essential cmake ninja-build git xvfb \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxi-dev libegl-dev libglu1-mesa-dev mesa-common-dev
```

VST3 install locations: `~/.vst3/` (user) or `/usr/lib/vst3/` (system). Copy the whole
`EyraEQ.vst3` folder.

## macOS

Xcode 15 or newer. The preset builds a Universal binary with a minimum macOS version of 12.0.

VST3 install location: `~/Library/Audio/Plug-Ins/VST3/` or `/Library/Audio/Plug-Ins/VST3/`.

**Signing and notarisation.** Builds are unsigned. Gatekeeper blocks unsigned plug-ins downloaded from
the internet. To distribute:

1. Sign with a Developer ID Application certificate and the hardened runtime:
   `codesign --force --deep --options runtime --timestamp --sign "Developer ID Application: <Name> (<TEAMID>)" EyraEQ.vst3`
   (also sign `EyraEQ.app`).
2. Zip or package, then submit: `xcrun notarytool submit EyraEQ.zip --apple-id <id> --team-id <TEAMID> --password <app-specific-password> --wait`
3. Staple: `xcrun stapler staple EyraEQ.vst3` (and the .app / .pkg / .dmg).

None of this has been done yet. CI artefacts are unsigned and not notarised.

## Windows

Visual Studio 2022 (Desktop development with C++), CMake (bundled with VS is fine).

VST3 install location: `C:\Program Files\Common Files\VST3\`.

## Continuous integration

`.github/workflows/build.yml` builds all three platforms on GitHub-hosted runners, runs `ctest`
(unit tests and the VST3 host test), runs pluginval at strictness 10, and uploads the unsigned VST3 and
Standalone builds as workflow artefacts.

## Installers

Installers (.exe, .dmg/.pkg, .AppImage/.deb) are Phase 6 and do not exist yet.
