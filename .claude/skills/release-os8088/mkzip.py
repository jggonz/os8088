#!/usr/bin/env python3
"""Pack a release into ONE zip.

A release used to attach eleven loose `.img` files to the GitHub release, and
a reader had to know which two of them they wanted before they could download
anything. This builds a single `os8088-<version>.zip` instead: every image the
build produced, a README that says which ones to use, and a SHA256SUMS file
covering all of them.

TWO THINGS THIS DELIBERATELY DOES NOT DO.

It does not glob `build/*.img`. The manifest below is an ALLOWLIST, and it is
one because `build/` also holds disks that must never leave this machine --
`zork*.img` carries Infocom story files that `tools/getstories.py` fetched and
nobody here has the right to redistribute, and every `tests/` gate writes its
scratch image into the same directory. A glob ships those the first time
somebody runs an unrelated target before cutting a release, and the mistake is
invisible in the zip listing until it is public.

It does not build anything. Run `make` (and any on-demand target you want in
the zip) first; a missing REQUIRED image stops the script, and a missing
optional one is reported and skipped. That split is the point -- the seven
images `make` produces are the release, and `apps-all`/`word`/`cword`/
`runcpm` and the live media (`make live`, SPEC.md 80) are on-demand targets
that a tree without the C toolchain cannot build at all.

The zip is byte-for-byte reproducible: entries are written in manifest order,
every timestamp is the date in the version string, and permissions are fixed.
The images are already deterministic (tools/os88disk.py pins the volume serial
and every FAT timestamp), so the same version built from the same commit
produces the same zip, and a reader can check that.
"""
import argparse
import hashlib
import os
import re
import sys
import zipfile

# (filename, required, description). ORDER IS THE ZIP'S ORDER and the README's.
# Required = built by a bare `make`. Optional = its own on-demand target.
MANIFEST = [
    ("os8088.img",     True,  "System disk, 1.44MB. Boots the OS. Goes in drive A."),
    ("apps.img",       True,  "Software disk, 1.44MB. Goes in drive B."),
    ("os8088-720.img", True,  "System disk, 720KB."),
    ("apps720.img",    True,  "Software disk, 720KB."),
    ("os8088-360.img", True,  "System disk, 360KB. This is the one a real IBM PC/XT reads."),
    ("apps360.img",    True,  "Software disk, 360KB."),
    ("media360.img",   True,  "Music disk, 360KB. The music module does not fit beside the "
                              "programs at this size, so it rides a disk of its own. "
                              "There is no 1.44MB or 720KB version -- at those sizes it is "
                              "already on the software disk."),
    ("os8088-usb.img", False, "Live USB image. The whole system and every program on one "
                              "bootable hard-disk image. Write it raw to a USB stick and "
                              "boot a PC from it in legacy BIOS mode -- no floppy drive "
                              "needed. The LIVE USB AND LIVE CD section says how."),
    ("os8088.iso",     False, "Live CD. The same system as a bootable CD image, for "
                              "burning to a disc or booting in an emulator. A CD is "
                              "read-only, so settings and saved files last until "
                              "power-off; the USB image keeps them."),
    ("apps-all.img",   False, "Every program on one 1.44MB software disk, including both "
                              "word processors, the story reader, the CP/M emulator and "
                              "the Commodore 64. Use this instead of apps.img if you "
                              "would rather swap one disk than five."),
    ("word.img",       False, "Word processor disk, 1.44MB."),
    ("word720.img",    False, "Word processor disk, 720KB."),
    ("word360.img",    False, "Word processor disk, 360KB."),
    ("cword.img",      False, "Word processor disk built by the C compiler, 1.44MB."),
    ("cword720.img",   False, "Word processor disk built by the C compiler, 720KB."),
    ("cword360.img",   False, "Word processor disk built by the C compiler, 360KB."),
    ("runcpm.img",     False, "CP/M emulator disk, 1.44MB. Carries the emulator and the "
                              "CP/M programs that run under it."),
    ("runcpm720.img",  False, "CP/M emulator disk, 720KB. Carries the emulator and the "
                              "arcade games only."),
    ("runcpm360.img",  False, "CP/M emulator disk, 360KB. Carries the emulator and no "
                              "programs -- there is no room for them at this size."),
    ("c64.img",        False, "Commodore 64 disk, 1.44MB. Carries the emulator, the three "
                              "Commodore ROM images it reads at launch, and COPYING."),
    ("c64720.img",     False, "Commodore 64 disk, 720KB."),
    ("c64360.img",     False, "Commodore 64 disk, 360KB."),
]

