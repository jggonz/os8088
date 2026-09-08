#!/usr/bin/env python3
"""Hardware-free imager safety and workflow tests: python3 tests/unit/t_imager.py."""
import contextlib
import hashlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import os88imager as imager


def disk(**changes):
    data = dict(DeviceIdentifier='disk4', WholeDisk=True, Internal=False,
                BusProtocol='USB', Writable=True, RemovableMedia=True,
                TotalSize=64 * 1024**2, MediaName='USB flash drive',
                DeviceTreePath='IOService/USB/stick', MediaUUID='medium-1')
    data.update(changes)
    return data


def floppy(path, size=1440 * 1024):
    header = bytearray(512)
    header[510:] = b'\x55\xaa'
    struct.pack_into('<H', header, 11, 512)
    struct.pack_into('<H', header, 19, size // 512)
    header[16] = 2
    with path.open('wb') as out:
        out.write(header)
        out.truncate(size)
    return path


class ImagerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_catalog_all_floppy_geometries_nested_and_iso_usb(self):
        for size in imager.FLOPPY_SIZES:
            floppy(self.root / ('os8088-%d.img' % size), size)
        sub = self.root / 'small'
        sub.mkdir()
        floppy(sub / 'apps.img')
        iso = self.root / 'os8088.iso'
        with iso.open('wb') as out:
            out.seek(32768)
            out.write(b'\x01CD001\x01')
        usb = self.root / 'os8088-usb.img'
        header = bytearray(512)
        header[510:] = b'\x55\xaa'
        header[446] = 0x80
        header[450] = 4
        struct.pack_into('<II', header, 454, 63, 100)
        with usb.open('wb') as out:
            out.write(header)
            out.truncate(163 * 512)
        (self.root / 'bad.img').write_bytes(header)  # partition past EOF
        (self.root / 'bad.iso').write_bytes(b'not an iso')
        (self.root / 'scratch.img').write_bytes(bytes(1440 * 1024))
        catalog, errors = imager.images(self.root)
        self.assertEqual(errors, [])
        self.assertEqual(sorted(i['kind'] for i in catalog), ['cd'] + ['floppy'] * 5 + ['usb'])

    def test_missing_catalog(self):
        catalog, errors = imager.images(self.root / 'missing')
        self.assertFalse(catalog)
        self.assertTrue(errors)

    def test_reject_unsafe_or_unknown_disks(self):
        for changes in ({'Internal': True}, {'Internal': None}, {'WholeDisk': False},
                        {'BusProtocol': 'Thunderbolt'}, {'Writable': False},
                        {'RemovableMedia': False}, {'DeviceIdentifier': 'disk4s1'},
                        {'TotalSize': 0}):
            with self.subTest(changes=changes):
                self.assertIsNone(imager.disk_device(disk(**changes), {'disk0'}))
        self.assertIsNone(imager.disk_device(disk(), {'disk4'}))
        self.assertEqual(imager.disk_device(disk(), {'disk0'})['kind'], 'usb')

    def test_diskutil_usb_inventory_uses_whole_disk_key(self):
        # Fields observed on the attached 16 GB USB stick. diskutil emits
        # WholeDisk, not Whole; the latter silently hid every physical disk.
        observed = {
            'DeviceIdentifier': 'disk7', 'ParentWholeDisk': 'disk7',
            'WholeDisk': True, 'Internal': False, 'BusProtocol': 'USB',
            'RemovableMedia': True, 'Writable': True,
            'TotalSize': 15728640000, 'MediaName': 'ProductCode',
        }
        with patch.object(imager, 'boot_disks', return_value={'disk0'}), \
                patch.object(imager, 'plist', return_value={'WholeDisks': ['disk7']}), \
                patch.object(imager, 'info', return_value=observed):
            found = imager.disks()
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]['id'], 'disk7')
        self.assertEqual(found[0]['kind'], 'usb')
        self.assertEqual(found[0]['size'], 15728640000)
        for value in (False, None):
            self.assertIsNone(imager.disk_device(dict(observed, WholeDisk=value), {'disk0'}))
        self.assertIsNone(imager.disk_device(observed, {'disk7'}))

    def test_whole_boot_device_without_parent(self):
        with patch.object(imager, 'info', return_value={
                'DeviceIdentifier': 'disk7', 'WholeDisk': True}):
            self.assertEqual(imager.boot_disks(), {'disk7'})

    def test_floppy_bridge_capacity_and_empty_drive(self):
        device = imager.disk_device(disk(MediaName='TEAC UF000x Media',
                                         TotalSize=720 * 1024), {'disk0'})
        self.assertEqual(device['kind'], 'floppy')
        self.assertTrue(imager.compatible(device, {'kind': 'floppy', 'size': 720 * 1024}))
        self.assertFalse(imager.compatible(device, {'kind': 'floppy', 'size': 360 * 1024}))
        self.assertFalse(imager.compatible(device, {'kind': 'usb', 'size': 720 * 1024}))
        empty = imager.disk_device(disk(MediaName='USB FDD', TotalSize=0), {'disk0'})
        self.assertEqual(empty['kind'], 'floppy')
        self.assertFalse(imager.compatible(empty, {'kind': 'floppy', 'size': 720 * 1024}))

    def test_usb_capacity(self):
        self.assertFalse(imager.compatible({'kind': 'usb', 'size': 10}, {'kind': 'usb', 'size': 11}))
        self.assertTrue(imager.compatible({'kind': 'usb', 'size': 11}, {'kind': 'usb', 'size': 11}))

    def test_apfs_physical_boot_stores(self):
        values = {'/': {'ParentWholeDisk': 'disk3', 'APFSPhysicalStores': [
            {'APFSPhysicalStore': 'disk0s2'}, {'APFSPhysicalStore': 'disk4s2'}]},
            'disk0s2': {'ParentWholeDisk': 'disk0'}, 'disk4s2': {'ParentWholeDisk': 'disk4'}}
        with patch.object(imager, 'info', side_effect=values.__getitem__):
            self.assertEqual(imager.boot_disks(), {'disk0', 'disk4'})
        for data in ({}, {'APFSContainerReference': 'disk3', 'ParentWholeDisk': 'disk3'}):
            with patch.object(imager, 'info', return_value=data), self.assertRaises(imager.ImagerError):
                imager.boot_disks()

    def test_burners_filter_read_only_and_preserve_ids(self):
        outputs = [b'   Vendor Product Rev Bus SupportLevel\n1: wrong shape\n1  V CD-ROM 1 USB\n2  V Burner 2 USB\n',
                   b'CD-Write: none\n', b'CD-Write: -R, -RW\n']
        with patch.object(imager, 'command', side_effect=outputs):
            result = imager.burners()
        self.assertEqual([d['id'] for d in result], ['2'])

    def test_changed_device_and_unknown_boot_prevent_write(self):
        device = imager.disk_device(disk(), {'disk0'})
        with patch.object(imager, 'disks', return_value=[]), patch.object(imager, 'command') as cmd:
            with self.assertRaises(imager.ImagerError):
                imager.write_disk(self.root / 'missing.img', device, '')
            cmd.assert_not_called()

    def test_cancel_never_unmounts_or_escalates(self):
        path = floppy(self.root / 'system.img')
        device = imager.disk_device(disk(TotalSize=path.stat().st_size), {'disk0'})
        image = {'path': str(path), 'kind': 'floppy', 'size': path.stat().st_size}
        with patch('builtins.input', return_value='no'), patch.object(imager, 'command') as cmd, \
                patch.object(imager.subprocess, 'call') as call, contextlib.redirect_stdout(io.StringIO()):
            imager.perform(device, image)
            cmd.assert_not_called()
            call.assert_not_called()

    def test_selected_burner_and_verify_flag(self):
        path = self.root / 'live.iso'
        with path.open('wb') as out:
            out.seek(32768)
            out.write(b'\x01CD001\x01')
        device = {'id': '2', 'kind': 'cd', 'name': 'second burner'}
        with patch('builtins.input', return_value='2'), patch.object(imager, 'revalidate'), \
                patch.object(imager.subprocess, 'call', return_value=0) as call, \
                contextlib.redirect_stdout(io.StringIO()):
            imager.perform(device, {'path': str(path), 'kind': 'cd', 'size': path.stat().st_size})
        self.assertEqual(call.call_args.args[0], ['drutil', '-drive', '2', 'burn', '-verify', str(path)])

    def test_stream_verifies_actual_bytes_and_preserves_trailing_capacity(self):
        payload = bytes(range(256)) * 4100
        target_path = self.root / 'target'
        target_path.write_bytes(b'X' * (len(payload) + 512))
        with target_path.open('r+b', buffering=0) as target, contextlib.redirect_stdout(io.StringIO()):
            imager.stream_and_verify(io.BytesIO(payload), target, len(payload), hashlib.sha256(payload).hexdigest())
        self.assertEqual(target_path.read_bytes(), payload + b'X' * 512)

    def test_zero_write_does_not_loop(self):
        class Stalled(io.BytesIO):
            def write(self, data):
                return 0
        with self.assertRaisesRegex(imager.ImagerError, 'stopped accepting'):
            imager.stream_and_verify(io.BytesIO(b'123'), Stalled(), 3, '')

    def test_readback_corruption_detected(self):
        class Corrupt:
            def __init__(self, f):
                self.f = f
            def __getattr__(self, name):
                return getattr(self.f, name)
            def read(self, size):
                data = self.f.read(size)
                return b'X' * len(data)
        with (self.root / 'target').open('w+b', buffering=0) as target, \
                contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(imager.ImagerError, 'mismatch'):
            imager.stream_and_verify(io.BytesIO(b'123'), Corrupt(target), 3, hashlib.sha256(b'123').hexdigest())

    def test_partial_writes_complete(self):
        class Partial:
            def __init__(self, f):
                self.f = f
            def __getattr__(self, name):
                return getattr(self.f, name)
            def write(self, data):
                return self.f.write(data[:2])
        payload = b'partial write test'
        with (self.root / 'target').open('w+b', buffering=0) as target, \
                contextlib.redirect_stdout(io.StringIO()):
            imager.stream_and_verify(io.BytesIO(payload), Partial(target), len(payload),
                                     hashlib.sha256(payload).hexdigest())
        self.assertEqual((self.root / 'target').read_bytes(), payload)

    def test_unmount_failure_never_opens_target(self):
        path = floppy(self.root / 'system.img')
        device = imager.disk_device(disk(TotalSize=path.stat().st_size), {'disk0'})
        with patch.object(imager, 'revalidate'), \
                patch.object(imager, 'command', side_effect=imager.ImagerError('busy')), \
                patch.object(imager.os, 'open') as raw_open, self.assertRaisesRegex(imager.ImagerError, 'busy'):
            imager.write_disk(path, device, imager.digest_file(path))
        raw_open.assert_not_called()

    def test_changed_image_never_unmounts(self):
        path = floppy(self.root / 'system.img')
        device = imager.disk_device(disk(TotalSize=path.stat().st_size), {'disk0'})
        with patch.object(imager, 'revalidate'), patch.object(imager, 'command') as cmd, \
                self.assertRaisesRegex(imager.ImagerError, 'Image changed'):
            imager.write_disk(path, device, 'old checksum')
        cmd.assert_not_called()

    def test_device_swap_after_unmount_never_opens_target(self):
        path = floppy(self.root / 'system.img')
        device = imager.disk_device(disk(TotalSize=path.stat().st_size), {'disk0'})
        with patch.object(imager, 'revalidate', side_effect=[None, imager.ImagerError('swapped')]), \
                patch.object(imager, 'command'), patch.object(imager.os, 'open') as raw_open, \
                self.assertRaisesRegex(imager.ImagerError, 'swapped'):
            imager.write_disk(path, device, imager.digest_file(path))
        raw_open.assert_not_called()

    def test_menu_rescan_selection_and_cancel(self):
        path = floppy(self.root / 'system.img')
        device = imager.disk_device(disk(TotalSize=path.stat().st_size), {'disk0'})
        with patch.object(imager.sys, 'platform', 'darwin'), \
                patch.object(imager, 'detect', return_value=([device], [])) as scan, \
                patch('builtins.input', side_effect=['r', '1', '1', 'cancel', 'q']), \
                patch.object(imager.subprocess, 'call') as call, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(imager.main(['--images', str(self.root)]), 0)
        self.assertEqual(scan.call_count, 3)
        call.assert_not_called()

    def test_scan_never_writes(self):
        with patch.object(imager.sys, 'platform', 'darwin'), patch.object(imager, 'detect', return_value=([], [])), \
                patch.object(imager.subprocess, 'call') as call, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(imager.main(['--scan', '--images', str(self.root)]), 0)
            call.assert_not_called()


if __name__ == '__main__':
    unittest.main()
