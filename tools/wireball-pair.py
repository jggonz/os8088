#!/usr/bin/env python3
"""Build and open two 86Box 6.0+ XTs joined by a COM2 null modem.

Run from any directory: python3 /path/to/os8088/tools/wireball-pair.py
Requires the repo build dependencies, 86Box 6.0+, and its XT/OTI ROMs.
On macOS, tools/setup-macos.sh installs those dependencies and ROMs.
"""
import argparse
import configparser
import fcntl
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
PAIR = BUILD / '86box-wireball-pair'
TITLE = 'WireBall'
APP_IMAGE = 'wireball360.img'
APP_DISK = 'wireball.img'


def running_profiles():
    """Match the exact profile arguments, including VMs opened outside this tool."""
    output = subprocess.check_output(['ps', '-axo', 'pid=,args='], text=True)
    running = []
    for number in (1, 2):
        profile = str(PAIR / f'pc{number}')
        for line in output.splitlines():
            fields = line.split(maxsplit=1)
            if len(fields) < 2 or not re.match(r'^(?:.*?/)?86box\s', fields[1], re.IGNORECASE):
                continue
            if any(f'{flag} {profile} ' in fields[1] + ' ' for flag in ('-P', '--vmpath')):
                running.append(number)
                break
    return running


def find_box(override):
    candidate = override or os.environ.get('BOX')
    if not candidate and sys.platform == 'darwin':
        for app in (Path('/Applications/86Box.app'), Path.home() / 'Applications/86Box.app'):
            if app.exists():
                candidate = str(app)
                break
    candidate = candidate or shutil.which('86Box') or shutil.which('86box')
    if not candidate:
        raise RuntimeError('86Box not found. Install 86Box 6.0+ and its ROMs, or pass --86box PATH.')
    path = Path(candidate).expanduser()
    if path.suffix == '.app':
        path /= 'Contents/MacOS/86Box'
    elif not path.is_file():
        path = Path(shutil.which(candidate) or candidate)
    path = path.resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise RuntimeError(f'86Box executable not found: {path}')
    info = path.parent.parent / 'Info.plist'
    if sys.platform == 'darwin' and info.exists():
        # macOS owns this metadata and its reader. Avoid loading Python's
        # optional expat extension just to check one installed-app version.
        version = subprocess.check_output(['/usr/libexec/PlistBuddy', '-c',
                                           'Print :CFBundleShortVersionString', str(info)],
                                          text=True).strip()
        if version and int(version.split('.')[0]) < 6:
            raise RuntimeError(f'86Box {version} is too old; this serial link requires 6.0+.')
    return path


def prepare():
    for number, mode in ((1, 1), (2, 2)):
        profile = PAIR / f'pc{number}'
        profile.mkdir(exist_ok=True)
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.read(ROOT / 'vm/xt640/86box.cfg')
        previous = configparser.ConfigParser(interpolation=None)
        previous.read(profile / '86box.cfg')
        cfg['General']['uuid'] = previous.get('General', 'uuid', fallback=str(uuid.uuid4()))
        cfg['General']['scale'] = '1'
        # Host metadata belongs to the host that will run this profile.
        cfg['General'].pop('host_cpu', None)
        for source, target in (('os8088-360.img', 'system.img'), (APP_IMAGE, APP_DISK)):
            shutil.copy2(BUILD / source, profile / target)
        cfg['Floppy and CD-ROM drives']['fdd_01_fn'] = str(profile / 'system.img')
        cfg['Floppy and CD-ROM drives']['fdd_02_fn'] = str(profile / APP_DISK)
        cfg['Ports (COM & LPT)'] = {
            'serial1_enabled': '1', 'serial1_device': 'none',
            'serial2_enabled': '1', 'serial2_device': 'pipe',
            'serial3_enabled': '0', 'serial4_enabled': '0',
        }
        cfg['Named Pipe (COM) #2'] = {
            'path': str(PAIR / 'nullmodem'), 'mode': str(mode), 'reconnect': '1',
        }
        with (profile / '86box.cfg').open('w') as stream:
            cfg.write(stream)
        print(f'Prepared {TITLE} PC {number}: {profile}', flush=True)


def main():
    global PAIR, TITLE, APP_IMAGE, APP_DISK
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', choices=('wireball', 'wirefleet'), default='wireball', help='game to put on the paired XTs')
    parser.add_argument('--86box', dest='box', metavar='PATH', help='86Box executable or macOS .app (also accepts BOX environment variable)')
    parser.add_argument('--roms', type=Path, metavar='DIR', help='override the installed 86Box ROM directory')
    parser.add_argument('--prepare-only', action='store_true', help='build disks and write configs without opening 86Box')
    parser.add_argument('--no-build', action='store_true', help='use existing build/os8088-360.img and wireball360.img')
    args = parser.parse_args()
    if args.game == 'wirefleet':
        TITLE, APP_IMAGE, APP_DISK = 'WireFleet', 'wireflet360.img', 'wireflet.img'
        PAIR = BUILD / '86box-wirefleet-pair'
    if sys.platform not in ('darwin', 'linux'):
        raise RuntimeError('This FIFO launcher supports macOS and Linux.')
    if args.roms and not args.roms.expanduser().is_dir():
        raise RuntimeError(f'ROM directory not found: {args.roms}')
    PAIR.mkdir(parents=True, exist_ok=True)
    with (PAIR / 'launcher.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        running = running_profiles()
        if running == [1, 2] and not args.prepare_only:
            print(f'Both {TITLE} PCs are already running; their disks were left untouched.')
            return
        if running:
            raise RuntimeError(f'Close both {TITLE} PCs before preparing or relaunching the pair; their disks are in use.')
        box = None if args.prepare_only else find_box(args.box)
        if not args.no_build:
            subprocess.run(['make', 'BUILD=build', 'build/os8088-360.img', 'build/'+APP_IMAGE], cwd=ROOT, check=True)
        for image in ('os8088-360.img', APP_IMAGE):
            if not (BUILD / image).is_file():
                raise RuntimeError(f'Missing build/{image}; run again without --no-build.')
        prepare()
        if args.prepare_only:
            return
        for number in (1, 2):
            profile = PAIR / f'pc{number}'
            command = [str(box), '-P', str(profile), '-V', f'{TITLE} PC {number}', '-N']
            if args.roms:
                command += ['-R', str(args.roms.expanduser().resolve())]
            if sys.platform == 'darwin' and box.parent.parent.parent.suffix == '.app':
                subprocess.run(['open', '-n', '-a', str(box.parent.parent.parent), '--args', *command[1:]], check=True)
            else:
                with (profile / 'host.log').open('w') as log:
                    subprocess.Popen(command, cwd=profile, stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=log, start_new_session=True)
        deadline = time.monotonic() + 8
        while running_profiles() != [1, 2]:
            if time.monotonic() >= deadline:
                raise RuntimeError('Both 86Box processes did not start. Check the installed ROMs and pc1/pc2 host.log (Linux).')
            time.sleep(0.1)
        if args.game == 'wirefleet':
            print('Open B: → WIREFLET on each PC; each finds the other on COM2 by itself (move the mouse once).\n'
                  'Place or auto-place (A), then Ready (Y). The lower-identity captain fires first.\n'
                  'T: chat, F: fullscreen, S/O: save/load. Link → Connect COM2 is the manual fallback.')
        else:
            print('Open B: → WIREBALL and choose Serial Match (S) on each PC.\n'
                  'COM2 is linked; click a VM to control its paddle with Up/Down or W/S.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f'wireball-pair: {error}')
