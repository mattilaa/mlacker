#!/usr/bin/env bash
# Build (and optionally install) mlacker and its VST3 plugins against the MLang
# toolchain chosen by ./bootstrap.sh (see mlacker.conf).
set -eu

cd "$(dirname "$0")"
ROOT_DIR=$(pwd)
CONFIG_FILE="$ROOT_DIR/mlacker.conf"

target="all"   # all | app | plugins
install=""
jobs=""

usage() {
    cat <<EOF
Usage: ./build.sh [--all | --app | --plugins] [--install] [-j N]

  --all        Build mlacker and all plugins (default)
  --app        Build only the mlacker tracker
  --plugins    Build only the VST3 plugins
  --install    Also install: mlacker to BIN_DIR, plugins to PLUGIN_DIR
  -j, --jobs N Parallel jobs for building the MLang toolchain
  -h, --help   Show this help

Examples:
  ./build.sh                     # build everything
  ./build.sh --install --all     # build and install everything
  ./build.sh --install --plugins # build and install only the plugins

Configuration comes from mlacker.conf; run ./bootstrap.sh to create or change it.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --all) target="all"; shift ;;
        --app) target="app"; shift ;;
        --plugins) target="plugins"; shift ;;
        --install) install="1"; shift ;;
        -j|--jobs)
            [ "$#" -ge 2 ] || { echo "build.sh: $1 requires a value" >&2; exit 2; }
            jobs="$2"; shift 2 ;;
        --jobs=*) jobs="${1#*=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "build.sh: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ ! -f "$CONFIG_FILE" ]; then
    echo "build.sh: $CONFIG_FILE not found; run ./bootstrap.sh first." >&2
    exit 1
fi

# Read KEY="value" lines without executing the file.
conf_get() {
    sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" "$CONFIG_FILE" | tail -1
}

expand_path() {
    case "$1" in
        "~") printf '%s\n' "$HOME" ;;
        "~"/*) printf '%s\n' "$HOME/${1#\~/}" ;;
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$ROOT_DIR/$1" ;;
    esac
}

mlang_mode=$(conf_get MLANG_MODE)
mlang_dir=$(conf_get MLANG_DIR)
plugin_dir=$(conf_get PLUGIN_DIR)
bin_dir=$(conf_get BIN_DIR)
[ -n "$mlang_dir" ] || { echo "build.sh: MLANG_DIR is not set in $CONFIG_FILE" >&2; exit 1; }
[ -n "$plugin_dir" ] || plugin_dir="~/.local/plugins/VST3"
[ -n "$bin_dir" ] || bin_dir="~/.local/bin"

mlang_root=$(expand_path "$mlang_dir")
[ -d "$mlang_root" ] || { echo "build.sh: MLang checkout not found: $mlang_root (run ./bootstrap.sh)" >&2; exit 1; }
mlang_root=$(cd "$mlang_root" && pwd)
plugin_dir=$(expand_path "$plugin_dir")
bin_dir=$(expand_path "$bin_dir")

run() {
    echo "+ $*"
    "$@"
}

# --- MLang toolchain: seed compiler and runtime -------------------------------------
echo "[mlacker] MLang toolchain: $mlang_root ($mlang_mode)"
mlang_build="$mlang_root/build"
if [ ! -f "$mlang_build/CMakeCache.txt" ]; then
    generator=""
    command -v ninja >/dev/null 2>&1 && generator="-G Ninja"
    # shellcheck disable=SC2086
    run cmake -S "$mlang_root" -B "$mlang_build" -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release $generator
fi
if [ -n "$jobs" ]; then
    run cmake --build "$mlang_build" --target mlang mlang_std --parallel "$jobs"
else
    run cmake --build "$mlang_build" --target mlang mlang_std
fi
mlang="$mlang_build/mlang"

# --- mlacker and plugins ---------------------------------------------------------------
plugins=""
for manifest in plugins/*/mlang.toml; do
    [ -f "$manifest" ] || continue
    name=$(basename "$(dirname "$manifest")")
    plugins="${plugins:+$plugins }$name"
done

pkg() {
    run "$mlang" pkg --config "$ROOT_DIR/mlang.toml" run "$1" \
        --option mlang_root="$mlang_root" \
        --option plugin_dir="$plugin_dir" \
        --option bin_dir="$bin_dir" \
        --option plugins="$plugins"
}

case "$target:$install" in
    all:) pkg build; pkg build-plugins ;;
    all:1) pkg install-all ;;
    app:) pkg build ;;
    app:1) pkg install ;;
    plugins:) pkg build-plugins ;;
    plugins:1) pkg install-plugins ;;
esac

echo "[mlacker] Done: $target${install:+ (installed)}"
if [ -z "$install" ] && [ "$target" != "plugins" ]; then
    echo "[mlacker] Run with: build/cmake/bin/mlacker"
fi
