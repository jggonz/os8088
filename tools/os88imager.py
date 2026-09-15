#!/usr/bin/env python3
"""os8088 imager: interactive macOS floppy, USB and CD image writer.

Run `make imager`, or use --scan for a read-only inventory. No dependencies
beyond Python 3 and macOS utilities. See docs/IMAGER.md.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import os88disk  # noqa: E402  the --hdd image's own builder: hdd_retarget

ROOT = Path(__file__).resolve().parent.parent
STOCK_GEOMETRY = (os88disk.HDD_HEADS, os88disk.HDD_SPT)   # SPEC.md 80.1: 16 x 63
MIB = 1024 ** 2
# XTIDE Universal BIOS's addressing modes, by the card's P-CHS cylinder
# count: NORMAL to 1024, LARGE to 8192, LBA above (SPEC.md 80.5).
XTIDE_NORMAL_CYLS, XTIDE_LARGE_CYLS = 1024, 8192
KINDS = ('floppy', 'usb', 'cd')
LABELS = {'floppy': 'Floppy disks', 'usb': 'USB flash drives', 'cd': 'CD burners'}
FLOPPY_SIZES = {360 * 1024, 720 * 1024, 1200 * 1024, 1440 * 1024}
CHUNK = 1024 * 1024


class ImagerError(Exception):
    pass


def command(args):
    try:
        result = subprocess.run(args, capture_output=True)
    except OSError as exc:
        raise ImagerError('%s: %s' % (args[0], exc)) from exc
    if result.returncode:
        raise ImagerError(result.stderr.decode(errors='replace').strip() or
                          '%s failed (%d)' % (args[0], result.returncode))
    return result.stdout


def plist(args):
    try:
        return plistlib.loads(command(args))
    except (ValueError, plistlib.InvalidFileException) as exc:
        raise ImagerError('Invalid plist from %s' % args[0]) from exc


def info(device):
    return plist(['diskutil', 'info', '-plist', device])


def boot_disks():
    """Resolve APFS snapshots/containers to ALL physical boot stores."""
    root = info('/')
    stores = root.get('APFSPhysicalStores')
    if stores:
        disks = {info(s['APFSPhysicalStore']).get('ParentWholeDisk') for s in stores}
    elif root.get('APFSContainerReference'):
        raise ImagerError('Cannot resolve the APFS boot disk; refusing disk writes.')
    else:
        disks = {root.get('ParentWholeDisk') or
                 (root.get('DeviceIdentifier') if root.get('WholeDisk') else None)}
    if not disks or None in disks:
        raise ImagerError('Cannot identify the boot disk; refusing disk writes.')
    return disks


def disk_device(data, boot):
    dev = data.get('DeviceIdentifier', '')
    if (not re.fullmatch(r'disk\d+', dev) or dev in boot or
            data.get('WholeDisk') is not True or data.get('Internal') is not False or
            data.get('BusProtocol') != 'USB' or
            data.get('Writable') is not True):
        return None
    name = data.get('MediaName') or data.get('IORegistryEntryName') or dev
    size = data.get('TotalSize', 0)
    # USB floppy bridges often say only "TEAC ... Media". Standard media
    # capacity plus removable media handles those without a vendor whitelist.
    floppy = (bool(re.search(r'floppy|\bfdd\b', name, re.I)) or
              (size in FLOPPY_SIZES and data.get('RemovableMedia') is True))
    if not floppy and (size <= max(FLOPPY_SIZES) or
                       data.get('RemovableMedia') is not True):
        return None
    return {'id': dev, 'name': name, 'size': size,
            'kind': 'floppy' if floppy else 'usb',
            'identity': [data.get('DeviceTreePath'), data.get('MediaUUID'),
                         data.get('DiskUUID'), name, size]}


def disks():
    boot = boot_disks()
    listing = plist(['diskutil', 'list', '-plist', 'external', 'physical'])
    result = []
    for dev in listing.get('WholeDisks', []):
        data = disk_device(info(dev), boot)
        if data:
            result.append(data)
    return result


def burners():
    # drutil lists DiscRecording devices, and its numeric IDs can be passed
    # directly to -drive; never silently burn with the first attached drive.
    result = []
    for line in command(['drutil', 'list']).decode(errors='replace').splitlines():
        match = re.match(r'^\s*(\d+)\s+(.+?)\s*$', line)
        if not match:
            continue
        ident, name = match.groups()
        details = command(['drutil', '-drive', ident, 'info']).decode(errors='replace')
        writable = re.search(r'^\s*CD-Write:\s*(.+)$', details, re.M)
        if writable and re.search(r'\b(?:-R|-RW|R|RW)\b', writable.group(1)):
            result.append({'id': ident, 'name': name, 'size': 0, 'kind': 'cd',
                           'identity': [name, details]})
    return result


def detect():
    result, errors = [], []
    for scan in (disks, burners):
        try:
            result.extend(scan())
        except ImagerError as exc:
            errors.append(str(exc))
    return result, errors


def image_kind(path):
    """Classify actual bytes, including alternate BUILD directories/releases."""
    size = path.stat().st_size
    with path.open('rb') as src:
        head = src.read(512)
        if path.suffix.lower() == '.iso':
            src.seek(16 * 2048)
            if src.read(7) == b'\x01CD001\x01':
                return 'cd'
            return None
    if len(head) != 512 or head[510:] != b'\x55\xaa':
        return None
    if size in FLOPPY_SIZES:
        sector_size = struct.unpack_from('<H', head, 11)[0]
        sectors = struct.unpack_from('<H', head, 19)[0]
        if sector_size == 512 and sectors * sector_size == size and head[16] == 2:
            return 'floppy'
    for offset in range(446, 510, 16):
        entry = head[offset:offset + 16]
        start, count = struct.unpack_from('<II', entry, 8)
        if (entry[0] == 0x80 and entry[4] == 0x04 and start > 0 and count > 0
                and (start + count) * 512 <= size):
            return 'usb'
    return None


def images(directory):
    found, errors = [], []
    if not directory.is_dir():
        return [], ['Image directory does not exist: %s' % directory]
    for path in sorted(directory.rglob('*')):
        if path.suffix.lower() not in ('.img', '.iso') or not path.is_file():
            continue
        try:
            kind = image_kind(path)
            if kind:
                found.append({'path': str(path.resolve()), 'kind': kind,
                              'size': path.stat().st_size})
        except OSError as exc:
            errors.append('%s: %s' % (path, exc))
    return found, errors


def compatible(device, image):
    if device['kind'] != image['kind']:
        return False
    if device['kind'] == 'cd':
        return True  # DiscRecording checks the inserted blank disc.
    if device['kind'] == 'floppy':
        return device['size'] == image['size']
    return device['size'] >= image['size']


def revalidate(expected):
    candidates = burners() if expected['kind'] == 'cd' else disks()
    if expected not in candidates:
        raise ImagerError('Device changed or disappeared. Rescan and select it again.')


def digest_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as src:
        for chunk in iter(lambda: src.read(CHUNK), b''):
            digest.update(chunk)
    return digest.hexdigest()


def stream_and_verify(src, target, size, expected_hash):
    """Operate on already-open streams; tests use files, production a raw disk."""
    digest = hashlib.sha256()
    done = 0
    while done < size:
        chunk = src.read(min(CHUNK, size - done))
        if not chunk:
            raise ImagerError('Image became shorter during the write.')
        digest.update(chunk)
        remaining = memoryview(chunk)
        while remaining:
            written = target.write(remaining)
            if not written:
                raise ImagerError('Device stopped accepting writes.')
            remaining = remaining[written:]
        done += len(chunk)
        print('\rWriting: %d / %d bytes' % (done, size), end='', flush=True)
    target.flush()
    os.fsync(target.fileno())
    if digest.hexdigest() != expected_hash or src.read(1):
        raise ImagerError('Image changed during the write.')
    target.seek(0)
    digest = hashlib.sha256()
    done = 0
    while done < size:
        chunk = target.read(min(CHUNK, size - done))
        if not chunk:
            raise ImagerError('Device ended during verification.')
        digest.update(chunk)
        done += len(chunk)
        print('\rVerifying: %d / %d bytes' % (done, size), end='', flush=True)
    if digest.hexdigest() != expected_hash:
        raise ImagerError('Read-back mismatch. The medium did not retain the image.')
    print('\nVerified: SHA-256 match.')


def xtide_geometry(size):
    """The heads x spt an XTIDE Universal BIOS in Auto reports for a card of
    `size` bytes whose P-CHS is 16 x 63 - what CompactFlash cards above
    504 MiB report (SPEC.md 80.5). LARGE doubles the heads until the
    cylinders fit in 1024 (Revised Enhanced CHS); LBA picks them by capacity
    (assisted LBA), and a card LARGE cannot hold is past its last rung, so
    it is 255. None for a NORMAL-mode card: that geometry is the card's
    own, and this cannot know it."""
    sectors = size // 512
    cyls = sectors // (16 * 63)
    if cyls <= XTIDE_NORMAL_CYLS:
        return None
    if cyls <= XTIDE_LARGE_CYLS:
        heads = 16
        while cyls > XTIDE_NORMAL_CYLS:
            cyls //= 2
            heads *= 2
        return heads, 63, 'LARGE'
    return 255, 63, 'LBA'


def ask_geometry(device, image):
    """SPEC.md 80.5's one question, for a partitioned image on a USB-bus
    device: the stock geometry (None) or (heads, spt), or 'q'."""
    if device['kind'] != 'usb' or image['kind'] != 'usb':
        return None
    stock = '%d/%d' % STOCK_GEOMETRY
    print('\nGeometry: this image is %d heads x %d sectors per track. A PC booting a\n'
          'USB stick, QEMU and 86Box take that from the partition table. An XTIDE\n'
          'Universal BIOS (a CompactFlash card in an XT) reports the CARD\'s own\n'
          'geometry instead, and the image must be written to match it (SPEC.md 80.5).'
          % STOCK_GEOMETRY)
    suggestion = xtide_geometry(device['size'])
    if suggestion:
        heads, spt, mode = suggestion
        print('  A %s card runs XTIDE in %s mode, which reports %d/%d if the card\'s\n'
              '  own geometry is 16/63 (the usual). Its boot menu names the mode: if it\n'
              '  says %s, answer %d/%d.' % (size_label(device['size']), mode, heads, spt,
                                            mode, heads, spt))
    else:
        print('  A %s card runs XTIDE in NORMAL mode, which reports the card\'s own\n'
              '  geometry - unreadable through a USB reader. Type it from the card\'s\n'
              '  datasheet (a 256 MB SanDisk is 16/32; a card that is 16/63 already\n'
              '  boots the stock image).' % size_label(device['size']))
    while True:
        answer = input('Heads/sectors the booting BIOS reports (Enter keeps %s, q cancels): '
                       % stock).strip().lower()
        if answer == 'q':
            return 'q'
        if answer == '':
            return None
        try:
            cyls, heads, spt = os88disk.parse_geometry(answer)
        except ValueError as exc:
            print('  %s' % exc)
            continue
        if cyls and cyls * heads * spt * 512 < image.get('size', 0):
            print('  %d/%d/%d is %d sectors and the image is %d: the card cannot hold it.'
                  % (cyls, heads, spt, cyls * heads * spt, image['size'] // 512))
            continue
        if (heads, spt) == STOCK_GEOMETRY:
            return None
        return heads, spt


def retargeted(path, geometry):
    """The image as it will be written under `geometry`: (stream, size,
    SHA-256). The FILE is never touched; the ten bytes move in memory
    (os88disk.hdd_retarget), and the digest of what is written is what the
    read-back is compared with."""
    data = path.read_bytes()
    if geometry:
        try:
            data = os88disk.hdd_retarget(data, *geometry)
        except ValueError as exc:
            raise ImagerError('Cannot retarget this image: %s' % exc) from exc
    return io.BytesIO(data), len(data), hashlib.sha256(data).hexdigest()


def write_disk(path, expected, expected_hash, geometry=None):
    revalidate(expected)
    image = {'kind': image_kind(path), 'size': path.stat().st_size}
    if not compatible(expected, image) or digest_file(path) != expected_hash:
        raise ImagerError('Image changed or does not fit this medium.')
    # Read the source BEFORE unmounting, so images on the selected medium
    # cannot turn into missing paths halfway through the operation. Whole,
    # because a retarget rewrites two sectors of it and the hash on the way
    # out must be of what is written.
    src, size, write_hash = retargeted(path, geometry)
    if geometry:
        print('Retargeted to %d heads x %d sectors; SHA-256 as written: %s'
              % (geometry[0], geometry[1], write_hash))
    command(['diskutil', 'unmountDisk', '/dev/' + expected['id']])
    revalidate(expected)
    dev = '/dev/r' + expected['id']
    fd = os.open(dev, os.O_RDWR | os.O_NOFOLLOW)
    with os.fdopen(fd, 'r+b', buffering=0) as target:
        if not stat.S_ISCHR(os.fstat(target.fileno()).st_mode):
            raise ImagerError('Target is not a raw disk device.')
        stream_and_verify(src, target, size, write_hash)
    command(['diskutil', 'eject', '/dev/' + expected['id']])
    print('Ejected. The medium is ready to remove.')


def choose(prompt, entries):
    for i, entry in enumerate(entries, 1):
        print('  %d) %s' % (i, entry))
    while True:
        answer = input(prompt + ' (number, r to rescan, q to go back): ').strip().lower()
        if answer in ('q', 'r', ''):
            return answer or 'q'
        if answer.isdigit() and 1 <= int(answer) <= len(entries):
            return int(answer) - 1
        print('Choose a listed number, r, or q.')


def size_label(size):
    return '%.0f KiB' % (size / 1024) if size < 2 * 1024**2 else '%.1f MiB' % (size / 1024**2)


def perform(device, image):
    path = Path(image['path'])
    expected_hash = digest_file(path)
    print('\nImage: %s (%s)\nTarget: %s — %s' %
          (path, size_label(image['size']), device['id'], device['name']))
    print('SHA-256: %s' % expected_hash)
    geometry = ask_geometry(device, image)
    if geometry == 'q':
        print('Cancelled. Nothing was written.')
        return
    if geometry:
        _, _, write_hash = retargeted(path, geometry)
        print('Written as %d heads x %d sectors per track (the file is unchanged).\n'
              'SHA-256 as written: %s' % (geometry[0], geometry[1], write_hash))
    print('This writes the selected medium and destroys its existing contents.')
    if device['kind'] == 'cd':
        print('Insert a blank writable CD.')
    if input('Type %s to write, anything else cancels: ' % device['id']).strip() != device['id']:
        print('Cancelled. Nothing was written.')
        return
    revalidate(device)
    if image_kind(path) != image['kind'] or digest_file(path) != expected_hash:
        raise ImagerError('Image changed. Select it again.')
    if device['kind'] == 'cd':
        args = ['drutil', '-drive', device['id'], 'burn', '-verify', str(path)]
    else:
        args = ['sudo', sys.executable, str(Path(__file__).resolve()),
                '--_write', str(path), json.dumps(device), expected_hash,
                '%d/%d' % geometry if geometry else '-']
    if subprocess.call(args):
        raise ImagerError('Write or verification failed. The medium may be incomplete; rescan to retry.')
    print('Media creation completed.')


def main(argv=None):
    parser = argparse.ArgumentParser(prog='os8088 imager', description=__doc__)
    parser.add_argument('--scan', action='store_true', help='list devices and images without writing')
    parser.add_argument('--images', type=Path, default=ROOT / 'build',
                        help='search this directory recursively (default: build/)')
    parser.add_argument('--_write', nargs=4, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if sys.platform != 'darwin':
        raise ImagerError('Device detection and writing currently require macOS.')
    if args._write:
        path, device, digest, geometry = args._write
        if geometry == '-':
            geometry = None
        else:
            _, heads, spt = os88disk.parse_geometry(geometry)
            geometry = (heads, spt)
        write_disk(Path(path), json.loads(device), digest, geometry)
        return 0
    print('os8088 imager')
    while True:
        devices, errors = detect()
        catalog, image_errors = images(args.images.expanduser().resolve())
        for error in errors + image_errors:
            print('Notice: ' + error)
        for kind in KINDS:
            print('\n%s:' % LABELS[kind])
            group = [d for d in devices if d['kind'] == kind]
            for device in group:
                print('  %s: %s (%s)' % (device['id'], device['name'],
                      size_label(device['size']) if device['size'] else 'capacity checked at burn'))
            if not group:
                print('  None detected. Attach a drive%s, then rescan.' %
                      (' and insert a floppy' if kind == 'floppy' else ''))
            for item in catalog:
                if item['kind'] == kind:
                    print('    Image: %s (%s)' % (item['path'], size_label(item['size'])))
            if not any(i['kind'] == kind for i in catalog):
                print('    No built images. Build floppies with make; USB/CD with make live.')
        if args.scan:
            return 1 if errors or image_errors else 0
        selected = choose('\nChoose a device',
                          ['%s: %s — %s' % (LABELS[d['kind']], d['id'], d['name']) for d in devices])
        if selected == 'q':
            return 0
        if selected == 'r':
            continue
        device = devices[selected]
        matches = [i for i in catalog if compatible(device, i)]
        if not matches:
            print('No images fit this medium. Floppy image and disk capacities must match exactly.')
            continue
        selected = choose('Choose an image',
                          ['%s (%s)' % (i['path'], size_label(i['size'])) for i in matches])
        if isinstance(selected, int):
            try:
                perform(device, matches[selected])
            except (ImagerError, OSError) as exc:
                print('\nError: %s' % exc)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (EOFError, KeyboardInterrupt):
        print('\nStopped. If writing had started, the medium may be incomplete.')
        sys.exit(130)
    except (ImagerError, OSError, ValueError) as exc:
        print('\nos8088 imager: %s' % exc, file=sys.stderr)
        sys.exit(1)
