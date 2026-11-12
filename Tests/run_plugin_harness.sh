#!/usr/bin/env bash
# PRIZM V2 - build and run the end-to-end plugin harness against the real plugin code.
# Requires a prior Linux build:  cd Builds/LinuxMakefile && make CONFIG=Debug   (or Release)
# Usage: Tests/run_plugin_harness.sh [output-dir] [Debug|Release]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="${1:-$root/Builds/Tests}"
config="${2:-Debug}"
mkdir -p "$out"
cd "$root/Builds/LinuxMakefile"

# Reuse the exact compiler flags the Makefile uses for the plugin's shared code.
flags="$(make -n -B CONFIG="$config" "build/intermediate/$config/PluginProcessor_a059e380.o" 2>/dev/null \
         | grep '^g++' | sed 's/ -o "[^"]*"//; s/ -c "[^"]*"//; s/^g++//; s/-MMD//')"

eval g++ $flags -c "$here/PluginHarness.cpp" -o "$out/PluginHarness.o"
g++ -o "$out/prizm_plugin_harness" "$out/PluginHarness.o" build/prizmv1.a \
    $(pkg-config --libs alsa freetype2 fontconfig libcurl) -lrt -ldl -lpthread

"$out/prizm_plugin_harness" "$out"
