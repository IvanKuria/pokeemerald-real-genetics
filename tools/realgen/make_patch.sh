#!/usr/bin/env bash
# Builds the vanilla ROM and the Real Genetics ROM, then writes a .bps patch between them.
#
#   tools/realgen/make_patch.sh [VERSION]
#
# VERSION defaults to the exact git tag on HEAD, or dev-<short hash> when there isn't one.
#
# Steps:
#   1. Build the unmodified upstream pokeemerald (the commit our changes start from) in a temporary
#      git worktree, and check that it is byte-identical to the retail ROM (SHA1 below).
#   2. Build the current tree (the mod).
#   3. Write dist/pokeemerald-real-genetics-<VERSION>.bps plus a checksum note next to it.
#   4. Apply the patch to the vanilla build and check it reproduces the mod exactly (with our own
#      decoder, and with Flips too if it is installed).
#
# Environment:
#   REALGEN_MAKE     build command (default: ~/Tools/gba/bin/locked-make). CI sets "make -j<n>".
#   REALGEN_BASE     override the upstream base commit.
#   REALGEN_REBUILD  set to 1 to rebuild the vanilla ROM even if a cached build exists.
#   FLIPS            path to a Flips binary for the extra cross-check (optional).
#
# No ROM is ever written to dist/. The vanilla build is cached in build/realgen/, which git ignores.

set -euo pipefail

BASE_SHA1="f3ae088181bf583e55daf962a92bb46f4f1d07b7"   # Pokemon - Emerald Version (USA, Europe)
NAME="pokeemerald-real-genetics"

die() { echo "make_patch: error: $*" >&2; exit 1; }
log() { echo "make_patch: $*"; }

sha1_of() {
    if command -v sha1sum >/dev/null 2>&1; then sha1sum "$1" | cut -d' ' -f1
    else shasum -a 1 "$1" | cut -d' ' -f1; fi
}
size_of() { wc -c < "$1" | tr -d ' '; }

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
TOOLS="$ROOT/tools/realgen"
PYTHON="${PYTHON:-python3}"

# ---------------------------------------------------------------- build command
if [ -n "${REALGEN_MAKE:-}" ]; then
    read -r -a MAKE_CMD <<< "$REALGEN_MAKE"
elif [ -x "$HOME/Tools/gba/bin/locked-make" ]; then
    MAKE_CMD=("$HOME/Tools/gba/bin/locked-make")
else
    die "no build command. Set REALGEN_MAKE (for example REALGEN_MAKE=\"make -j4\")."
fi

# ---------------------------------------------------------------- version
VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    VERSION="$(git describe --tags --exact-match HEAD 2>/dev/null || echo "dev-$(git rev-parse --short HEAD)")"
fi
if ! git diff --quiet HEAD -- 2>/dev/null; then
    log "warning: the tree has uncommitted changes, the patch is built from the working tree"
    case "$VERSION" in *-dirty) ;; *) VERSION="$VERSION-dirty" ;; esac
fi
case "$VERSION" in *[!A-Za-z0-9._-]*) die "version '$VERSION' has characters that don't belong in a file name" ;; esac

# ---------------------------------------------------------------- upstream base commit
BASE="${REALGEN_BASE:-}"
if [ -z "$BASE" ] && git rev-parse --verify -q upstream/master >/dev/null; then
    BASE="$(git merge-base HEAD upstream/master)"
fi
if [ -z "$BASE" ]; then
    # no upstream remote (fresh clone, CI): the parent of the commit that added REALGEN_SPEC.md
    FIRST="$(git log --diff-filter=A --format=%H -- REALGEN_SPEC.md | tail -n 1)"
    [ -n "$FIRST" ] || die "can't find the upstream base commit; set REALGEN_BASE"
    BASE="$(git rev-parse "$FIRST^")"
fi
BASE="$(git rev-parse --verify "$BASE^{commit}")"
log "version     $VERSION"
log "base commit $BASE"

[ -d "$ROOT/tools/agbcc/bin" ] || die "tools/agbcc is missing; install agbcc into this tree first (see README.md)"

# ---------------------------------------------------------------- 1. vanilla ROM
CACHE_DIR="$ROOT/build/realgen"
VANILLA="$CACHE_DIR/vanilla-$BASE.gba"
mkdir -p "$CACHE_DIR"

if [ -f "$VANILLA" ] && [ "${REALGEN_REBUILD:-0}" != "1" ] && [ "$(sha1_of "$VANILLA")" = "$BASE_SHA1" ]; then
    log "using cached vanilla build $VANILLA"