# Files packed beside the images, when the image they belong to is here.
# THE C64 IS GPL-2-OR-LATER because VICE is, and C64-SPEC 1.2 says in
# as many words that "the release zip carries COPYING". Its own floppies carry
# it too -- this is the second copy, for a reader who unpacks the zip and never
# mounts a disk. (trigger image, source path, name in the zip, description)
EXTRAS = [
    ("c64.img", "apps/c64/COPYING", "COPYING.C64",
     "The GNU General Public License version 2, which is the licence the "
     "Commodore 64 emulator is under. It applies to that one program and to "
     "nothing else here."),
]

README = """\
os8088 {version}
{rule}

os8088 is a graphical operating system for the Intel 8086, written in assembly
and booted from a floppy disk. It has overlapping windows, a menu bar, a mouse
and programs you can run several copies of at once.

WHICH FILES YOU NEED
--------------------

You need two images: a system disk for drive A and a software disk for drive B.
Pick the pair that matches the disk size your machine or emulator uses.

  1.44MB   os8088.img       and  apps.img
  720KB    os8088-720.img   and  apps720.img
  360KB    os8088-360.img   and  apps360.img  (and media360.img, see below)

If you are not sure, use the 1.44MB pair. A real IBM PC/XT of the period reads
360KB disks and nothing larger.

RUNNING IT
----------

With QEMU, from the directory you unpacked this into:

  qemu-system-i386 \\
    -drive file=os8088.img,format=raw,if=floppy -boot a \\
    -drive file=apps.img,format=raw,if=floppy,index=1 \\
    -chardev msmouse,id=m0 -serial chardev:m0

The system disk goes in the first floppy drive and the software disk in the
second. The last line is the mouse: os8088 drives a serial mouse, so the
emulator has to put one on a serial port, and that is QEMU's way of doing it.
Other emulators have their own; the project's site has the current recipe and
the 86Box machine files.
{live}
THE FILES
---------

{files}

CHECKING WHAT YOU GOT
---------------------

SHA256SUMS lists a checksum for every file here. On macOS or Linux:

  shasum -a 256 -c SHA256SUMS

The build is deterministic, so anyone who builds this version from source gets
these same bytes.

Built from commit {commit}.

Licence: MIT, with one exception. The LICENSE file in the source repository
has the terms. The Commodore 64 emulator on c64*.img is under the GNU General
Public License version 2 or later, because it is a port of VICE, which is; that
licence is COPYING.C64 here when those disks are in this zip, and on the disks
themselves. Nothing else in os8088 is affected by it.
"""


# The live-media section of the README, present only when the zip carries
# the images it describes -- a README that explains a file the reader does
# not have reads as a packing mistake.
LIVE = """\

LIVE USB AND LIVE CD
--------------------

os8088-usb.img and os8088.iso carry the whole system and every program with
no floppies at all. os8088-usb.img is a raw hard-disk image: write it to a
USB stick -- everything already on the stick is erased -- and boot a PC from
it in legacy BIOS mode. On macOS or Linux, with the stick at /dev/sdX and
unmounted:

  dd if=os8088-usb.img of=/dev/sdX bs=1M

On a Mac with the os8088 source checkout, `make burn` does this with an
interactive guide instead -- it lists the attached USB flash drives, has
you confirm the one to erase by typing its name, and verifies the write.
It burns the CD too, when a burner is attached.

os8088.iso is the same system as a CD image, for burning to a disc or for an
emulator. In QEMU, one or the other:

  qemu-system-i386 -drive file=os8088-usb.img,format=raw -boot c \\
    -chardev msmouse,id=m0 -serial chardev:m0

  qemu-system-i386 -cdrom os8088.iso -boot d \\
    -chardev msmouse,id=m0 -serial chardev:m0

Booted this way the system runs from a hard-disk partition that appears as
drive C, with every program on it. The CD is read-only, so settings and
saved files last until power-off; the USB stick keeps them.
"""


