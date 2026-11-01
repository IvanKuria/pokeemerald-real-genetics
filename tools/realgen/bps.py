#!/usr/bin/env python3
"""Small BPS patch encoder and decoder (the beat patch format used by Flips and Rom Patcher JS).

Usage:
    bps.py diff SOURCE TARGET PATCH [--metadata TEXT]
    bps.py apply SOURCE PATCH OUTPUT
    bps.py info PATCH

Format reference: byuu's BPS specification ("beat" patch format, version 1).
  header   "BPS1", varint source size, varint target size, varint metadata size, metadata
  actions  varint ((length - 1) << 2 | command), then command data
             0 SourceRead  copy from the source at the current output offset
             1 TargetRead  literal bytes stored in the patch
             2 SourceCopy  copy from the source at a signed relative offset
             3 TargetCopy  copy from already written output at a signed relative offset
  footer   CRC32 of the source, CRC32 of the target, CRC32 of the patch (all little endian u32)

The encoder is greedy: at each output position it takes the longest of (a) a SourceRead at the same
offset, (b) a SourceCopy that continues one of the recent source/target offset deltas, or (c) a SourceCopy
found through a hash index of the source. That handles the usual ROM hack case well, where most of the
ROM is unchanged but everything after new code is shifted by a few bytes.
"""

import argparse
import struct
import sys
import zlib

MAGIC = b"BPS1"
SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY = range(4)
FOOTER_SIZE = 12

HASH_WINDOW = 16   # bytes compared when looking up a new match in the source index
HASH_STEP = 16     # the source index holds every HASH_STEP-th position
MIN_RUN = 4        # shortest SourceRead / delta continuation worth breaking a literal for
RECENT_DELTAS = 8  # how many recent source/target offset deltas to try before the index


class BPSError(Exception):
    pass


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


# ---------------------------------------------------------------- numbers

def encode_number(n, out):
    if n < 0:
        raise ValueError("BPS numbers are unsigned")
    while True:
        x = n & 0x7F
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return
        out.append(x)
        n -= 1


def decode_number(data, pos, end=None):
    if end is None:
        end = len(data)
    n = 0
    shift = 1
    while True:
        if pos >= end:
            raise BPSError("patch is truncated (number runs past the end)")
        x = data[pos]
        pos += 1
        n += (x & 0x7F) * shift
        if x & 0x80:
            return n, pos
        shift <<= 7
        n += shift


def encode_signed(n, out):
    encode_number((abs(n) << 1) | (1 if n < 0 else 0), out)


def decode_signed(data, pos, end=None):
    n, pos = decode_number(data, pos, end)
    return (-(n >> 1) if n & 1 else (n >> 1)), pos


# ---------------------------------------------------------------- decoder

