#!/usr/bin/env bash
# Configure, build and test EyraEQ on Linux.
#   Scripts/build-linux.sh            Release build + tests
#   Scripts/build-linux.sh debug      Debug build + tests
# Set JUCE_DIR=/path/to/JUCE to use a local JUCE checkout instead of fetching it.
set -euo pipefail
cd "$(dirname "$0")/.."

config="${1:-release}"
preset="linux-${config}"

extra=()
if [[ -n "${JUCE_DIR:-}" ]]; then
  extra+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=${JUCE_DIR}")
fi

cmake --preset "$preset" "${extra[@]}"
cmake --build --preset "$preset" --parallel

# The editor tests need a display; use Xvfb when none is available.
if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run > /dev/null; then
  xvfb-run -a ctest --preset "$preset"
else
  ctest --preset "$preset"
fi

echo
echo "VST3:       build/${preset}/EyraEQ_artefacts/*/VST3/EyraEQ.vst3"
echo "Standalone: build/${preset}/EyraEQ_artefacts/*/Standalone/EyraEQ"
