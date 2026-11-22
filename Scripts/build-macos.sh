#!/usr/bin/env bash
# Configure, build (Universal: arm64 + x86_64) and test EyraEQ on macOS with Xcode.
# Set JUCE_DIR=/path/to/JUCE to use a local JUCE checkout instead of fetching it.
# Output is unsigned; see Documentation/Building.md for signing and notarisation.
set -euo pipefail
cd "$(dirname "$0")/.."

extra=()
if [[ -n "${JUCE_DIR:-}" ]]; then
  extra+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=${JUCE_DIR}")
fi

cmake --preset macos "${extra[@]}"
cmake --build --preset macos-release --parallel
ctest --preset macos-release

echo
echo "VST3:       build/macos/EyraEQ_artefacts/Release/VST3/EyraEQ.vst3"
echo "Standalone: build/macos/EyraEQ_artefacts/Release/Standalone/EyraEQ.app"
