#!/bin/sh
# Build the pad-bank audition project using mlacker's built native host.
set -eu
cd "$(dirname "$0")/../.."
task_root=$PWD
task_filter=0
case "${1:-pads}" in
    pads) task_source=examples/jp8000_pads/generate.mla ;;
    keyfollow) task_source=examples/jp8000_pads/keyfollow.mla ;;
    filter24) task_source=examples/jp8000_pads/keyfollow.mla; task_filter=1 ;;
    obsession) task_source=examples/jp8000_pads/obsession.mla ;;
    detune) task_source=examples/jp8000_pads/detune.mla ;;
    lead) task_source=examples/jp8000_pads/trance_lead.mla ;;
    sub) task_source=examples/jp8000_pads/sub_resonance.mla ;;
    *) echo "Usage: $0 [pads|keyfollow|filter24|obsession|detune|lead|sub]" >&2; exit 2 ;;
esac
task_work="$task_root/build/jp8000"
mkdir -p "$task_work"
if [ ! -d "$task_root/build/plugins/VST3/Release/MlaJP8000.vst3" ]; then
    echo "Build MlaJP8000 first: ./build.sh --plugins" >&2
    exit 1
fi
cp "$task_source" src/zz_jp8000_pad_generator.mla
trap 'rm -f "$task_root/src/zz_jp8000_pad_generator.mla"' EXIT
trap 'exit 130' INT TERM
subprojects/mlang/build/mlang -c src/zz_jp8000_pad_generator.mla -O2 -Wno-unwrap -Wno-colon-if -o "$task_work/generate.o"
cmake -S . -B "$task_work/generator" -DCMAKE_BUILD_TYPE=Release \
    -DVST3_SDK_ROOT="$task_root/build/deps/vst3sdk" \
    -DMLACKER_MAIN_OBJECT="$task_work/generate.o" -DMLANG_ROOT="$task_root/subprojects/mlang"
cmake --build "$task_work/generator" --target mlacker -j4
if [ -n "${JP8000_RENDER_DIR:-}" ]; then mkdir -p "$JP8000_RENDER_DIR"; fi
JP8000_ROOT="$task_root" JP8000_FILTER_DEMO="$task_filter" "$task_work/generator/bin/mlacker"

# Check the rendered signal, in addition to the generator's parameter checks.
if [ -n "${JP8000_RENDER_DIR:-}" ]; then
    case "${1:-pads}" in
        lead) python3 tests/jp8000_sweep_audio.py "$JP8000_RENDER_DIR/out_of_the_blue_style.wav" ;;
        sub) python3 tests/jp8000_sweep_audio.py "$JP8000_RENDER_DIR/sub_resonance.wav" ;;
    esac
fi