else
    TMP="$(mktemp -d "${TMPDIR:-/tmp}/realgen-vanilla.XXXXXX")"
    cleanup() {
        git -C "$ROOT" worktree remove --force "$TMP/src" >/dev/null 2>&1 || true
        rm -rf "$TMP"
        git -C "$ROOT" worktree prune >/dev/null 2>&1 || true
    }
    trap cleanup EXIT
    log "building vanilla pokeemerald at ${BASE:0:12} in $TMP/src"
    git worktree add --quiet --detach "$TMP/src" "$BASE"
    mkdir -p "$TMP/src/tools"
    cp -R "$ROOT/tools/agbcc" "$TMP/src/tools/agbcc"
    "${MAKE_CMD[@]}" -C "$TMP/src" > "$TMP/vanilla-build.log" 2>&1 \
        || { tail -n 40 "$TMP/vanilla-build.log" >&2; die "vanilla build failed"; }
    [ -f "$TMP/src/pokeemerald.gba" ] || die "vanilla build produced no pokeemerald.gba"
    cp "$TMP/src/pokeemerald.gba" "$VANILLA.tmp"
    mv "$VANILLA.tmp" "$VANILLA"
    cleanup
    trap - EXIT
fi

VANILLA_SHA1="$(sha1_of "$VANILLA")"
[ "$VANILLA_SHA1" = "$BASE_SHA1" ] \
    || die "vanilla build SHA1 is $VANILLA_SHA1, expected $BASE_SHA1 (retail Emerald). Not making a patch."
log "vanilla ROM $(size_of "$VANILLA") bytes, SHA1 $VANILLA_SHA1 (matches retail Emerald)"

# ---------------------------------------------------------------- 2. modded ROM
log "building Real Genetics from $ROOT"
"${MAKE_CMD[@]}" -C "$ROOT" > "$CACHE_DIR/mod-build.log" 2>&1 \
    || { tail -n 40 "$CACHE_DIR/mod-build.log" >&2; die "mod build failed (log: $CACHE_DIR/mod-build.log)"; }
MOD="$ROOT/pokeemerald.gba"
[ -f "$MOD" ] || die "mod build produced no pokeemerald.gba"
MOD_SHA1="$(sha1_of "$MOD")"
log "modded ROM  $(size_of "$MOD") bytes, SHA1 $MOD_SHA1"
[ "$MOD_SHA1" != "$VANILLA_SHA1" ] || log "warning: the modded ROM is identical to vanilla"

# ---------------------------------------------------------------- 3. patch
mkdir -p "$ROOT/dist"
PATCH="$ROOT/dist/$NAME-$VERSION.bps"
NOTE="$ROOT/dist/$NAME-$VERSION.txt"
COMMIT="$(git rev-parse HEAD)"
METADATA="<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<patch name=\"Pokemon Emerald: Real Genetics\" version=\"$VERSION\" commit=\"$COMMIT\">
  <source name=\"Pokemon - Emerald Version (USA, Europe)\" sha1=\"$BASE_SHA1\"/>
  <target sha1=\"$MOD_SHA1\"/>
</patch>"

"$PYTHON" "$TOOLS/bps.py" diff "$VANILLA" "$MOD" "$PATCH.tmp" --metadata "$METADATA" >/dev/null
mv "$PATCH.tmp" "$PATCH"

# ---------------------------------------------------------------- 4. verify
CHECK="$CACHE_DIR/check.gba"
"$PYTHON" "$TOOLS/bps.py" apply "$VANILLA" "$PATCH" "$CHECK" >/dev/null
[ "$(sha1_of "$CHECK")" = "$MOD_SHA1" ] || die "applying the patch did not reproduce the modded ROM"
cmp -s "$CHECK" "$MOD" || die "applying the patch did not reproduce the modded ROM byte for byte"
log "verified    vanilla + patch == modded ROM (bps.py)"

FLIPS_BIN="${FLIPS:-$(command -v flips || true)}"
if [ -z "$FLIPS_BIN" ] && [ -x "$HOME/Tools/gba/bin/flips" ]; then FLIPS_BIN="$HOME/Tools/gba/bin/flips"; fi
if [ -n "$FLIPS_BIN" ] && [ -x "$FLIPS_BIN" ]; then
    rm -f "$CHECK"
    "$FLIPS_BIN" --apply "$PATCH" "$VANILLA" "$CHECK" >/dev/null
    cmp -s "$CHECK" "$MOD" || die "Flips produced a different ROM from the patch"
    log "verified    vanilla + patch == modded ROM (Flips)"
else
    log "Flips not found, skipped the second check"
fi
rm -f "$CHECK"

cat > "$NOTE" <<EOF
Pokemon Emerald: Real Genetics $VERSION
Patch file:        $NAME-$VERSION.bps ($(size_of "$PATCH") bytes)
Patch SHA1:        $(sha1_of "$PATCH")
Base ROM SHA1:     $BASE_SHA1  (Pokemon - Emerald Version (USA, Europe))
Patched ROM SHA1:  $MOD_SHA1
Built from commit: $COMMIT
EOF

log "wrote       $PATCH ($(size_of "$PATCH") bytes)"
log "wrote       $NOTE"
