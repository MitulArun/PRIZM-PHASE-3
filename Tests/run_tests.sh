#!/usr/bin/env bash
# PRIZM V2 - build and run the offline DSP/assistant tests (no JUCE, no DAW needed).
# Usage: Tests/run_tests.sh [output-dir]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="${1:-$root/Builds/Tests}"
mkdir -p "$out"

g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I"$root/Source" \
    "$here/TestMain.cpp" "$here/DSPTests.cpp" "$here/AssistantTests.cpp" \
    "$root"/Source/Common/*.cpp \
    "$root"/Source/DSP/*.cpp \
    $(ls "$root"/Source/Assistant/*.cpp 2>/dev/null || true) \
    -o "$out/prizm_tests"

"$out/prizm_tests"
