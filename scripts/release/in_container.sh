#!/usr/bin/env bash
# Runs INSIDE the Ubuntu 24.04 build container (started by build_release.sh).
# Builds the strict-native game for Linux and Windows from your own ROM and
# packages them. The ROM, generated C and your settings/saves never go into
# a package; the check at the end refuses if they would.
#
# Usage: in_container.sh <rom> [all|linux|windows|appimage]
#   appimage = only re-wrap the already-built Linux game (no recompiling)
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
ROM="$1"
WHAT="${2:-all}"
JOBS="${JOBS:-6}"
WORK="$ROOT/build-release"
DIST="$ROOT/dist/release"
FAILED=0
git config --global --add safe.directory '*'
mkdir -p "$WORK" "$DIST"

say() { printf '\n==== %s ====\n' "$*"; }

copy_licenses() {  # <build dir> <destination>
  mkdir -p "$2"
  cp LICENSE "$2/LICENSE-Lagoon-Recomp.txt"
  cp CREDITS.md "$2/"
  [ -d licenses ] && cp -r licenses/. "$2/"
  local sdl
  sdl="$(find "$1/_deps" -maxdepth 3 -iname 'LICENSE.txt' -ipath '*sdl3*' 2>/dev/null | head -n1)"
  [ -n "$sdl" ] && cp "$sdl" "$2/SDL3-LICENSE.txt"
  return 0
}

check_clean() {  # refuse a package that would carry private files
  local bad
  bad="$(find "$1" \( -iname '*.sfc' -o -iname '*.smc' -o -iname '*.srm' -o -name rom.cfg \
         -o -name config.ini -o -name keybinds.ini -o -name saves -o -name '*.log' \
         -o -name 'src' -o -name '*.generated.c' \) -print)"
  if [ -n "$bad" ]; then
    echo "REFUSING: private files would be packaged:" >&2; echo "$bad" >&2; return 1
  fi
}

# ---------------------------------------------------------------- Linux
build_linux() {
  say "1/4 Building the Linux game (Ubuntu 24.04 libraries)"
  rm -rf "$WORK/linux"
  python3 -P scripts/build_port.py --rom "$ROM" --build-dir "$WORK/linux" --jobs "$JOBS" \
    "--cmake-arg=-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" || return 1
  package_appimage
}

package_appimage() {
  say "2/4 Making the AppImage"
  [ -x "$WORK/linux/LagoonSNESRecomp" ] || { echo "No Linux game built yet: run 'linux' first." >&2; return 1; }
  local B="$WORK/linux" A="$WORK/AppDir"
  rm -rf "$A"
  mkdir -p "$A/usr/bin" "$A/usr/share/lagoon-eyecomfort" "$A/usr/share/applications"
  install -m755 "$B/LagoonSNESRecomp" "$A/usr/bin/"
  strip --strip-debug "$A/usr/bin/LagoonSNESRecomp" || true
  cp -r "$B/assets" "$A/usr/bin/assets"
  cp "$B/lagoon-build.json" "$A/usr/bin/"
  cp -r "$B/mods" "$A/usr/share/lagoon-eyecomfort/mods"
  copy_licenses "$B" "$A/usr/share/lagoon-eyecomfort/licenses"
  install -m755 scripts/release/AppRun "$A/AppRun"
  cp scripts/release/lagoon-eyecomfort.desktop "$A/"
  cp scripts/release/lagoon-eyecomfort.desktop "$A/usr/share/applications/"
  cp assets/icons/lagoon-game.png "$A/lagoon-eyecomfort.png"
  check_clean "$A" || return 1
  cp scripts/release/default-config.ini "$A/usr/share/lagoon-eyecomfort/default-config.ini"
  # Every player must be able to read the files (source files may be private 0600).
  chmod -R a+rX "$A"; chmod a+rx "$A/AppRun" "$A/usr/bin/LagoonSNESRecomp"
  rm -f "$DIST/Lagoon-EyeComfort-x86_64.AppImage"
  echo "(appimagetool messages are also saved in build-release/appimagetool.log)"
  if ! ARCH=x86_64 appimagetool -n "$A" "$DIST/Lagoon-EyeComfort-x86_64.AppImage" > "$WORK/appimagetool.log" 2>&1; then
    cat "$WORK/appimagetool.log"; return 1
  fi
  tail -n 3 "$WORK/appimagetool.log"
}

# -------------------------------------------------------------- Windows
build_windows() {
  say "3/4 Building the Windows game (MinGW cross-compiler)"
  rm -rf "$WORK/windows"
  local reuse=()
  [ -f src/gen/program_manifest.json ] && reuse=(--reuse-generated)
  python3 -P scripts/build_port.py "${reuse[@]}" --rom "$ROM" --build-dir "$WORK/windows" --jobs "$JOBS" \
    "--cmake-arg=-DCMAKE_TOOLCHAIN_FILE=$ROOT/scripts/release/mingw-w64-x86_64.cmake" \
    "--cmake-arg=-DLAGOON_PORTABLE_STATIC_SDL3=OFF" || return 1

  say "4/4 Making the Windows zip"
  local W="$WORK/windows" P="$WORK/win-pkg/Lagoon-EyeComfort"
  rm -rf "$WORK/win-pkg"
  mkdir -p "$P"
  cp "$W/LagoonSNESRecomp.exe" "$P/"
  x86_64-w64-mingw32-strip --strip-debug "$P/LagoonSNESRecomp.exe" || true
  cp -r "$W/assets" "$W/mods" "$P/"
  cp "$W/lagoon-build.json" "$P/"
  copy_licenses "$W" "$P/licenses"
  [ -f /usr/share/doc/mingw-w64-common/copyright ] && cp /usr/share/doc/mingw-w64-common/copyright "$P/licenses/MinGW-w64-runtime.txt"
  cp "scripts/release/READ ME FIRST - Windows.txt" "$P/"
  check_clean "$P" || return 1
  # Starting settings; the game keeps config.ini beside the .exe and only adds to it.
  cp scripts/release/default-config.ini "$P/config.ini"
  rm -f "$DIST/Lagoon-EyeComfort-win-x64.zip"
  (cd "$WORK/win-pkg" && zip -qr -9 "$DIST/Lagoon-EyeComfort-win-x64.zip" Lagoon-EyeComfort) || return 1
}

case "$WHAT" in
  appimage) package_appimage || { FAILED=1; echo "!!! AppImage packaging failed: copy the error above to Claude." >&2; } ;;
esac
case "$WHAT" in
  all|linux) build_linux || { FAILED=1; echo "!!! Linux build failed: copy the error above to Claude." >&2; } ;;
esac
case "$WHAT" in
  all|windows) build_windows || { FAILED=1; echo "!!! Windows build failed: copy the error above to Claude." >&2; } ;;
esac

(cd "$DIST" && rm -f SHA256SUMS.txt && ls Lagoon-EyeComfort-* >/dev/null 2>&1 && sha256sum Lagoon-EyeComfort-* > SHA256SUMS.txt)
say "Done"
ls -lh "$DIST"
exit "$FAILED"
