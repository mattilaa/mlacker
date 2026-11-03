#!/bin/sh
# Build the pad-bank audition project using mlacker's built native host.
set -eu
cd "$(dirname "$0")/../.."
task_root=$PWD
task_work="$task_root/build/jp8000"
mkdir -p "$task_work"
if [ ! -d "$task_root/build/plugins/VST3/Release/MlaJP8000.vst3" ]; then
    echo "Build MlaJP8000 first: ./build.sh --plugins" >&2
    exit 1
fi
cp examples/jp8000_pads/generate.mla src/zz_jp8000_pad_generator.mla
trap 'rm -f "$task_root/src/zz_jp8000_pad_generator.mla"' EXIT
trap 'exit 130' INT TERM
subprojects/mlang/build/mlang -c src/zz_jp8000_pad_generator.mla -O2 -Wno-unwrap -Wno-colon-if -o "$task_work/generate.o"
cmake -S . -B "$task_work/generator" -DCMAKE_BUILD_TYPE=Release \
    -DVST3_SDK_ROOT="$task_root/build/deps/vst3sdk" \
    -DMLACKER_MAIN_OBJECT="$task_work/generate.o" -DMLANG_ROOT="$task_root/subprojects/mlang"
cmake --build "$task_work/generator" --target mlacker -j4
JP8000_ROOT="$task_root" "$task_work/generator/bin/mlacker"
