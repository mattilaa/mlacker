#!/usr/bin/env bash
# Build the macOS installer package (.pkg) for mlacker, the MLang toolchain in
# subprojects/mlang and the Mla VST3 plugins, from a finished build:
#
#   MLACKER_STATIC_OPENSSL=1 ./build.sh --all     # MLANG_TOOLCHAIN="yes"
#   packaging/macos/build_pkg.sh --version 0.3.0
#
# The installer installs for the current user. Parts can be left out in its
# Customize step; the chosen ones unpack into a staging folder, and the last
# step asks where each goes (defaults: the build scripts' ~/.local,
# ~/.local/bin, ~/.local/plugins/VST3), moves it there and offers to add the
# tool directories to the PATH (scripts/finish/postinstall).
#
# MLang's tools load Homebrew libraries (z3, zstd, OpenSSL); they are bundled
# into lib/mlang of the MLang install and the tools load them from there.
# OpenSSL's static archives go next to libmlang_std.a, so programs compiled
# with the installed mlang link OpenSSL statically. mlacker itself must be
# built with MLACKER_STATIC_OPENSSL=1.
#
# Options:
#   --version V     Package version (default 0.0.0)
#   --out DIR       Output directory (default build/release)
# Environment:
#   MLACKER_BIN                      mlacker binary (default build/cmake/bin/mlacker)
#   MLACKER_CODESIGN_IDENTITY        "Developer ID Application: ..." to sign the
#                                    binaries and plugins with the hardened
#                                    runtime, as notarization needs (ad hoc
#                                    when empty)
#   MLACKER_INSTALLER_SIGN_IDENTITY  "Developer ID Installer: ..." to sign the
#                                    package (unsigned when empty)
set -euo pipefail

cd "$(dirname "$0")/../.."
root=$(pwd)
here="$root/packaging/macos"

version="0.0.0"
out="build/release"
while [ $# -gt 0 ]; do
    case "$1" in
        --version) version="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "build_pkg.sh: unknown option $1" >&2; exit 2 ;;
    esac
done

mlang_root="$root/subprojects/mlang"
mlang_build="$mlang_root/build"
mlacker_bin="${MLACKER_BIN:-$root/build/cmake/bin/mlacker}"
plugin_dir="$root/build/plugins/VST3/Release"
work="$root/build/pkg-work"
stage="$work/stage"

die() { echo "build_pkg.sh: $*" >&2; exit 1; }
[ -x "$mlang_build/mlang" ] || die "no MLang build in $mlang_build (run ./build.sh --all)"
for tool in mlangd-mla mlang-format mlang-frontend-mla mlang-frontend mlangpkg; do
    [ -e "$mlang_build/$tool" ] || die "missing $mlang_build/$tool (build the full toolchain: MLANG_TOOLCHAIN=\"yes\")"
