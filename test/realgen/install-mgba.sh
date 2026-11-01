#!/bin/sh
# Installs mGBA 0.10.5 into ~/Tools/gba/mgba without admin rights:
#   mGBA.app        official macOS release (GUI, for watching a run by hand)
#   build/libmgba.a headless library built from the release source, with Lua 5.4 scripting and PNG output
#   bin/realgen-mgba  this repo's headless runner (test/realgen/mgba/realgen-mgba.c), linked against it
# Needs: Xcode command line tools, python3, curl. Builds with -j3 (8 GB machine).
set -eu
MGBA=${MGBA_DIR:-$HOME/Tools/gba/mgba}
VER=0.10.5
LUA=5.4.7
PNG_DIR=${PNG_DIR:-$HOME/Tools/gba/libpng}   # libpng with include/ and lib/libpng.a (already used by the pokeemerald tools)
HERE=$(cd "$(dirname "$0")" && pwd)

mkdir -p "$MGBA/dl" "$MGBA/src" "$MGBA/bin"
cd "$MGBA/dl"
[ -f mgba-$VER.tar.gz ] || curl -fsSL -o mgba-$VER.tar.gz https://github.com/mgba-emu/mgba/archive/refs/tags/$VER.tar.gz
[ -f lua-$LUA.tar.gz ] || curl -fsSL -o lua-$LUA.tar.gz https://www.lua.org/ftp/lua-$LUA.tar.gz
[ -f mGBA-$VER-macos.dmg ] || curl -fsSL -o mGBA-$VER-macos.dmg https://github.com/mgba-emu/mgba/releases/download/$VER/mGBA-$VER-macos.dmg

if [ ! -d "$MGBA/mGBA.app" ]; then
    hdiutil attach -nobrowse -readonly -mountpoint /tmp/mgba-dmg.$$ mGBA-$VER-macos.dmg >/dev/null
    cp -R /tmp/mgba-dmg.$$/mGBA.app "$MGBA/"
    hdiutil detach /tmp/mgba-dmg.$$ >/dev/null
fi

[ -d "$MGBA/src/mgba-$VER" ] || tar xzf mgba-$VER.tar.gz -C "$MGBA/src"
[ -d "$MGBA/src/lua-$LUA" ] || tar xzf lua-$LUA.tar.gz -C "$MGBA/src"

if [ ! -f "$MGBA/deps/lib/liblua.a" ]; then
    (cd "$MGBA/src/lua-$LUA" && make -s macosx -j2 >/dev/null && make -s install INSTALL_TOP="$MGBA/deps" >/dev/null)
fi

[ -x "$MGBA/venv/bin/cmake" ] || { python3 -m venv "$MGBA/venv" && "$MGBA/venv/bin/pip" -q install cmake; }

if [ ! -f "$MGBA/build/libmgba.a" ]; then
    mkdir -p "$MGBA/build"
    cd "$MGBA/build"
    "$MGBA/venv/bin/cmake" "../src/mgba-$VER" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DUSE_EPOXY=OFF \
        -DUSE_FFMPEG=OFF -DUSE_MINIZIP=OFF -DUSE_LIBZIP=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LZMA=OFF \
        -DUSE_DISCORD_RPC=OFF -DUSE_EDITLINE=OFF -DUSE_ZLIB=ON -DUSE_PNG=ON -DUSE_LUA=ON -DENABLE_SCRIPTING=ON \
        -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DBUILD_ROM_TEST=ON -DBUILD_LTO=OFF \
        -DCMAKE_PREFIX_PATH="$MGBA/deps;$PNG_DIR" \
        -DLUA_INCLUDE_DIR="$MGBA/deps/include" -DLUA_LIBRARY="$MGBA/deps/lib/liblua.a" >/dev/null
    "$MGBA/venv/bin/cmake" --build . -j3 >/dev/null
fi

"$HERE/mgba/build-runner.sh"
echo "mGBA ready: $MGBA/bin/realgen-mgba (headless), $MGBA/mGBA.app (GUI)"
