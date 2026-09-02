#!/usr/bin/env python3
"""os88burn: put the live media on real media, on a Mac (SPEC.md 80.4).

    python3 tools/os88burn.py            # the interactive guide
    python3 tools/os88burn.py --scan     # list drives and burners, change
                                         # nothing, and exit
    make burn                            # the same guide

An interactive text-mode guide for the two things a reader does with
SPEC.md 80's images and cannot do with a copy command: write
`os8088-usb.img` to a USB flash drive, and burn `os8088.iso` to a CD. It
exists because the raw alternative is `dd` against a device node, and a
mistyped device node is somebody's backup drive - so this shows what is
attached, refuses everything that is not a USB flash drive, and makes the
one destructive step a deliberate act.

WHAT THE USB HALF WILL AND WILL NOT OFFER. A drive is listed only if
diskutil says all three of: the bus is USB, the device is external, and it
is not the disk macOS is running from. Internal disks, Thunderbolt/SATA
enclosures and the boot disk are not shown at all rather than shown and
guarded - a list that cannot name the wrong disk beats a warning about it.
The erase is confirmed by TYPING THE DISK'S IDENTIFIER back, not by
pressing y: y is muscle memory, and this step is the one place muscle
memory kills.

HOW THE WRITE RUNS. The wizard itself stays unprivileged - listing,
choosing and confirming need no rights, so they take none. Only the write
is escalated: it re-invokes this same file under `sudo` in a hidden
`--_write` mode that opens the RAW device (`/dev/rdiskN` - the buffered
node is several times slower), streams the image in 1MB chunks with a
progress line, and then READS THE WHOLE IMAGE BACK and compares SHA-256s -
a write that errored is loud, but a stick that silently drops bytes (fake
capacity flash) is not, and the read-back is the only thing that catches
it. The stick is ejected afterwards, ready to pull.

THE CD HALF drives Apple's own machinery: `drutil list` says whether a
burner is attached at all (none attached: the option says so and does
nothing, SPEC.md 47's shape), and `hdiutil burn` does the burn and its own
verify pass. It needs no privileges. A failed burn - no blank disc, disc
too small - offers the retry after the fix rather than starting over.

macOS ONLY, and it says so elsewhere: everything here is diskutil,
drutil and hdiutil. On Linux the same job is `lsblk` + `dd` +
`wodim`, and a guide that pretended to cover both would test as neither.

The images come out of `make live` (or an unpacked release zip - the
guide takes a path). Nothing here builds anything.
"""
import argparse
import hashlib
import os
import plistlib
import subprocess
import sys

CHUNK = 1 << 20                       # 1MB writes: big enough to stream a
                                      # USB2 stick at bus speed, small
                                      # enough for a live progress line
MBR_SIG = b"\x55\xaa"


def fail(msg):
    sys.stderr.write("os88burn: error: %s\n" % msg)
    sys.exit(1)


def run(args):
    """One external command, captured; None if it would not run at all."""
    try:
        return subprocess.run(args, capture_output=True, text=True)
    except FileNotFoundError:
        return None


def mb(n):
    return "%.1fMB" % (n / 1e6) if n < 1e9 else "%.1fGB" % (n / 1e9)


# -----------------------------------------------------------------------------
# what is attached
# -----------------------------------------------------------------------------

def boot_whole_disk():
    """The whole disk '/' lives on - the one disk this tool must never
    list, even though it CAN be external: a Mac booted from a USB drive
    reports that drive as USB and external, and it is still the last
    thing anybody wants erased."""
    r = run(["diskutil", "info", "-plist", "/"])
    if not r or r.returncode != 0:
        return None
    return plistlib.loads(r.stdout.encode()).get("ParentWholeDisk")


def usb_drives():
    """Every attached USB flash drive, as dicts. The three-way filter is
    the module docstring's contract: USB bus, external, not the boot
    disk. Anything that fails it is not shown at all."""
    r = run(["diskutil", "list", "-plist", "external", "physical"])
    if not r or r.returncode != 0:
        fail("diskutil would not list disks - is this a Mac?")
    wholes = plistlib.loads(r.stdout.encode()).get("WholeDisks", [])
    boot = boot_whole_disk()
    out = []
    for dev in wholes:
        r = run(["diskutil", "info", "-plist", dev])
        if not r or r.returncode != 0:
            continue
        info = plistlib.loads(r.stdout.encode())
        if info.get("BusProtocol") != "USB":
            continue
        if info.get("Internal"):
            continue
        if dev == boot:
            continue
        vr = run(["diskutil", "list", "-plist", dev])
        vols = []
        if vr and vr.returncode == 0:
            for d in plistlib.loads(vr.stdout.encode()) \
                    .get("AllDisksAndPartitions", []):
                for p in d.get("Partitions", []):
                    if p.get("VolumeName"):
                        vols.append(p["VolumeName"])
        out.append({"dev": dev,
                    "size": info.get("TotalSize", 0),
                    "name": (info.get("MediaName") or "").strip(),
                    "vols": vols})
    return out