done
[ -x "$mlacker_bin" ] || die "no mlacker build at $mlacker_bin"
ls "$plugin_dir"/*.vst3 >/dev/null 2>&1 || die "no VST3 plugins in $plugin_dir"
openssl_lib="$(brew --prefix openssl@3)/lib"
[ -f "$openssl_lib/libssl.a" ] && [ -f "$openssl_lib/libcrypto.a" ] || die "no OpenSSL static archives in $openssl_lib"

rm -rf "$work"
mkdir -p "$stage/mlang" "$stage/mlacker" "$stage/plugins" "$work/pkgs" "$work/resources" "$out"

is_macho() { file -b "$1" | grep -q 'Mach-O'; }
# Signs a binary or bundle: Developer ID with the hardened runtime when an
# identity is given, ad hoc otherwise (arm64 runs nothing unsigned).
sign_code() {
    if [ -n "${MLACKER_CODESIGN_IDENTITY:-}" ]; then
        codesign --force --options runtime --timestamp --sign "$MLACKER_CODESIGN_IDENTITY" "$1"
    else
        codesign --force --sign - "$1" 2>/dev/null
    fi
}
# A library outside the system: Homebrew's, which other Macs do not have.
is_foreign() { case "$1" in /opt/homebrew/*|/usr/local/*) return 0 ;; esac; return 1; }
foreign_deps() {
    otool -L "$1" | tail -n +2 | awk '{ print $1 }' | while read -r dep; do
        if is_foreign "$dep"; then echo "$dep"; fi
    done
}

# Copy the foreign libraries the binaries in $1/bin load into $1/lib/mlang
# and make the binaries (and the libraries themselves) load the copies.
bundle_dylibs() {
    local prefix="$1" lib="$1/lib/mlang" queue=() file dep name ref
    for file in "$prefix"/bin/*; do is_macho "$file" && queue+=("$file"); done
    while [ ${#queue[@]} -gt 0 ]; do
        file=${queue[0]}
        queue=("${queue[@]:1}")
        for dep in $(foreign_deps "$file"); do
            name=$(basename "$dep")
            if [ ! -f "$lib/$name" ]; then
                cp -L "$dep" "$lib/$name"
                chmod u+w "$lib/$name"
                install_name_tool -id "@rpath/$name" "$lib/$name" 2>/dev/null
                queue+=("$lib/$name")
            fi
            case "$file" in
                "$lib"/*) ref="@loader_path/$name" ;;
                *) ref="@executable_path/../lib/mlang/$name" ;;
            esac
            install_name_tool -change "$dep" "$ref" "$file" 2>/dev/null
        done
        # Changed load commands invalidate the signature; arm64 needs one.
        sign_code "$file"
    done
}

echo "[pkg] Staging MLang ($(git -C "$mlang_root" log -1 --format='%h %s'))"
cmake --install "$mlang_build" --prefix "$stage/mlang" >/dev/null
for tool in mlangd-mla mlang-format mlang-frontend-mla mlang-frontend mlangpkg; do
    cp -f "$mlang_build/$tool" "$stage/mlang/bin/$tool"
    chmod 755 "$stage/mlang/bin/$tool"
done
cp -f "$openssl_lib/libssl.a" "$openssl_lib/libcrypto.a" "$stage/mlang/lib/mlang/"
bundle_dylibs "$stage/mlang"

echo "[pkg] Staging mlacker and the plugins"
cp -f "$mlacker_bin" "$stage/mlacker/mlacker"
sign_code "$stage/mlacker/mlacker"
for bundle in "$plugin_dir"/*.vst3; do
    cp -R "$bundle" "$stage/plugins/"
    sign_code "$stage/plugins/$(basename "$bundle")"
done

# Nothing may still load a Homebrew library.
leftover=""
while IFS= read -r -d '' file; do
    is_macho "$file" || continue
    for dep in $(foreign_deps "$file"); do leftover="$leftover\n  ${file#"$stage"/} -> $dep"; done
done < <(find "$stage" -type f -print0)
[ -z "$leftover" ] || die "binaries still load Homebrew libraries (mlacker needs MLACKER_STATIC_OPENSSL=1):$(printf "$leftover")"

# The oldest macOS every binary runs on.
min_macos=$(find "$stage" -type f -print0 | while IFS= read -r -d '' file; do
    if is_macho "$file"; then vtool -show-build "$file" 2>/dev/null | awk '$1 == "minos" { print $2 }'; fi
done | sort -V | tail -1)
min_macos=${min_macos:-11.0}
mlang_commit=$(git -C "$mlang_root" rev-parse --short HEAD)
echo "[pkg] Version $version, MLang $mlang_commit, macOS $min_macos or later"

# The parts unpack into a staging folder in the home folder (install
# locations are relative to it); scripts/finish/postinstall moves them.
staging="/Library/Caches/com.github.mattilaa.mlacker.installer"

# Plugin bundles stay where the user chose; Installer would otherwise update
# a copy of the same bundle found elsewhere on disk instead.
pkgbuild --analyze --root "$stage/plugins" "$work/plugins.plist" >/dev/null
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$work/plugins.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$work/plugins.plist" >/dev/null 2>&1 || true
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$work/plugins.plist"
    i=$((i + 1))
done

# Extended attributes (quarantine, provenance) would end up in the payload as
# AppleDouble ._ files: package a copy without them.
chmod -R u+w "$stage"
ditto --norsrc --noextattr --noqtn --noacl "$stage" "$work/payload"
stage="$work/payload"
pkgbuild --quiet --nopayload --identifier com.github.mattilaa.mlacker.prepare --version "$version" \
    --scripts "$here/scripts/prepare" "$work/pkgs/prepare.pkg"
pkgbuild --quiet --root "$stage/mlang" --identifier com.github.mattilaa.mlacker.mlang --version "$version" \
    --install-location "$staging/mlang" "$work/pkgs/mlang.pkg"
pkgbuild --quiet --root "$stage/mlacker" --identifier com.github.mattilaa.mlacker.app --version "$version" \
    --install-location "$staging/mlacker" "$work/pkgs/mlacker.pkg"
pkgbuild --quiet --root "$stage/plugins" --component-plist "$work/plugins.plist" \
    --identifier com.github.mattilaa.mlacker.plugins --version "$version" \
    --install-location "$staging/plugins" "$work/pkgs/plugins.pkg"
pkgbuild --quiet --nopayload --identifier com.github.mattilaa.mlacker.finish --version "$version" \
    --scripts "$here/scripts/finish" "$work/pkgs/finish.pkg"

fill() {
    sed -e "s|@VERSION@|$version|g" -e "s|@MLANG_COMMIT@|$mlang_commit|g" -e "s|@MIN_MACOS@|$min_macos|g" "$1"
}
fill "$here/distribution.xml.in" > "$work/distribution.xml"
fill "$here/resources/welcome.html" > "$work/resources/welcome.html"
fill "$here/resources/conclusion.html" > "$work/resources/conclusion.html"
cp "$root/LICENSE" "$work/resources/LICENSE.txt"

package="$out/mlacker-$version-macos-arm64.pkg"
sign=()
[ -n "${MLACKER_INSTALLER_SIGN_IDENTITY:-}" ] && sign=(--sign "$MLACKER_INSTALLER_SIGN_IDENTITY")
productbuild --quiet --distribution "$work/distribution.xml" --resources "$work/resources" \
    --package-path "$work/pkgs" ${sign[@]+"${sign[@]}"} "$package"
echo "[pkg] Built $package ($(du -h "$package" | cut -f1))"
