#!/bin/sh
# Regenerate a JV-style pad demo project and open it in mlacker.
#
#   examples/jv_pads/build.sh atmosphere   # examples/atmosphere_pad.mlaproj
#   examples/jv_pads/build.sh footprint    # examples/footprint_demo.mlaproj
#
# Synthesizes the pad samples (make_pad.py), compiles the song generator
# (<song>_song.mla) against mlacker's modules and VST3 host, runs it to load
# the installed Mla plugins (~/.local/plugins/VST3) and save the project,
# then opens the project. Run ./build.sh (and install the plugins) first.
set -e
song=${1:-footprint}
case "$song" in
    atmosphere) project=examples/atmosphere_pad.mlaproj; presets="atmosphere dawn2dusk footprint97" ;;
    footprint) project=examples/footprint_demo.mlaproj; presets="footprint97 dawn2dusk jd800pluck" ;;
    *) echo "Usage: $0 atmosphere|footprint" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/../.."
root=$PWD
work=$root/build/jv_pads
mkdir -p "$work/samples"
for preset in $presets; do
    case "$preset" in
        atmosphere) name="Atmosphere Pad" ;;
        dawn2dusk) name="Dawn 2 Dusk" ;;
        footprint97) name="Footprint 97" ;;
        jd800pluck) name="JD-800 Pluck" ;;
    esac
    python3 -I examples/jv_pads/make_pad.py "$preset" "$work/samples/$name.wav"
done
# The generator imports mlacker_ui modules, which resolve from src/.
cp "examples/jv_pads/${song}_song.mla" "src/zz_${song}_song.mla"
trap 'rm -f "$root/src/zz_${song}_song.mla"' EXIT
subprojects/mlang/build/mlang -c "src/zz_${song}_song.mla" -o "$work/$song.o" -O2 -Wno-unwrap -Wno-colon-if >/dev/null
cmake -S . -B "$work/cmake_$song" -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT="$root/build/deps/vst3sdk" \
    -DMLACKER_MAIN_OBJECT="$work/$song.o" -DMLANG_ROOT="$root/subprojects/mlang" >/dev/null
cmake --build "$work/cmake_$song" --target mlacker -j8 >/dev/null
rm -rf "$project"
PAD_DIR="$work/samples" "$work/cmake_$song/bin/mlacker"
build/cmake/bin/mlacker "$project"
