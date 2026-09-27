#!/usr/bin/env bash
# Build (and optionally install) mlacker, its VST3 plugins and, when asked, the
# full MLang toolchain, using the MLang checkout chosen by ./bootstrap.sh
# (see mlacker.conf).
set -eu

cd "$(dirname "$0")"
ROOT_DIR=$(pwd)
CONFIG_FILE="$ROOT_DIR/mlacker.conf"

all=""
do_app=""
do_plugins=""
do_mlang=""
install=""
update=""
run_tests=""
jobs=""

usage() {
    cat <<EOF
Usage: ./build.sh [--all | --app | --plugins | --mlang ...] [--install] [--test] [--update] [-j N]

Targets (combine freely; none given means --all):
  --all        mlacker and all plugins, plus the full MLang toolchain when
               MLANG_TOOLCHAIN="yes" in mlacker.conf (default)
  --app        The mlacker tracker
  --plugins    The VST3 plugins
  --mlang      The full MLang toolchain in the configured checkout (compiler,
               runtime, mlang-config, mlangd-mla, mlang-format, mlang-frontend,
               mlangpkg), built with MLang's own build.sh

Options:
  --install    Also install: MLang under MLANG_PREFIX, mlacker to BIN_DIR,
               plugins to PLUGIN_DIR
  --test       After building, run the tests of the selected targets: mlacker's
               MLang unit tests and CTest suite (--app), each plugin's test
               task (--plugins)
  --update     Fetch the latest MLang into subprojects/mlang (subproject mode)
               and fast-forward its current branch. Local commits and
               uncommitted changes are kept; a diverged branch is reported,
               not merged. Alone it only updates; with targets it updates,
               then builds
  -j, --jobs N Parallel jobs for building MLang
  -h, --help   Show this help

Examples:
  ./build.sh                     # build everything
  ./build.sh --app               # build only mlacker
  ./build.sh --install --all     # build and install everything
  ./build.sh --install --app     # build and install only mlacker (to BIN_DIR)
  ./build.sh --install --plugins # build and install only the plugins
  ./build.sh --install --mlang   # build and install only MLang (e.g. subprojects/mlang)
  ./build.sh --update            # pull the latest MLang into subprojects/mlang
  ./build.sh --update --all      # pull the latest MLang, then build everything
  ./build.sh --test              # build everything, then run all tests

Without --mlang only the MLang compiler and runtime that mlacker links against
are built. Configuration comes from mlacker.conf; run ./bootstrap.sh to create
or change it.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --all) all="1"; shift ;;
        --app) do_app="1"; shift ;;
        --plugins) do_plugins="1"; shift ;;
        --mlang) do_mlang="1"; shift ;;
        --install) install="1"; shift ;;
        --update) update="1"; shift ;;
        --test) run_tests="1"; shift ;;
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
mlang_toolchain=$(conf_get MLANG_TOOLCHAIN)
mlang_prefix=$(conf_get MLANG_PREFIX)
plugin_dir=$(conf_get PLUGIN_DIR)
bin_dir=$(conf_get BIN_DIR)
[ -n "$mlang_dir" ] || { echo "build.sh: MLANG_DIR is not set in $CONFIG_FILE" >&2; exit 1; }
[ -n "$mlang_prefix" ] || mlang_prefix="~/.local"
[ -n "$plugin_dir" ] || plugin_dir="~/.local/plugins/VST3"
[ -n "$bin_dir" ] || bin_dir="~/.local/bin"

update_only=""
if [ -n "$update" ] && [ -z "$all" ] && [ -z "$do_app" ] && [ -z "$do_plugins" ] && [ -z "$do_mlang" ]; then
    update_only="1"
fi
if [ -n "$all" ] || { [ -z "$update_only" ] && [ -z "$do_app" ] && [ -z "$do_plugins" ] && [ -z "$do_mlang" ]; }; then
    do_app="1"
    do_plugins="1"
    [ "$mlang_toolchain" = "yes" ] && do_mlang="1"
fi

mlang_root=$(expand_path "$mlang_dir")
[ -d "$mlang_root" ] || { echo "build.sh: MLang checkout not found: $mlang_root (run ./bootstrap.sh)" >&2; exit 1; }
mlang_root=$(cd "$mlang_root" && pwd)
mlang_prefix=$(expand_path "$mlang_prefix")
plugin_dir=$(expand_path "$plugin_dir")
bin_dir=$(expand_path "$bin_dir")
mlang_build="$mlang_root/build"

run() {
    echo "+ $*"
    "$@"
}