def version_date(version):
    """A fixed timestamp, so the same version packs to the same bytes.

    The zip format cannot store a date before 1980, so a version string that
    carries no date falls back to the epoch the format itself starts at.
    """
    m = re.search(r"(\d{4})(\d{2})(\d{2})", version)
    if m:
        y, mo, d = (int(x) for x in m.groups())
        if 1980 <= y <= 2107 and 1 <= mo <= 12 and 1 <= d <= 31:
            return (y, mo, d, 12, 0, 0)
    return (1980, 1, 1, 0, 0, 0)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def add(zf, name, data, when):
    """One entry, with everything the OS would otherwise vary pinned flat."""
    info = zipfile.ZipInfo(name, date_time=when)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    info.create_system = 3          # unix, whatever host packed it
    zf.writestr(info, data)


def main():
    ap = argparse.ArgumentParser(description="Pack one release zip.")
    ap.add_argument("--version", required=True, help="e.g. v1.0.20260819")
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out", help="default: <build-dir>/os8088-<version>.zip")
    ap.add_argument("--commit", default="", help="the OS commit it was built from")
    args = ap.parse_args()

    bd = args.build_dir
    out = args.out or os.path.join(bd, "os8088-%s.zip" % args.version)
    root = "os8088-%s" % args.version
    when = version_date(args.version)

    commit = args.commit
    if not commit:
        import subprocess
        try:
            commit = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], text=True).strip()
        except Exception:
            commit = "unknown"

    picked, missing, skipped = [], [], []
    for name, required, desc in MANIFEST:
        p = os.path.join(bd, name)
        if os.path.isfile(p):
            picked.append((name, p, desc))
        elif required:
            missing.append(name)
        else:
            skipped.append(name)

    # An extra rides only when the image it belongs to did. A licence for a
    # program that is not in the zip is noise; a program in the zip without
    # its licence is the thing this list exists to stop.
    have = {name for name, _, _ in picked}
    for trigger, src, as_name, desc in EXTRAS:
        if trigger in have:
            if not os.path.isfile(src):
                print("mkzip: %s is in the zip and %s is missing -- refusing to "
                      "ship it without its licence." % (trigger, src), file=sys.stderr)
                return 1
            picked.append((as_name, src, desc))

    if missing:
        print("mkzip: these are built by `make` and are not in %s:" % bd,
              file=sys.stderr)
        for m in missing:
            print("  %s" % m, file=sys.stderr)
        print("mkzip: run `make` and try again -- refusing to pack a partial "
              "release.", file=sys.stderr)
        return 1

    sums = ["%s  %s" % (sha256(p), name) for name, p, _ in picked]
    files = "\n\n".join(
        "  %s\n      %s" % (name, "\n      ".join(_wrap(desc, 66)))
        for name, _, desc in picked)
    live = LIVE if "os8088-usb.img" in have and "os8088.iso" in have else ""
    readme = README.format(version=args.version, rule="=" * (7 + len(args.version)),
                           files=files, commit=commit, live=live)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        add(zf, "%s/README.txt" % root, readme.encode(), when)
        add(zf, "%s/SHA256SUMS" % root, ("\n".join(sums) + "\n").encode(), when)
        for name, p, _ in picked:
            with open(p, "rb") as f:
                add(zf, "%s/%s" % (root, name), f.read(), when)

    # Read it back rather than trusting the write: a truncated member is a
    # perfectly valid-looking zip until somebody unpacks it.
    with zipfile.ZipFile(out) as zf:
        bad = zf.testzip()
        if bad:
            print("mkzip: %s is corrupt in %s" % (bad, out), file=sys.stderr)
            return 1
        n = len(zf.namelist())

    raw = sum(os.path.getsize(p) for _, p, _ in picked)
    imgs = sum(1 for name, _, _ in picked if name.endswith(".img"))
    print("mkzip: %s" % out)
    print("  %d entries, %d images, %.1fMB packed from %.1fMB"
          % (n, imgs, os.path.getsize(out) / 1e6, raw / 1e6))
    print("  sha256 %s" % sha256(out))
    if skipped:
        print("  on-demand disks NOT built, so not in the zip: %s"
              % ", ".join(skipped))
    return 0


def _wrap(s, w):
    out, line = [], ""
    for word in s.split():
        if line and len(line) + 1 + len(word) > w:
            out.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    if line:
        out.append(line)
    return out


if __name__ == "__main__":
    sys.exit(main())
