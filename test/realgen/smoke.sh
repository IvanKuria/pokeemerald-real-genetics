#!/bin/sh
# Emulator smoke test: boots the built ROM headlessly in mGBA, runs N frames, writes a screenshot and fails if the
# last frame is blank (fewer than 8 colours).
#
#   test/realgen/smoke.sh [ROM] [FRAMES] [OUT.png] [extra realgen-mgba args...]
#   defaults: ./pokeemerald.gba, 2400 frames, test/realgen/build/smoke.png, and (only when no extra args are given)
#   --press 1500:S:4, which skips the intro cinematic so the shot is the "PRESS START" title screen.
#   e.g. just the intro cinematic:  test/realgen/smoke.sh pokeemerald.gba 1500 out.png --min-colors 8
#   Lua checks:  ... --script my_check.lua   (mGBA 0.10 scripting API; the runner fires "start" and "frame" callbacks)
#
# Headless and windowless: runs the mGBA core in-process (no GUI, GL or audio device). First run installs mGBA with
# test/realgen/install-mgba.sh. Build the ROM with ~/Tools/gba/bin/locked-make first; this script never builds it.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MGBA=${MGBA_DIR:-$HOME/Tools/gba/mgba}
ROM=${1:-$ROOT/pokeemerald.gba}
FRAMES=${2:-2400}
OUT=${3:-$HERE/build/smoke.png}
if [ $# -gt 3 ]; then shift 3; else set -- --press 1500:S:4; fi

[ -f "$ROM" ] || { echo "no ROM at $ROM (build it with ~/Tools/gba/bin/locked-make)"; exit 1; }
[ -f "$MGBA/build/libmgba.a" ] || "$HERE/install-mgba.sh"
if [ ! -x "$MGBA/bin/realgen-mgba" ] || [ "$HERE/mgba/realgen-mgba.c" -nt "$MGBA/bin/realgen-mgba" ]; then
    "$HERE/mgba/build-runner.sh"
fi
mkdir -p "$(dirname "$OUT")"
"$MGBA/bin/realgen-mgba" "$ROM" --frames "$FRAMES" --screenshot "$OUT" --min-colors 8 "$@"
