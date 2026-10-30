#!/usr/bin/env bash
# Regenerate the standalone project using mlacker's configured MLang release.
set -eu
cd "$(dirname "$0")/../.."
root=$PWD
variant=${1:-title}
case "$variant" in
    title) source=examples/turbo_outrun/song.mla ;;
    loading) source=examples/turbo_outrun/loading_song.mla ;;
    *) echo "Usage: $0 [title|loading]" >&2; exit 2 ;;
esac
work="$root/build/turbo_outrun/$variant"
mkdir -p "$work"
source_copy="$root/src/zz_turbo_outrun_${variant}_song.mla"
if [ -e "$source_copy" ]; then
    echo "Temporary generator source already exists: $source_copy" >&2
    exit 1
fi
cp "$source" "$source_copy"
trap 'rm -f "$source_copy"' EXIT
trap 'exit 130' INT TERM
subprojects/mlang/build/mlang -c "$source_copy" -o "$work/song.o" -O2 -Wno-unwrap -Wno-colon-if
cmake -S . -B "$work/cmake" -DCMAKE_BUILD_TYPE=Release \
    -DVST3_SDK_ROOT="$root/build/deps/vst3sdk" \
    -DMLACKER_MAIN_OBJECT="$work/song.o" -DMLANG_ROOT="$root/subprojects/mlang"
cmake --build "$work/cmake" --target mlacker --parallel 4
"$work/cmake/bin/mlacker"
