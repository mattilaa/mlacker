#!/usr/bin/env bash
# Configure where mlacker gets its MLang toolchain and where it installs, then
# optionally build. Writes mlacker.conf, which build.sh reads.
set -eu

cd "$(dirname "$0")"
ROOT_DIR=$(pwd)
CONFIG_FILE="$ROOT_DIR/mlacker.conf"
SUBPROJECT_DIR="subprojects/mlang"

mode=""          # subproject | external
mlang_dir=""
mlang_repo=""
mlang_branch=""
plugin_dir=""
bin_dir=""
build_answer=""  # yes | no | "" (ask)
install_answer=""
assume_defaults=""

usage() {
    cat <<EOF
Usage: ./bootstrap.sh [options]

Chooses the MLang toolchain mlacker builds against and the install locations,
writes mlacker.conf, and offers to run ./build.sh.

MLang toolchain (pick one):
  --mlang-subproject    Clone MLang's development repository into
                        $SUBPROJECT_DIR. Make stdlib/compiler changes needed by
                        mlacker there and commit them to the MLang repository.
  --mlang-dir DIR       Use an existing MLang checkout (e.g. ../mlang); changes
                        to MLang are made in that checkout.
  --mlang-repo URL      Repository to clone for --mlang-subproject
  --mlang-branch NAME   Branch to clone (default: main)

Install locations:
  --plugin-dir DIR      VST3 plugin install directory (default: ~/.local/plugins/VST3)
  --bin-dir DIR         mlacker binary install directory (default: ~/.local/bin)

After configuring:
  --build | --no-build      Build (./build.sh) without asking, or skip it
  --install | --no-install  Install after building, or skip it
  -y, --yes                 Accept defaults for everything not given on the
                            command line (no prompts)
  -h, --help                Show this help
EOF
}

need_value() {
    if [ "$2" -lt 2 ]; then
        echo "bootstrap.sh: $1 requires a value" >&2
        exit 2
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --mlang-subproject) mode="subproject"; shift ;;
        --mlang-dir) need_value "$1" "$#"; mode="external"; mlang_dir="$2"; shift 2 ;;
        --mlang-dir=*) mode="external"; mlang_dir="${1#*=}"; shift ;;
        --mlang-repo) need_value "$1" "$#"; mlang_repo="$2"; shift 2 ;;
        --mlang-repo=*) mlang_repo="${1#*=}"; shift ;;
        --mlang-branch) need_value "$1" "$#"; mlang_branch="$2"; shift 2 ;;
        --mlang-branch=*) mlang_branch="${1#*=}"; shift ;;
        --plugin-dir) need_value "$1" "$#"; plugin_dir="$2"; shift 2 ;;
        --plugin-dir=*) plugin_dir="${1#*=}"; shift ;;
        --bin-dir) need_value "$1" "$#"; bin_dir="$2"; shift 2 ;;
        --bin-dir=*) bin_dir="${1#*=}"; shift ;;
        --build) build_answer="yes"; shift ;;
        --no-build) build_answer="no"; shift ;;
        --install) install_answer="yes"; shift ;;
        --no-install) install_answer="no"; shift ;;
        -y|--yes) assume_defaults="1"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "bootstrap.sh: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

interactive() {
    [ -z "$assume_defaults" ] && [ -t 0 ]
}

# ask VAR "Question" "default"
ask() {
    local answer=""
    if interactive; then
        printf "%s [%s]: " "$2" "$3"
        IFS= read -r answer || answer=""
    fi
    [ -n "$answer" ] || answer="$3"
    eval "$1=\$answer"
}

# confirm "Question" y|n -> status 0 for yes
confirm() {
    local answer="" hint="y/N"
    [ "$2" = "y" ] && hint="Y/n"
    if ! interactive; then
        [ "$2" = "y" ]
        return
    fi
    while true; do
        printf "%s [%s]: " "$1" "$hint"
        IFS= read -r answer || answer=""
        [ -n "$answer" ] || answer="$2"
        case "$answer" in
            y|Y|yes) return 0 ;;
            n|N|no) return 1 ;;
        esac
    done
}

# Earlier answers are the defaults on a re-run.
conf_get() {
    [ -f "$CONFIG_FILE" ] || return 0
    sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" "$CONFIG_FILE" | tail -1
}

prev_mode=$(conf_get MLANG_MODE)
prev_dir=$(conf_get MLANG_DIR)

is_mlang_checkout() {
    [ -f "$1/CMakeLists.txt" ] && [ -f "$1/src/parser.y" ]
}