def burners():
    """The attached CD/DVD burners, as name strings. `drutil list` prints
    its column header whether or not a device follows it, so the header
    line is skipped by shape rather than counted on."""
    r = run(["drutil", "list"])
    if not r or r.returncode != 0:
        return []
    out = []
    for line in r.stdout.splitlines():
        s = line.strip()
        if not s or s.startswith("Vendor"):
            continue
        out.append(" ".join(s.split()))
    return out


# -----------------------------------------------------------------------------
# what is being written
# -----------------------------------------------------------------------------

def check_usb_image(path):
    """The same refusal os88iso.py makes (SPEC.md 80.2): a USB image is a
    partitioned hard-disk image with an active type-04h entry, and
    anything else written raw to a stick is a stick that does not boot -
    better refused here, where the message can say why."""
    with open(path, "rb") as f:
        head = f.read(512)
    if len(head) < 512 or head[510:512] != MBR_SIG:
        fail("%s has no MBR signature - the live USB image is "
             "os8088-usb.img, from `make live` (SPEC.md 80.1)" % path)
    for i in range(4):
        e = head[446 + i * 16:446 + i * 16 + 16]
        if e and e[0] == 0x80 and e[4] == 0x04:
            return
    fail("%s has no active FAT16 partition entry - not the live USB "
         "image" % path)


def check_iso(path):
    with open(path, "rb") as f:
        f.seek(0x8000)
        vd = f.read(6)
    if vd[1:6] != b"CD001":
        fail("%s is not an ISO9660 image - the live CD is os8088.iso, "
             "from `make live` (SPEC.md 80.2)" % path)


def find_image(given, default_name, checker, what):
    """Resolve the image to write: --image/--iso, the build/ default, or
    a path the user types (an unpacked release zip has these same
    files)."""
    if given:
        if not os.path.isfile(given):
            fail("%s: no such file" % given)
        checker(given)
        return given
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cand = os.path.join(root, "build", default_name)
    if os.path.isfile(cand):
        checker(cand)
        return cand
    print()
    print("  build/%s is not built. `make live` builds it (it needs the" %
          default_name)
    print("  C toolchain and `make runcpm-src` first - SPEC.md 80), or")
    print("  point at one from an unpacked release zip.")
    while True:
        p = ask("  path to %s (or q to go back): " % what)
        if p in ("q", ""):
            return None
        p = os.path.expanduser(p)
        if not os.path.isfile(p):
            print("  %s: no such file" % p)
            continue
        checker(p)
        return p


# -----------------------------------------------------------------------------
# the two halves
# -----------------------------------------------------------------------------

def ask(prompt):
    try:
        return input(prompt).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        print("Nothing was written.")
        sys.exit(0)


def usb_flow(image):
    image = find_image(image, "os8088-usb.img", check_usb_image,
                       "the live USB image")
    if not image:
        return
    size = os.path.getsize(image)
    print()
    print("  Image: %s (%s)" % (image, mb(size)))

    while True:
        drives = usb_drives()
        print()
        if not drives:
            print("  No USB flash drives are attached.")
            print("  Plug one in, then r to rescan, or q to go back.")
        else:
            print("  USB flash drives attached now - CHOOSING ONE ERASES "
                  "IT COMPLETELY:")
            print()
            for i, d in enumerate(drives, 1):
                vols = ", ".join(d["vols"]) or "no mounted volumes"
                print("    %d) %-8s %8s  %s  (%s)"
                      % (i, d["dev"], mb(d["size"]), d["name"] or "?", vols))
            print()
            print("  Number to choose, r to rescan, q to go back.")
        c = ask("  > ").lower()
        if c == "q":
            return
        if c == "r" or not c:
            continue
        if not c.isdigit() or not 1 <= int(c) <= len(drives):
            print("  ? %r" % c)
            continue
        d = drives[int(c) - 1]
        if d["size"] < size:
            print("  %s is %s - smaller than the image. Pick another."
                  % (d["dev"], mb(d["size"])))
            continue
        break

    print()
    print("  About to ERASE %s: %s, %s (%s)."
          % (d["dev"], d["name"] or "?", mb(d["size"]),
             ", ".join(d["vols"]) or "no mounted volumes"))
    print("  Everything on it is destroyed. This cannot be undone.")
    print()
    got = ask("  Type the disk's identifier (%s) to confirm, anything "
              "else aborts: " % d["dev"])
    if got != d["dev"]:
        print("  Not confirmed. Nothing was written.")
        return

    r = run(["diskutil", "unmountDisk", "/dev/" + d["dev"]])
    if not r or r.returncode != 0:
        fail("could not unmount %s: %s"
             % (d["dev"], (r.stderr or r.stdout).strip() if r else "?"))
    print("  Unmounted %s." % d["dev"])
    print()
    print("  Writing needs administrator rights for the raw device; sudo")
    print("  may ask for your password now.")
    rc = subprocess.call(["sudo", sys.executable,
                          os.path.abspath(__file__),
                          "--_write", image, "/dev/r" + d["dev"]])
    if rc != 0:
        fail("the write did not complete; the stick's contents are "
             "undefined - rerun this tool to try again")
    run(["diskutil", "eject", "/dev/" + d["dev"]])
    print()
    print("  Done. %s is ejected - pull it out, plug it into the target"
          % d["dev"])
    print("  machine, and boot it in legacy BIOS mode (SPEC.md 80.1).")


