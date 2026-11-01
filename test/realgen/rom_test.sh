#!/bin/sh
# In-ROM test: runs the REALGEN_TEST=1 build headlessly in mGBA. The game itself (src/realgen_test.c) plays through
# the scenarios (coat colours in battle and summary, Day Care inheritance and hatching, lethal egg, species barrier,
# Ditto clones, shiny breeding), prints its checks and asks for screenshots.
#
#   ~/Tools/gba/bin/locked-make REALGEN_TEST=1          # builds pokeemerald_realgen_test.gba (never the shipped ROM)
#   test/realgen/rom_test.sh [SHOT_DIR] [MAX_FRAMES]     # default SHOT_DIR = ./shots, MAX_FRAMES = 60000
#
# Exit 0 when every in-game check passed.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MGBA=${MGBA_DIR:-$HOME/Tools/gba/mgba}
ROM=$ROOT/pokeemerald_realgen_test.gba
MAP=$ROOT/pokeemerald_realgen_test.map
SHOTS=${1:-$ROOT/shots}
FRAMES=${2:-60000}

[ -f "$ROM" ] || { echo "no $ROM: build it with ~/Tools/gba/bin/locked-make REALGEN_TEST=1"; exit 1; }
ADDR=$(awk '$2 == "gRealgenTest" && $1 ~ /^0x/ { print $1; exit }' "$MAP")
[ -n "$ADDR" ] || { echo "gRealgenTest not found in $MAP"; exit 1; }
[ -f "$MGBA/build/libmgba.a" ] || "$HERE/install-mgba.sh"
if [ ! -x "$MGBA/bin/realgen-mgba" ] || [ "$HERE/mgba/realgen-mgba.c" -nt "$MGBA/bin/realgen-mgba" ]; then
    "$HERE/mgba/build-runner.sh"
fi
mkdir -p "$SHOTS"
SCRIPT=$(mktemp -t realgen-rom-test).lua
trap 'rm -f "$SCRIPT"' EXIT
{
    echo "MAILBOX = $ADDR"
    echo "SHOTS = \"$SHOTS\""
    cat "$HERE/rom_test.lua"
} > "$SCRIPT"
# the Lua side exits the runner with 0 (all checks passed) or 1 when the game reports the result; a runner that
# simply runs out of frames prints its "ran N frames" line instead
LOG=$(mktemp -t realgen-rom-test-log)
trap 'rm -f "$SCRIPT" "$LOG"' EXIT
set +e
"$MGBA/bin/realgen-mgba" "$ROM" --frames "$FRAMES" --script "$SCRIPT" | tee "$LOG"
set -e
if grep -q "^rom test finished.*: PASS" "$LOG"; then exit 0; fi
grep -q "^rom test finished" "$LOG" || echo "rom test did not finish within $FRAMES frames"
exit 1