def apply_patch(source, patch):
    """Return (target bytes, metadata bytes). Raises BPSError on any mismatch or malformed patch."""
    if len(patch) < len(MAGIC) + 3 + FOOTER_SIZE or patch[:4] != MAGIC:
        raise BPSError("not a BPS patch")
    source_crc, target_crc, patch_crc = struct.unpack("<III", patch[-FOOTER_SIZE:])
    if crc32(patch[:-4]) != patch_crc:
        raise BPSError("patch file is corrupted (patch CRC32 mismatch)")

    end = len(patch) - FOOTER_SIZE
    pos = 4
    source_size, pos = decode_number(patch, pos, end)
    target_size, pos = decode_number(patch, pos, end)
    metadata_size, pos = decode_number(patch, pos, end)
    if pos + metadata_size > end:
        raise BPSError("metadata runs past the end of the patch")
    metadata = bytes(patch[pos:pos + metadata_size])
    pos += metadata_size

    if len(source) != source_size:
        raise BPSError("wrong source file: expected %d bytes, got %d" % (source_size, len(source)))
    if crc32(source) != source_crc:
        raise BPSError("wrong source file (source CRC32 mismatch)")

    out = bytearray(target_size)
    out_pos = 0
    source_rel = 0
    target_rel = 0
    while pos < end:
        data, pos = decode_number(patch, pos, end)
        command = data & 3
        length = (data >> 2) + 1
        if out_pos + length > target_size:
            raise BPSError("patch writes past the end of the target")

        if command == SOURCE_READ:
            if out_pos + length > source_size:
                raise BPSError("SourceRead past the end of the source")
            out[out_pos:out_pos + length] = source[out_pos:out_pos + length]
        elif command == TARGET_READ:
            if pos + length > end:
                raise BPSError("TargetRead past the end of the patch")
            out[out_pos:out_pos + length] = patch[pos:pos + length]
            pos += length
        elif command == SOURCE_COPY:
            offset, pos = decode_signed(patch, pos, end)
            source_rel += offset
            if source_rel < 0 or source_rel + length > source_size:
                raise BPSError("SourceCopy outside the source")
            out[out_pos:out_pos + length] = source[source_rel:source_rel + length]
            source_rel += length
        else:
            offset, pos = decode_signed(patch, pos, end)
            target_rel += offset
            if target_rel < 0 or target_rel >= out_pos:
                raise BPSError("TargetCopy outside the written target")
            if target_rel + length <= out_pos:
                out[out_pos:out_pos + length] = out[target_rel:target_rel + length]
            else:
                # overlapping copy (run-length style), has to go byte by byte
                for i in range(length):
                    out[out_pos + i] = out[target_rel + i]
            target_rel += length
        out_pos += length

    if out_pos != target_size:
        raise BPSError("patch ended early: wrote %d of %d bytes" % (out_pos, target_size))
    result = bytes(out)
    if crc32(result) != target_crc:
        raise BPSError("result does not match (target CRC32 mismatch)")
    return result, metadata


# ---------------------------------------------------------------- encoder

def _match_length(a, ai, b, bi):
    """Number of equal bytes starting at a[ai] and b[bi]."""
    limit = min(len(a) - ai, len(b) - bi)
    n = 0
    step = 64
    while n < limit:
        c = min(step, limit - n)
        if a[ai + n:ai + n + c] == b[bi + n:bi + n + c]:
            n += c
            if step < (1 << 20):
                step <<= 1
        elif c <= 16:
            while n < limit and a[ai + n] == b[bi + n]:
                n += 1
            return n
        else:
            step = 16
    return n


def _build_index(source):
    index = {}
    last = len(source) - HASH_WINDOW
    for i in range(0, last + 1, HASH_STEP):
        window = source[i:i + HASH_WINDOW]
        if window.count(window[0]) == HASH_WINDOW:
            continue  # uniform padding, the delta and SourceRead candidates cover it better
        index.setdefault(window, i)
    return index