# --- Update the MLang subproject -------------------------------------------------------
update_mlang() {
    if [ "$mlang_mode" != "subproject" ]; then
        echo "[mlacker] --update only updates the subprojects/mlang clone; $mlang_root is"
        echo "[mlacker] your own MLang checkout (external mode), update it yourself."
        return 0
    fi
    if [ ! -d "$mlang_root/.git" ]; then
        echo "build.sh: $mlang_root is not a git checkout; run ./bootstrap.sh" >&2
        return 1
    fi
    local branch upstream behind ahead
    branch=$(git -C "$mlang_root" symbolic-ref --quiet --short HEAD || true)
    if [ -z "$branch" ]; then
        echo "build.sh: $mlang_root is on a detached HEAD; check out a branch to update it" >&2
        return 1
    fi
    echo "[mlacker] Updating MLang in $mlang_root (branch $branch)"
    run git -C "$mlang_root" fetch --prune origin
    upstream=$(git -C "$mlang_root" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)
    if [ -z "$upstream" ]; then
        echo "[mlacker] Branch $branch has no upstream; fetched only."
        return 0
    fi
    behind=$(git -C "$mlang_root" rev-list --count "HEAD..$upstream")
    ahead=$(git -C "$mlang_root" rev-list --count "$upstream..HEAD")
    if [ "$behind" = "0" ]; then
        if [ "$ahead" = "0" ]; then
            echo "[mlacker] MLang is up to date with $upstream"
        else
            echo "[mlacker] MLang is up to date with $upstream ($ahead local commit(s) not pushed)"
        fi
        return 0
    fi
    if [ "$ahead" != "0" ]; then
        echo "build.sh: $branch has $ahead local and $behind upstream commit(s) in $mlang_root;" >&2
        echo "build.sh: rebase or merge there (git -C $mlang_dir pull --rebase), then rebuild." >&2
        return 1
    fi
    # --ff-only never merges; git refuses if uncommitted changes would be overwritten.
    run git -C "$mlang_root" merge --ff-only "$upstream"
    echo "[mlacker] MLang updated to $(git -C "$mlang_root" log --oneline -1)"
}

if [ -n "$update" ]; then
    update_mlang
    if [ -n "$update_only" ]; then
        exit 0
    fi
fi

# --- MLang ---------------------------------------------------------------------------------
echo "[mlacker] MLang checkout: $mlang_root ($mlang_mode)"
if [ ! -f "$mlang_build/CMakeCache.txt" ]; then
    generator=""
    command -v ninja >/dev/null 2>&1 && generator="-G Ninja"
    # shellcheck disable=SC2086
    run cmake -S "$mlang_root" -B "$mlang_build" -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release $generator
fi
if [ -n "$jobs" ]; then
    run cmake --build "$mlang_build" --target mlang mlang_std mlang-config --parallel "$jobs"
else
    run cmake --build "$mlang_build" --target mlang mlang_std mlang-config
fi

if [ -n "$do_mlang" ]; then
    # MLang's build.sh reads build/mlang-config.conf; create one on first use
    # instead of letting it start its interactive bootstrap.
    if [ ! -f "$mlang_build/mlang-config.conf" ]; then
        (cd "$mlang_root" && run build/mlang-config --build-dir build \
            --install-prefix "$mlang_prefix" --bin-dir "$mlang_prefix/bin" --write)
    fi
    install_flag="--no-install"
    [ -n "$install" ] && install_flag="--install"
    echo "[mlacker] Building the full MLang toolchain${install:+ and installing it under $mlang_prefix}"
    if [ -n "$jobs" ]; then
        (cd "$mlang_root" && run ./build.sh "$install_flag" --prefix "$mlang_prefix" \
            --bin-dir "$mlang_prefix/bin" --jobs "$jobs")
    else
        (cd "$mlang_root" && run ./build.sh "$install_flag" --prefix "$mlang_prefix" \
            --bin-dir "$mlang_prefix/bin")
    fi
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

if [ -n "$do_app" ] && [ -n "$do_plugins" ] && [ -n "$install" ]; then
    pkg install-all
else
    if [ -n "$do_app" ]; then
        if [ -n "$install" ]; then pkg install; else pkg build; fi
    fi
    if [ -n "$do_plugins" ]; then
        if [ -n "$install" ]; then pkg install-plugins; else pkg build-plugins; fi
    fi
fi

if [ -n "$run_tests" ]; then
    # Plugins first: mlacker's CTest suite includes the Mla Drum/Delay host tests
    # only when those bundles exist when it is configured.
    if [ -n "$do_plugins" ]; then
        for plugin in $plugins; do
            if grep -q '^name = "test"' "plugins/$plugin/mlang.toml"; then
                echo "[mlacker] Testing $plugin"
                run "$mlang" pkg --config "$ROOT_DIR/plugins/$plugin/mlang.toml" run test \
                    --option mlang_root="$mlang_root"
            fi
        done
    fi
    if [ -n "$do_app" ]; then
        echo "[mlacker] Testing mlacker (MLang unit tests, then CTest)"
        pkg test
    fi
fi

built=""
[ -n "$do_mlang" ] && built="MLang"
[ -n "$do_app" ] && built="${built:+$built, }mlacker"
[ -n "$do_plugins" ] && built="${built:+$built, }plugins"
echo "[mlacker] Done: $built${install:+ (installed)}${run_tests:+ (tested)}"
if [ -n "$do_app" ] && [ -z "$install" ]; then
    echo "[mlacker] Run with: build/cmake/bin/mlacker"
fi
