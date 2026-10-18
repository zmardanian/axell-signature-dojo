#!/usr/bin/env bash
# Configures and builds inside the juce-build distrobox. Usage: scripts/build.sh [Release|Debug] [extra cmake args]
set -euo pipefail
cd "$(dirname "$0")/.."
BOX=juce-build
CFG="${1:-Release}"
shift || true
DIR="$PWD"
BUILD="build"
[ "$CFG" = "Debug" ] && BUILD="build-debug"
# reuse an already downloaded JUCE 8.0.15 next door (no network needed)
JUCE_SRC=""
for d in "$DIR/../axell-penta-eq/build/_deps/juce-src" "$DIR/../axell-duo-compressor/build/_deps/juce-src"; do
  [ -f "$d/CMakeLists.txt" ] && { JUCE_SRC="-DFETCHCONTENT_SOURCE_DIR_JUCE=$(realpath "$d")"; break; }
done
# -j8: the plugin builds taught that 16 compile jobs plus a benchmark can run the machine out of memory
distrobox enter "$BOX" -- bash -lc "cd '$DIR' && cmake -B '$BUILD' -G Ninja -DCMAKE_BUILD_TYPE=$CFG $JUCE_SRC $* && cmake --build '$BUILD' -j8"
