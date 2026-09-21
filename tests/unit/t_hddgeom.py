#!/usr/bin/env python3
"""SPEC.md 80.5: retargeting a --hdd image to the geometry a period ROM
reports - python3 tests/unit/t_hddgeom.py.

`os88disk.hdd_retarget` moves exactly ten bytes (each partition entry's
two CHS columns, the volume's BPB_SecPerTrk/BPB_NumHeads) and refuses
anything it did not build; `--verify-hdd` fails the disagreement field
note 33 was. The synthetic image here is the smallest thing those checks
read; the shipped live image, when `make live` has built it, is retargeted
and verified as well.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import os88disk as disk  # noqa: E402

SECTOR = disk.SECTOR
LIVE = ROOT / 'build' / 'os8088-usb.img'


def hdd_image(nsec=200, base=63, heads=disk.HDD_HEADS, spt=disk.HDD_SPT):
    """An MBR with one active type-04 entry at `base`, and a boot record
    there whose BPB carries `heads` x `spt` and BPB_HiddSec = base. Nothing
    else - hdd_retarget reads nothing else."""
    img = bytearray(nsec * SECTOR)
    cnt = nsec - base
    ent = bytearray(16)
    ent[0] = 0x80
    ent[1:4] = disk.hdd_chs(base, heads, spt)
    ent[4] = 0x04
    ent[5:8] = disk.hdd_chs(base + cnt - 1, heads, spt)
    struct.pack_into('<II', ent, 8, base, cnt)
    img[disk.HP_TBL:disk.HP_TBL + 16] = ent
    img[510:512] = b'\x55\xaa'
    v = base * SECTOR
    struct.pack_into('<HH', img, v + 24, spt, heads)
    struct.pack_into('<I', img, v + 28, base)
    img[v + 510:v + 512] = b'\x55\xaa'
    return bytes(img)


class RetargetTests(unittest.TestCase):
    def test_moves_exactly_the_ten_bytes(self):
        src = hdd_image()
        out = disk.hdd_retarget(src, 64, 63)
        moved = {i for i in range(len(src)) if src[i] != out[i]}
        allowed = set(range(disk.HP_TBL + 1, disk.HP_TBL + 4)) | \
            set(range(disk.HP_TBL + 5, disk.HP_TBL + 8)) | \
            set(range(63 * SECTOR + 24, 63 * SECTOR + 28))
        self.assertTrue(moved <= allowed, sorted(moved - allowed))
        self.assertEqual(struct.unpack_from('<HH', out, 63 * SECTOR + 24), (63, 64))
        ent = out[disk.HP_TBL:disk.HP_TBL + 16]
        self.assertEqual(disk.chs_lba(ent[1:4], 64, 63), 63)
        self.assertEqual(disk.chs_lba(ent[5:8], 64, 63), 199)
        # ...and the LBA column, the type and the flag did not move
        self.assertEqual(ent[0], 0x80)
        self.assertEqual(ent[4], 0x04)
        self.assertEqual(struct.unpack_from('<II', ent, 8), (63, 137))

    def test_round_trip_is_identity(self):
        src = hdd_image(nsec=4000)
        for heads, spt in ((32, 63), (255, 63), (4, 32), (2, 17)):
            with self.subTest(geometry=(heads, spt)):
                there = disk.hdd_retarget(src, heads, spt)
                self.assertNotEqual(there, src)
                self.assertEqual(disk.hdd_retarget(there, disk.HDD_HEADS, disk.HDD_SPT), src)

    def test_same_geometry_is_a_no_op(self):
        src = hdd_image()
        self.assertEqual(disk.hdd_retarget(src, disk.HDD_HEADS, disk.HDD_SPT), src)

    def test_refusals(self):
        src = bytearray(hdd_image(nsec=1200))
        with self.assertRaisesRegex(ValueError, 'cylinder'):
            disk.hdd_retarget(bytes(src), 1, 1)          # LBA 1199 needs cylinder 1199
        for heads, spt in ((0, 63), (256, 63), (16, 0), (16, 64)):
            with self.subTest(geometry=(heads, spt)), self.assertRaises(ValueError):
                disk.hdd_retarget(bytes(src), heads, spt)
        nosig = bytearray(src)
        nosig[511] = 0
        with self.assertRaisesRegex(ValueError, 'signature'):
            disk.hdd_retarget(bytes(nosig), 32, 63)
        foreign = bytearray(src)
        foreign[disk.HP_TBL + 2] ^= 1                    # a start column that disagrees
        with self.assertRaisesRegex(ValueError, 'disagree'):
            disk.hdd_retarget(bytes(foreign), 32, 63)
        wrong_home = bytearray(src)
        struct.pack_into('<I', wrong_home, 63 * SECTOR + 28, 62)
        with self.assertRaisesRegex(ValueError, 'starts at'):
            disk.hdd_retarget(bytes(wrong_home), 32, 63)
        short = src[:100 * SECTOR]                       # the partition runs past the end
        with self.assertRaisesRegex(ValueError, 'inside'):
            disk.hdd_retarget(bytes(short), 32, 63)
        empty = bytearray(2 * SECTOR)
        empty[510:512] = b'\x55\xaa'
        with self.assertRaisesRegex(ValueError, 'no partition'):
            disk.hdd_retarget(bytes(empty), 32, 63)

    def test_parse_geometry(self):
        self.assertEqual(disk.parse_geometry('32/63'), (None, 32, 63))
        self.assertEqual(disk.parse_geometry(' 1985/16/63 '), (1985, 16, 63))
        self.assertEqual(disk.parse_geometry('16x63'), (None, 16, 63))
        for bad in ('', '16', '16/63/1/1', 'a/b', '0/63', '16/64', '256/63', '0/16/63'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                disk.parse_geometry(bad)

    def test_cli_refuses_a_card_that_cannot_hold_the_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'hd.img'
            src.write_bytes(hdd_image(nsec=4000))
            out = Path(tmp) / 'out.img'
            r = subprocess.run([sys.executable, str(ROOT / 'tools' / 'os88disk.py'),
                                '--retarget', str(src), '--geometry', '10/16/16',
                                '-o', str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1, r.stderr)
            self.assertIn('2560 sectors and the image is 4000', r.stderr)
            self.assertFalse(out.exists())
            r = subprocess.run([sys.executable, str(ROOT / 'tools' / 'os88disk.py'),
                                '--retarget', str(src), '--geometry', '20/16/16',
                                '-o', str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(out.read_bytes(), disk.hdd_retarget(src.read_bytes(), 16, 16))

    @unittest.skipUnless(LIVE.exists(), 'build/os8088-usb.img is on demand (make live)')
    def test_live_image_retargets_and_verifies(self):
        src = LIVE.read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            for heads in (32, 64, 128, 255):
                with self.subTest(heads=heads):
                    out = Path(tmp) / ('cf%d.img' % heads)
                    out.write_bytes(disk.hdd_retarget(src, heads, 63))
                    r = subprocess.run([sys.executable, str(ROOT / 'tools' / 'os88disk.py'),
                                        '--verify-hdd', str(out)], capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stderr)
            # ...and the disagreement field note 33 was is what --verify-hdd fails
            bad = bytearray(src)
            struct.pack_into('<H', bad, 63 * SECTOR + 26, 32)     # BPB says 32 heads, the table 16
            out = Path(tmp) / 'bad.img'
            out.write_bytes(bad)
            r = subprocess.run([sys.executable, str(ROOT / 'tools' / 'os88disk.py'),
                                '--verify-hdd', str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
            self.assertIn('CHS column', r.stderr)


if __name__ == '__main__':
    unittest.main()
