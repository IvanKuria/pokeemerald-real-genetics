#!/usr/bin/env python3
"""Tests for bps.py. Run: python3 tools/realgen/test_bps.py

If a Flips binary is found (FLIPS env var, `flips` on PATH, or ~/Tools/gba/bin/flips), patches are also
cross-checked against it in both directions.
"""

import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bps  # noqa: E402


def find_flips():
    for candidate in (os.environ.get("FLIPS"), shutil.which("flips"),
                      os.path.expanduser("~/Tools/gba/bin/flips")):
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def rom_like(rng, size):
    """Bytes that look a bit like a ROM: code-ish noise, repeated tables and 0xFF padding."""
    out = bytearray()
    table = bytes(rng.randrange(256) for _ in range(64))
    while len(out) < size * 3 // 4:
        kind = rng.randrange(3)
        if kind == 0:
            out += bytes(rng.randrange(256) for _ in range(rng.randrange(1, 300)))
        elif kind == 1:
            out += table * rng.randrange(1, 4)
        else:
            out += bytes(rng.randrange(4) for _ in range(rng.randrange(1, 100)))
    out = out[:size * 3 // 4]
    out += b"\xff" * (size - len(out))
    return bytes(out)


def mutate(rng, data, edits):
    data = bytearray(data)
    for _ in range(edits):
        kind = rng.randrange(4)
        at = rng.randrange(len(data) + 1) if data else 0
        n = rng.randrange(1, 40)
        if kind == 0 and data:      # overwrite
            for i in range(at, min(at + n, len(data))):
                data[i] = rng.randrange(256)
        elif kind == 1:             # insert (shifts everything after it)
            data[at:at] = bytes(rng.randrange(256) for _ in range(n))
        elif kind == 2 and data:    # delete
            del data[at:at + n]
        else:                       # duplicate an earlier block somewhere else
            if len(data) > 64:
                src = rng.randrange(len(data) - 32)
                data[at:at] = data[src:src + 32]
    return bytes(data)


class NumberTests(unittest.TestCase):
    def test_round_trip(self):
        values = [0, 1, 0x7F, 0x80, 0x81, 0x3FFF, 0x4000, 0x407F, 0x4080, 0x1FFFFF, 0x200000,
                  2 ** 24, 2 ** 32 - 1, 2 ** 40 + 12345]
        for v in values:
            out = bytearray()
            bps.encode_number(v, out)
            got, pos = bps.decode_number(out, 0)
            self.assertEqual(got, v)
            self.assertEqual(pos, len(out))

    def test_known_encodings(self):
        # values from the reference encoder: each byte holds 7 bits, with the "minus one" trick
        cases = {0: b"\x80", 1: b"\x81", 127: b"\xff", 128: b"\x00\x80", 255: b"\x7f\x80", 16511: b"\x7f\xff"}
        for v, encoded in cases.items():
            out = bytearray()
            bps.encode_number(v, out)
            self.assertEqual(bytes(out), encoded, v)

    def test_signed(self):
        for v in (0, 1, -1, 5, -5, 1 << 30, -(1 << 30)):
            out = bytearray()
            bps.encode_signed(v, out)
            self.assertEqual(bps.decode_signed(out, 0)[0], v)


class RoundTripTests(unittest.TestCase):
    def check(self, source, target, metadata=b""):
        patch = bps.create_patch(source, target, metadata)
        result, meta = bps.apply_patch(source, patch)
        self.assertEqual(result, target)
        self.assertEqual(meta, metadata)
        return patch

    def test_identical(self):
        data = rom_like(random.Random(1), 50000)
        patch = self.check(data, data)
        self.assertLess(len(patch), 40)

    def test_empty_cases(self):
        self.check(b"", b"")
        self.check(b"", b"hello")
        self.check(b"hello", b"")

    def test_single_byte_change(self):
        rng = random.Random(2)
        data = rom_like(rng, 100000)
        changed = bytearray(data)
        changed[54321] ^= 0x55
        patch = self.check(data, bytes(changed))
        self.assertLess(len(patch), 60)

    def test_insertion_shift_stays_small(self):
        rng = random.Random(3)
        data = rom_like(rng, 200000)
        # insert 40 new bytes near the start, which shifts everything after it
        target = data[:1000] + bytes(rng.randrange(256) for _ in range(40)) + data[1000:-40]
        patch = self.check(data, target)
        self.assertLess(len(patch), 200)

    def test_random_mutations(self):
        rng = random.Random(4)
        for trial in range(30):
            size = rng.choice([1, 17, 300, 5000, 70000])
            source = rom_like(rng, size)
            target = mutate(rng, source, rng.randrange(0, 25))
            self.check(source, target, b"trial %d" % trial)

    def test_unrelated_data(self):
        rng = random.Random(5)
        a = bytes(rng.randrange(256) for _ in range(20000))
        b = bytes(rng.randrange(256) for _ in range(30000))
        self.check(a, b)

    def test_target_bigger_and_smaller(self):
        rng = random.Random(6)
        data = rom_like(rng, 40000)
        self.check(data, data + b"\x00" * 9000 + data[:5000])
        self.check(data, data[3000:20000])

    def test_metadata(self):
        meta = '<?xml version="1.0" encoding="UTF-8"?><patch>Pokémon</patch>'.encode("utf-8")
        patch = self.check(b"abcdefgh" * 100, b"abcdefgx" * 100, meta)
        self.assertEqual(bps.read_info(patch)["metadata"], meta)


class DecoderTests(unittest.TestCase):
    def build(self, source, target, actions):
        """Hand-assemble a patch from a list of (command, length, extra) tuples."""
        out = bytearray(bps.MAGIC)
        bps.encode_number(len(source), out)
        bps.encode_number(len(target), out)
        bps.encode_number(0, out)
        for command, length, extra in actions:
            bps.encode_number(((length - 1) << 2) | command, out)
            if command == bps.TARGET_READ:
                out += extra
            elif command in (bps.SOURCE_COPY, bps.TARGET_COPY):
                bps.encode_signed(extra, out)
        out += struct.pack("<II", bps.crc32(source), bps.crc32(target))
        out += struct.pack("<I", bps.crc32(out))
        return bytes(out)

    def test_target_copy_run_length(self):
        source = b""
        target = b"ab" + b"ab" * 50
        patch = self.build(source, target, [(bps.TARGET_READ, 2, b"ab"), (bps.TARGET_COPY, 100, 0)])
        self.assertEqual(bps.apply_patch(source, patch)[0], target)

    def test_source_copy_negative_offsets(self):
        source = b"0123456789"
        target = b"6789" + b"0123" + b"45"
        actions = [(bps.SOURCE_COPY, 4, 6), (bps.SOURCE_COPY, 4, -10), (bps.SOURCE_COPY, 2, 0)]
        patch = self.build(source, target, actions)
        self.assertEqual(bps.apply_patch(source, patch)[0], target)

    def test_rejects_wrong_source(self):
        patch = bps.create_patch(b"original data here", b"changed data here")
        with self.assertRaises(bps.BPSError):
            bps.apply_patch(b"original data herE", patch)
        with self.assertRaises(bps.BPSError):
            bps.apply_patch(b"short", patch)

    def test_rejects_corrupt_patch(self):
        patch = bytearray(bps.create_patch(b"x" * 1000, b"y" * 1000))
        patch[6] ^= 1
        with self.assertRaises(bps.BPSError):
            bps.apply_patch(b"x" * 1000, bytes(patch))

    def test_rejects_garbage(self):
        with self.assertRaises(bps.BPSError):
            bps.apply_patch(b"", b"not a patch at all")


@unittest.skipIf(find_flips() is None, "Flips not found, skipping the cross-check")
class FlipsCrossCheck(unittest.TestCase):
    def setUp(self):
        self.flips = find_flips()
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def path(self, name, data=None):
        p = os.path.join(self.tmp, name)
        if data is not None:
            with open(p, "wb") as f:
                f.write(data)
        return p

    def pair(self, seed):
        rng = random.Random(seed)
        source = rom_like(rng, 300000)
        return source, mutate(rng, source, 40)

    def test_flips_applies_our_patch(self):
        for seed in range(3):
            source, target = self.pair(seed)
            self.path("p.bps", bps.create_patch(source, target))
            subprocess.run([self.flips, "--apply", self.path("p.bps"), self.path("s.bin", source),
                            self.path("out.bin")], check=True, stdout=subprocess.DEVNULL)
            with open(self.path("out.bin"), "rb") as f:
                self.assertEqual(f.read(), target)

    def test_we_apply_flips_patch(self):
        for seed in range(3):
            source, target = self.pair(seed + 10)
            subprocess.run([self.flips, "--create", "--bps-delta", self.path("s.bin", source),
                            self.path("t.bin", target), self.path("f.bps")], check=True,
                           stdout=subprocess.DEVNULL)
            with open(self.path("f.bps"), "rb") as f:
                self.assertEqual(bps.apply_patch(source, f.read())[0], target)


if __name__ == "__main__":
    unittest.main(verbosity=2)
