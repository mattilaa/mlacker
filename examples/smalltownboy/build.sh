#!/bin/sh
# Generate the separate Smalltown Boy bass project and offline preview.
set -eu
cd "$(dirname "$0")/../.."
root=$PWD
work="$root/build/smalltownboy"
mkdir -p "$work"
subprojects/mlang/build/mlang pkg --config plugins/mla_mini/mlang.toml run compile-dsp
subprojects/mlang/build/mlang pkg --config plugins/mla_eq/mlang.toml run compile-dsp
cmake --build build/plugins --target MlaMini MlaEQ -j4
cp examples/smalltownboy/song.mla src/zz_smalltownboy_song.mla
trap 'rm -f "$root/src/zz_smalltownboy_song.mla"' EXIT
trap 'exit 130' INT TERM
subprojects/mlang/build/mlang -c src/zz_smalltownboy_song.mla -o "$work/song.o" -O2 -Wno-unwrap -Wno-colon-if
cmake -S . -B "$work/cmake" -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT="$root/build/deps/vst3sdk" \
    -DMLACKER_MAIN_OBJECT="$work/song.o" -DMLANG_ROOT="$root/subprojects/mlang"
cmake --build "$work/cmake" --target mlacker -j4
MINI_VST3="build/plugins/VST3/Release/MlaMini.vst3" EQ_VST3="build/plugins/VST3/Release/MlaEQ.vst3" \
    MLANG_TUI_NO_HARDWARE=1 "$work/cmake/bin/mlacker"
if [ "${NO_OPEN:-1}" = 0 ]; then build/cmake/bin/mlacker examples/smalltownboy.mlaproj; fi