def write_and_verify(image, dev):
    """The privileged half, running under sudo: stream, fsync, read the
    whole image length back off the device, compare digests."""
    size = os.path.getsize(image)
    want = hashlib.sha256()
    try:
        out = os.open(dev, os.O_WRONLY)
    except OSError as e:
        fail("cannot open %s: %s" % (dev, e))
    done = 0
    with open(image, "rb") as src:
        while True:
            chunk = src.read(CHUNK)
            if not chunk:
                break
            want.update(chunk)
            while chunk:                     # a raw device may take less
                try:                         # than asked; an error mid-way
                    n = os.write(out, chunk)  # deserves a sentence, not a
                except OSError as e:         # traceback
                    print()
                    fail("write failed %s in: %s - the stick's contents "
                         "are undefined" % (mb(done), e))
                chunk = chunk[n:]
                done += n
            sys.stdout.write("\r  writing  %s / %s (%d%%)"
                             % (mb(done), mb(size), done * 100 // size))
            sys.stdout.flush()
    os.fsync(out)
    os.close(out)
    print()
    got = hashlib.sha256()
    done = 0
    with open(dev, "rb", buffering=0) as back:
        while done < size:
            chunk = back.read(min(CHUNK, size - done))
            if not chunk:
                break
            got.update(chunk)
            done += len(chunk)
            sys.stdout.write("\r  verifying %s / %s (%d%%)"
                             % (mb(done), mb(size), done * 100 // size))
            sys.stdout.flush()
    print()
    if done < size or got.digest() != want.digest():
        fail("READ-BACK MISMATCH: the stick did not keep what was "
             "written. Suspect the stick (fake-capacity flash fails "
             "exactly this way) before suspecting the image.")
    print("  Verified: %d bytes, SHA-256 match." % size)
    return 0


def cd_flow(iso):
    devs = burners()
    if not devs:
        print()
        print("  No CD/DVD burner is attached to this Mac, so there is")
        print("  nothing to burn with. Attach one and rerun.")
        return
    iso = find_image(iso, "os8088.iso", check_iso, "the live CD image")
    if not iso:
        return
    print()
    print("  Burner: %s" % devs[0])
    print("  Image:  %s (%s)" % (iso, mb(os.path.getsize(iso))))
    print()
    print("  Insert a blank CD-R (or CD-RW) in the burner.")
    while True:
        if ask("  Enter to burn, q to go back: ").lower() == "q":
            return
        print()
        # hdiutil does the burn and its own verify pass; its output is
        # the progress display, so it keeps the terminal.
        rc = subprocess.call(["hdiutil", "burn", iso])
        if rc == 0:
            print()
            print("  Done. The disc boots a legacy-BIOS machine set to")
            print("  boot from CD (SPEC.md 80.2); being a CD it is")
            print("  read-only - settings last until power-off (80.3).")
            return
        print()
        print("  The burn did not complete (no blank disc, or the disc")
        print("  was refused). Fix that and try again, or q to go back.")


# -----------------------------------------------------------------------------

def scan():
    drives = usb_drives()
    print("USB flash drives:")
    if not drives:
        print("  (none attached)")
    for d in drives:
        vols = ", ".join(d["vols"]) or "no mounted volumes"
        print("  %-8s %8s  %s  (%s)"
              % (d["dev"], mb(d["size"]), d["name"] or "?", vols))
    devs = burners()
    print("CD/DVD burners:")
    if not devs:
        print("  (none attached)")
    for b in devs:
        print("  %s" % b)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true",
                    help="list attached USB flash drives and burners, "
                         "change nothing, exit")
    ap.add_argument("--image", metavar="IMG",
                    help="the live USB image (default: build/os8088-usb.img)")
    ap.add_argument("--iso", metavar="ISO",
                    help="the live CD image (default: build/os8088.iso)")
    ap.add_argument("--_write", nargs=2, metavar=("IMG", "DEV"),
                    help=argparse.SUPPRESS)      # the sudo half, internal
    a = ap.parse_args()

    if a._write:
        return write_and_verify(*a._write)
    if sys.platform != "darwin":
        fail("this is the macOS guide (diskutil/drutil/hdiutil); on "
             "Linux use lsblk to find the stick and dd to write it")
    if a.scan:
        return scan()

    print()
    print("os8088 live media writer (SPEC.md 80)")
    while True:
        nburn = len(burners())
        print()
        print("  1) Write the live USB image to a flash drive")
        if nburn:
            print("  2) Burn the live CD")
        else:
            print("  2) Burn the live CD - no burner attached, so this "
                  "will only say so")
        print("  q) Quit")
        c = ask("  > ").lower()
        if c == "1":
            usb_flow(a.image)
        elif c == "2":
            cd_flow(a.iso)
        elif c in ("q", ""):
            print("Nothing more to do.")
            return 0
        else:
            print("  ? %r" % c)


if __name__ == "__main__":
    sys.exit(main())
