#!/usr/bin/env python3
"""rczex: the Z80 core's gate, run INSIDE os8088 (SPEC.md 71).

    make rczex                                   # boots build/runcpm.img
    python3 tests/rczex.py build/runcpm.img [--program ZEXALL] [--timeout S]
                                                 [--keep] [--socket PATH]

Boots the RUNCPM floppy in QEMU (`make test TESTAPPS=...`), opens Disk B by
the two-press double-click, launches RUNCPM the same way, TYPES `ZEXDOC` AT
THE `A>` PROMPT (wave 4: the DRI CCP loads it off A\0 through the disk
layer, SPEC.md 71.3; `--loader` uses the debug key instead - Alt+L, the
name, Enter - which is how waves 2 and 3 ran it), and then READS THE
TERMINAL OFF SCREENDUMPS - there is no serial
port into a package, and what the user sees is the glass - through
tests/rczex_ocr.py's harvested 8x8 glyphs, until ZEXDOC prints 'Tests
complete'. Every one of ZEXDOC's groups (its names are string constants in
the .COM, read from there so the list cannot drift) must read '  OK'; an
'ERROR' line, an unreadable result, or the timeout fails the gate. Part of
RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas / "Mockba the
Borg" (MIT licence, Copyright (c) 2017 Mockba the Borg): BANNER below is
what RunCPM/main.c prints, and ZEXDOC is Frank D. Cringle's exerciser off
RunCPM's master disk.

ZEXDOC ran ~144 s in the raw-QEMU harness (apps/runcpm/hosttest/rcz80test.sh:
the same shipping core, no OS around it) and 146 s here (2026-08-17, an
Apple-silicon host, after the review's slice fixes; 179 s before them); the
difference is the wake round trip and the terminal flush per slice, which the
adaptive slice length keeps small. On a loaded host (load ~2.3) the same
run took 175-211 s while the raw harness took 155 s and a control build with
the previous adaptation 178 s: the in-OS run is the more sensitive to
contention (the QMP screendump a second, the OS's own tasks), so a figure is
quoted with the raw harness's from the same hour beside it.
ZEXALL (undocumented flags too) is `--program ZEXALL`, on demand: it reports
differences in X/Y that this core does not compute (SPEC.md 71.4).

Coordinates are the 640x480 VGA desktop's: Disk B's icon at (600,100), and
RUNCPM.O88 the row `package_row` FINDS - rows are 16 px apart from y = 128,
and the Disk window lists the root sorted by its 8.3 name with the hidden
ASSOC.DAT and the volume label left out, so the row is read off the image's
own root directory rather than assumed. It used to be a constant, the fifth
row, and the games (SPEC.md 71.6) moved it: their drive folders - D, G, H,
N - sort in among the files, and a stale constant double-clicks a FOLDER,
which opens a window and hangs the gate on a missing banner. The banner rows
are then found by glyph consistency, not assumed
(rczex_ocr.Screen.find_origin).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import read_ppm            # noqa: E402  (tools/shot.py)
from rczex_ocr import Screen         # noqa: E402
import os88qemu                                              # noqa: E402

BANNER = {
    0: "  CP/M Emulator v6.9 by Marcelo Dantas",
    1: "      Built Jul 21 2026 - 20:43:19",
    2: "----------------------------------------",
    3: "CPU is 8086 native",
    6: "BIOS at 0xfe00 - BDOS at 0xec00",
    7: "BIOS/BDOS using interrupt handoff method",
    8: "CCP CCP-DR.60K at 0xe400",
}


def qmp(sock, *cmds):
    subprocess.run([sys.executable, "tools/qmp.py", sock, *cmds], check=True,
                   stdout=subprocess.DEVNULL)


def mouse_to(sock, x, y):
    subprocess.run([sys.executable, "tools/mouse.py", sock, "to", str(x), str(y)],
                   check=True, stdout=subprocess.DEVNULL)


def dclick(sock, x, y):
    mouse_to(sock, x, y)
    qmp(sock, "mouse_button 1", "sleep 0.08", "mouse_button 0", "sleep 0.10",
        "mouse_button 1", "sleep 0.08", "mouse_button 0")


def package_row(image, name="RUNCPM.O88"):
    """Which row of the Disk window `name` is on, off the image's own FAT12
    root directory: every entry but the volume label and the hidden ones,
    sorted by the 11-byte 8.3 name, which is the order the Disk window lists
    (checked against the glass on the 1.44MB disk)."""
    with open(image, "rb") as fh:
        boot = fh.read(36)
        bps = struct.unpack("<H", boot[11:13])[0]
        res = struct.unpack("<H", boot[14:16])[0]
        nfat, nroot = boot[16], struct.unpack("<H", boot[17:19])[0]
        spf = struct.unpack("<H", boot[22:24])[0]
        fh.seek((res + nfat * spf) * bps)
        root = fh.read(nroot * 32)
    names = []
    for i in range(nroot):
        e = root[i * 32:i * 32 + 32]
        if not e or e[0] in (0x00, 0xE5) or e[11] & 0x0A:   # free, hidden, label
            continue
        names.append(e[:11].decode("latin-1"))
    names.sort()
    want = name.split(".")[0].ljust(8) + name.split(".")[1].ljust(3)
    if want not in names:
        raise SystemExit(f"rczex: {name} is not in {image}'s root")
    return names.index(want)


def screen(sock, path):
    ppm = os.path.abspath(path)
    qmp(sock, f'screendump "{ppm}"')
    w, h, pix = read_ppm(ppm)
    return Screen(w, h, pix)


def sendkeys(sock, text):
    cmds = []
    for ch in text:
        key = {"-": "minus", ".": "dot", " ": "spc"}.get(ch, ch.lower())
        cmds += [f"sendkey {key}", "sleep 0.15"]
    qmp(sock, *cmds)


def group_names(com):
    """ZEXDOC's group names, in test order, out of the binary's strings."""
    data = open(com, "rb").read()
    names = [m.group(1).decode() for m in
             re.finditer(rb"([<a-z][<>a-z0-9 ,()+/]{2,27}\.\.+)\$", data)]
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", nargs="?", default="build/runcpm.img")
    ap.add_argument("--program", default="ZEXDOC")
    ap.add_argument("--timeout", type=int, default=2400)
    ap.add_argument("--keep", action="store_true", help="leave QEMU running")
    ap.add_argument("--socket", default="build/qmp.sock")
    ap.add_argument("--shots", default="build/port-shots")
    ap.add_argument("--loader", action="store_true",
                    help="load through Alt+L (the debug key) instead of typing "
                         "the name at the CCP's prompt")
    args = ap.parse_args()
    sock = args.socket
    os.makedirs(args.shots, exist_ok=True)
    com = os.path.join("build", "runcpm-disk", "A", "0", args.program + ".COM")
    if not os.path.exists(com):
        print(f"rczex: no {com} (make runcpm-src)")
        return 2
    groups = group_names(com)
    print(f"rczex: {args.program}: {len(groups)} groups")

    subprocess.run(["pkill", "-f", "qemu-system-i386"], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    time.sleep(1)
    if os.path.exists(sock):
        os.unlink(sock)
    log = open(os.path.join(args.shots, "rczex-qemu.log"), "w")
    # `make test` DAEMONISES the emulator, so this Popen returns and the
    # guest stays up - killing the Popen would not reach it (os88qemu).
    os88qemu.own()
    qemu = subprocess.Popen(["make", "test", f"TESTAPPS={args.image}"], stdout=log,
                            stderr=subprocess.STDOUT)
    t0 = time.time()
    while not os.path.exists(sock):
        time.sleep(0.5)
        if time.time() - t0 > 60:
            print("rczex: QEMU did not come up (build/port-shots/rczex-qemu.log)")
            return 1
    time.sleep(10)                                   # the desktop

    dclick(sock, 600, 100)                           # Disk B
    time.sleep(3)
    dclick(sock, 170, 128 + 16 * package_row(args.image))     # RUNCPM.O88
    time.sleep(4)
    mouse_to(sock, 620, 470)                         # off the terminal
    scr = screen(sock, os.path.join(args.shots, "rczex-0.ppm"))
    (x0, y0), score = scr.find_origin(0, BANNER[0])
    if score < 30:
        print(f"rczex: the banner is not on the glass (best grid {x0},{y0} scored {score})")
        return 1
    print(f"rczex: terminal origin ({x0},{y0}); banner row 0 consistency {score}")
    for r, text in BANNER.items():                   # the glyphs, once, while
        scr.learn(r, text)                           # the banner is whole
    glyphs = scr.glyphs

    if args.loader:
        qmp(sock, "sendkey alt-l", "sleep 0.5")
    else:
        # the CCP is at its prompt (the banner's row 9 is 'A>'); the DRI
        # CCP opens NAME.COM through F_OPEN/F_READ, ~24 records a second
        # of the wake's slices, then jumps to 0100h
        pass
    sendkeys(sock, args.program)
    qmp(sock, "sendkey ret")
    t_start = time.time()

    seen, done, fails, learned_groups = [], False, 0, 0
    last_new = time.time()
    while time.time() - t_start < args.timeout:
        time.sleep(1)                                # the fastest groups finish in well under a second
        scr = screen(sock, os.path.join(args.shots, "rczex-poll.ppm"))
        scr.x0, scr.y0 = x0, y0
        scr.glyphs = glyphs                          # what is known so far...
        # ...plus every group name, learned in the order they appear
        rows = [scr.read(r, 79) for r in range(25)]
        for r in range(25):
            t = rows[r]
            if learned_groups < len(groups):
                g = groups[learned_groups]
                if len(t) >= len(g) and all(a == b or a == "?" for a, b in zip(t, g)) \
                        and t[len(g):len(g) + 2] == "  ":
                    scr.learn(r, g)
                    learned_groups += 1
        rows = [scr.read(r, 79) for r in range(25)]
        new = 0
        for t in rows:
            if not t.strip():
                continue
            if t in seen:
                continue
            if "  OK" in t or "ERROR" in t or "ests complete" in t or "not found" in t:
                seen.append(t)
                new += 1
                print(f"  {int(time.time() - t_start):4d}s  {t}")
                if "ERROR" in t or "not found" in t:
                    fails += 1
                if "ests complete" in t:      # (T is not a banner glyph)
                    done = True
        if new:
            last_new = time.time()
        if done:
            break
        if time.time() - last_new > 900:
            print("rczex: no new result line for 15 minutes")
            break
    elapsed = int(time.time() - t_start)
    subprocess.run([sys.executable, "tools/shot.py", sock,
                    os.path.join(args.shots, f"rczex-{args.program.lower()}-end.png"),
                    "--crop", "0,38,640,200"], check=False, stdout=subprocess.DEVNULL)
    if not args.keep:
        qmp(sock, "quit")
    qemu.wait(timeout=30)
    oks = sum(1 for t in seen if "  OK" in t)
    print(f"rczex: {oks} OK, {fails} failing, {elapsed}s"
          + ("" if done else " - did not finish"))
    if done and fails == 0 and oks == len(groups):
        print("rczex: PASS")
        return 0
    print(f"rczex: FAIL ({oks} of {len(groups)} groups OK)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