def create_patch(source, target, metadata=b""):
    source = bytes(source)
    target = bytes(target)
    metadata = bytes(metadata)
    out = bytearray(MAGIC)
    encode_number(len(source), out)
    encode_number(len(target), out)
    encode_number(len(metadata), out)
    out += metadata

    index = _build_index(source)
    slen = len(source)
    tlen = len(target)
    source_rel = 0          # mirrors the decoder's source relative offset
    recent = []             # recent (source position - target position) deltas, newest first
    lit_start = 0
    pos = 0

    def flush_literal(upto):
        if upto > lit_start:
            encode_number(((upto - lit_start - 1) << 2) | TARGET_READ, out)
            out.extend(target[lit_start:upto])

    while pos < tlen:
        byte = target[pos]
        best_len = 0
        best_src = -1

        # (a) same offset in the source
        if pos < slen and source[pos] == byte:
            n = _match_length(source, pos, target, pos)
            if n >= MIN_RUN:
                best_len, best_src = n, pos

        # (b) continue a recent shift
        for delta in recent:
            s = pos + delta
            if s == pos or s < 0 or s >= slen or source[s] != byte:
                continue
            n = _match_length(source, s, target, pos)
            if n >= MIN_RUN and n > best_len:
                best_len, best_src = n, s

        # (c) new match through the index
        if best_len < HASH_WINDOW and pos + HASH_WINDOW <= tlen:
            s = index.get(target[pos:pos + HASH_WINDOW])
            if s is not None:
                n = _match_length(source, s, target, pos)
                if n > best_len:
                    best_len, best_src = n, s

        if best_len == 0:
            pos += 1
            continue

        # grow the match backwards into any pending literal bytes
        start = pos
        s = best_src
        while start > lit_start and s > 0 and source[s - 1] == target[start - 1]:
            start -= 1
            s -= 1
        length = best_len + (pos - start)

        flush_literal(start)
        if s == start:
            encode_number(((length - 1) << 2) | SOURCE_READ, out)
        else:
            encode_number(((length - 1) << 2) | SOURCE_COPY, out)
            encode_signed(s - source_rel, out)
            source_rel = s + length
            delta = s - start
            if delta in recent:
                recent.remove(delta)
            recent.insert(0, delta)
            del recent[RECENT_DELTAS:]
        pos = start + length
        lit_start = pos

    flush_literal(tlen)
    out += struct.pack("<II", crc32(source), crc32(target))
    out += struct.pack("<I", crc32(out))
    return bytes(out)


def read_info(patch):
    if len(patch) < len(MAGIC) + 3 + FOOTER_SIZE or patch[:4] != MAGIC:
        raise BPSError("not a BPS patch")
    end = len(patch) - FOOTER_SIZE
    pos = 4
    source_size, pos = decode_number(patch, pos, end)
    target_size, pos = decode_number(patch, pos, end)
    metadata_size, pos = decode_number(patch, pos, end)
    metadata = bytes(patch[pos:pos + metadata_size])
    source_crc, target_crc, patch_crc = struct.unpack("<III", patch[-FOOTER_SIZE:])
    return {
        "source_size": source_size,
        "target_size": target_size,
        "metadata": metadata,
        "source_crc32": source_crc,
        "target_crc32": target_crc,
        "patch_crc32": patch_crc,
        "patch_crc_ok": crc32(patch[:-4]) == patch_crc,
    }


# ---------------------------------------------------------------- command line

def _read(path):
    with open(path, "rb") as f:
        return f.read()


def _write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description="BPS patch encoder/decoder")
    sub = parser.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("diff", help="create a patch that turns SOURCE into TARGET")
    d.add_argument("source")
    d.add_argument("target")
    d.add_argument("patch")
    d.add_argument("--metadata", default="")
    a = sub.add_parser("apply", help="apply PATCH to SOURCE and write OUTPUT")
    a.add_argument("source")
    a.add_argument("patch")
    a.add_argument("output")
    i = sub.add_parser("info", help="print a patch's header and checksums")
    i.add_argument("patch")
    args = parser.parse_args(argv)

    try:
        if args.cmd == "diff":
            patch = create_patch(_read(args.source), _read(args.target), args.metadata.encode("utf-8"))
            _write(args.patch, patch)
            print("wrote %s (%d bytes)" % (args.patch, len(patch)))
        elif args.cmd == "apply":
            result, _ = apply_patch(_read(args.source), _read(args.patch))
            _write(args.output, result)
            print("wrote %s (%d bytes)" % (args.output, len(result)))
        else:
            info = read_info(_read(args.patch))
            print("source size   %d" % info["source_size"])
            print("target size   %d" % info["target_size"])
            print("source CRC32  %08x" % info["source_crc32"])
            print("target CRC32  %08x" % info["target_crc32"])
            print("patch CRC32   %08x (%s)" % (info["patch_crc32"], "ok" if info["patch_crc_ok"] else "BAD"))
            if info["metadata"]:
                print("metadata:")
                print(info["metadata"].decode("utf-8", "replace"))
    except (BPSError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
