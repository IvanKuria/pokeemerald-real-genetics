#!/bin/sh
# Compiles realgen-mgba.c against the libmgba built by test/realgen/install-mgba.sh.
set -eu
MGBA=${MGBA_DIR:-$HOME/Tools/gba/mgba}
PNG_DIR=${PNG_DIR:-$HOME/Tools/gba/libpng}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(/bin/ls -d "$MGBA"/src/mgba-* | head -1)
mkdir -p "$MGBA/bin"
cc -O2 -std=c11 -Wall -DBUILD_STATIC -DENABLE_SCRIPTING -DUSE_LUA -DUSE_PNG -DUSE_ZLIB -DM_CORE_GBA -DM_CORE_GB \
    -DUSE_PTHREADS -DUSE_DEBUGGERS -D_DARWIN_C_SOURCE \
    -I"$MGBA/build/include" -I"$SRC/include" -I"$SRC/src" -I"$PNG_DIR/include" \
    -o "$MGBA/bin/realgen-mgba" "$HERE/realgen-mgba.c" \
    "$MGBA/build/libmgba.a" "$PNG_DIR/lib/libpng.a" "$MGBA/deps/lib/liblua.a" -lz -lm -framework Foundation