# --- MLang toolchain ---------------------------------------------------------
if [ -z "$mode" ]; then
    default_mode="${prev_mode:-}"
    if [ -z "$default_mode" ]; then
        if is_mlang_checkout "../mlang"; then default_mode="external"; else default_mode="subproject"; fi
    fi
    echo "MLang toolchain:"
    echo "  subproject: clone MLang's development repository into $SUBPROJECT_DIR;"
    echo "              commit MLang changes needed by mlacker there"
    echo "  external:   use your own MLang checkout and make MLang changes there"
    while true; do
        ask mode "Use subproject or external" "$default_mode"
        case "$mode" in
            s|sub|subproject) mode="subproject"; break ;;
            e|ext|external) mode="external"; break ;;
        esac
        interactive || { echo "bootstrap.sh: invalid MLang mode: $mode" >&2; exit 2; }
        echo "Please answer subproject or external."
    done
fi

if [ "$mode" = "subproject" ]; then
    mlang_dir="$SUBPROJECT_DIR"
    if [ -z "$mlang_repo" ]; then
        default_repo=$(conf_get MLANG_REPO)
        if [ -z "$default_repo" ]; then
            case "$(git remote get-url origin 2>/dev/null || true)" in
                git@*) default_repo="git@github.com:mattilaa/mlang.git" ;;
                *) default_repo="https://github.com/mattilaa/mlang.git" ;;
            esac
        fi
        if [ -d "$SUBPROJECT_DIR/.git" ]; then
            mlang_repo="$default_repo"
        else
            ask mlang_repo "MLang repository to clone" "$default_repo"
        fi
    fi
    if [ -z "$mlang_branch" ]; then
        default_branch=$(conf_get MLANG_BRANCH)
        if [ -d "$SUBPROJECT_DIR/.git" ]; then
            mlang_branch="${default_branch:-main}"
        else
            ask mlang_branch "MLang branch" "${default_branch:-main}"
        fi
    fi
    if [ -d "$SUBPROJECT_DIR/.git" ]; then
        current=$(git -C "$SUBPROJECT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")
        echo "Using the existing MLang checkout in $SUBPROJECT_DIR (branch $current); not touching it."
    else
        mkdir -p "$(dirname "$SUBPROJECT_DIR")"
        echo "git clone --branch $mlang_branch $mlang_repo $SUBPROJECT_DIR"
        git clone --branch "$mlang_branch" "$mlang_repo" "$SUBPROJECT_DIR"
    fi
else
    if [ -z "$mlang_dir" ]; then
        default_dir="../mlang"
        [ "$prev_mode" = "external" ] && [ -n "$prev_dir" ] && default_dir="$prev_dir"
        ask mlang_dir "Path to your MLang checkout" "$default_dir"
    fi
    case "$mlang_dir" in "~"/*) mlang_dir="$HOME/${mlang_dir#\~/}" ;; esac
fi

if ! is_mlang_checkout "$mlang_dir"; then
    echo "bootstrap.sh: $mlang_dir is not an MLang checkout (no CMakeLists.txt + src/parser.y)" >&2
    exit 1
fi

# --- Install locations ---------------------------------------------------------
if [ -z "$plugin_dir" ]; then
    default_plugin_dir=$(conf_get PLUGIN_DIR)
    ask plugin_dir "VST3 plugin install directory" "${default_plugin_dir:-~/.local/plugins/VST3}"
fi
if [ -z "$bin_dir" ]; then
    default_bin_dir=$(conf_get BIN_DIR)
    ask bin_dir "mlacker binary install directory" "${default_bin_dir:-~/.local/bin}"
fi

# --- Write the config ----------------------------------------------------------------
cat > "$CONFIG_FILE" <<EOF
# mlacker build configuration, written by ./bootstrap.sh and read by ./build.sh.
# Re-run ./bootstrap.sh to change it (current values become the defaults).

# subproject: $SUBPROJECT_DIR is a clone of MLang's development repository
# external:   MLANG_DIR is your own MLang checkout
MLANG_MODE="$mode"
MLANG_DIR="$mlang_dir"
MLANG_REPO="$mlang_repo"
MLANG_BRANCH="$mlang_branch"

PLUGIN_DIR="$plugin_dir"
BIN_DIR="$bin_dir"
EOF
echo
echo "Wrote $CONFIG_FILE:"
grep -v '^#' "$CONFIG_FILE" | grep -v '^$' | sed 's/^/  /'
echo

# --- Build ---------------------------------------------------------------------------
if [ -z "$build_answer" ]; then
    if confirm "Build mlacker and the plugins now?" y; then build_answer="yes"; else build_answer="no"; fi
fi
if [ "$build_answer" != "yes" ]; then
    echo "Run ./build.sh when you are ready (./build.sh --help for options)."
    exit 0
fi
if [ -z "$install_answer" ]; then
    if confirm "Install mlacker to $bin_dir and the plugins to $plugin_dir after building?" n; then
        install_answer="yes"
    else
        install_answer="no"
    fi
fi
if [ "$install_answer" = "yes" ]; then
    echo "./build.sh --install --all"
    exec ./build.sh --install --all
fi
echo "./build.sh --all"
exec ./build.sh --all
