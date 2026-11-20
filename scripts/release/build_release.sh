#!/usr/bin/env bash
# Build the release packages for GitHub:
#   dist/release/Lagoon-EyeComfort-x86_64.AppImage   (Linux)
#   dist/release/Lagoon-EyeComfort-win-x64.zip       (Windows)
#   dist/release/SHA256SUMS.txt
#
# Everything is built inside an Ubuntu 24.04 container (podman or docker), so
# nothing is installed on your own system and the Linux game runs on most
# distros. Your ROM is read in place and never copied into a package.
#
# Usage: bash scripts/release/build_release.sh [all|linux|windows|appimage] [path/to/rom.sfc]
#   appimage = only re-wrap the already-built Linux game into the AppImage (fast)
#        JOBS=8 bash scripts/release/build_release.sh     (parallel compile jobs, default 6)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WHAT="${1:-all}"
ROM="${2:-$ROOT/lagoon.sfc}"
ROM="$(readlink -f "$ROM")"
[ -f "$ROM" ] || { echo "ROM not found: $ROM  (pass its path as the 2nd argument)" >&2; exit 1; }

if command -v podman >/dev/null; then ENGINE=podman; USERARGS=()
elif command -v docker >/dev/null; then ENGINE=docker; USERARGS=(--user "$(id -u):$(id -g)")
else echo "Install podman first:  sudo pacman -S podman" >&2; exit 1; fi

IMAGE=lagoon-release-builder
echo "== Preparing the build container (slow only the first time)"
"$ENGINE" build -t "$IMAGE" -f "$ROOT/scripts/release/Containerfile" "$ROOT/scripts/release"

MOUNTS=(-v "$ROOT:$ROOT")
case "$ROM" in "$ROOT"/*) ;; *) MOUNTS+=(-v "$ROM:$ROM:ro") ;; esac

"$ENGINE" run --rm "${USERARGS[@]}" -e HOME=/tmp -e JOBS="${JOBS:-6}" "${MOUNTS[@]}" -w "$ROOT" \
  "$IMAGE" bash "$ROOT/scripts/release/in_container.sh" "$ROM" "$WHAT"
