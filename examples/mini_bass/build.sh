#!/bin/sh
# Build Mla Mini, generate its playable project and render a dry WAV preview.
set -eu
cd "$(dirname "$0")/../.."
root=$PWD
work="$root/build/mini_bass"
mkdir -p "$work"
subprojects/mlang/build/mlang pkg --config plugins/mla_mini/mlang.toml run compile-dsp
# Use the shared SDK build, preserving the plugins already configured there.
plugins=$(sed -n 's/^MLA_PLUGINS:UNINITIALIZED=//p' build/plugins/CMakeCache.txt)
case " $plugins " in *" mla_mini "*) ;; *) plugins="$plugins mla_mini" ;; esac
cmake -S plugins -B build/plugins -DCMAKE_BUILD_TYPE=Release \
    -DVST3_SDK_ROOT="$root/build/deps/vst3sdk" -DMLANG_ROOT="$root/subprojects/mlang" -DMLA_PLUGINS="$plugins"
cmake --build build/plugins --target MlaMini -j4
cp examples/mini_bass/song.mla src/zz_mini_bass_song.mla
trap 'rm -f "$root/src/zz_mini_bass_song.mla"' EXIT
trap 'exit 130' INT TERM
subprojects/mlang/build/mlang -c src/zz_mini_bass_song.mla -o "$work/song.o" -O2 -Wno-unwrap -Wno-colon-if
cmake -S . -B "$work/cmake" -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT="$root/build/deps/vst3sdk" \
    -DMLACKER_MAIN_OBJECT="$work/song.o" -DMLANG_ROOT="$root/subprojects/mlang"
cmake --build "$work/cmake" --target mlacker -j4
MINI_VST3="build/plugins/VST3/Release/MlaMini.vst3" MLANG_TUI_NO_HARDWARE=1 "$work/cmake/bin/mlacker"
if [ "${NO_OPEN:-1}" = 0 ]; then build/cmake/bin/mlacker examples/mini_bass.mlaproj; fi
