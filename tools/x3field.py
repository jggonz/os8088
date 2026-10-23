#!/usr/bin/env python3
"""x3field - XTMP3's selection matrix on 86Box (docs/xtmp3/FIELD.md).

    make xtmp3field && python3 tools/x3field.py [--machine xt|286|386|486|pentium ...]
                                                [--fix c128|aftr ...] [--json FILE]
                                                [--limit SECS] [--shots DIR]

86Box has no debug socket and this host cannot send it a key, so nothing here
drives the machine. Each run copies vm/<machine>-xtmp3 into a scratch
directory under build/x3field/ with FRESH copies of `make xtmp3field`'s two
floppies (the system disk, whose \\AUTOSTRT opens B:\\XTMP3\\TRACK.MP3, and that
B:) and launches it. XTMP3 - the -DX3REPORT build, apps/xtmp3/x3rep.inc -
opens on the track by itself, calibrates, and writes
SYSTEM\\APPDATA\\X3REPORT.TXT on B: when the ladder has decided (EVENT D, the
decode-first dialog) or when the stream has played out (EVENT E). This reads
the report off the scratch B: image, takes the emulator's window with
`screencapture -l` while it plays, and kills the emulator (by its own pid:
SIGTERM does not stop 86Box, so SIGKILL).

The machines' NVRAM: an AT BIOS whose CMOS is empty stops at "CMOS system
options not set - Press <F1>", which only a person at the keyboard can clear
(the 486's did, on its first run here). Each run takes a CMOS image of the
same BOARD that passes its standard checksum from any checkout's vm/*/nvr
(nvr_for), sets its floppy types to the profiles' two 1.44MB drives and
re-sums it. A board nobody has ever set up has none, and its run says so.

Timing is EMULATED time: every figure in a report is the guest's own PIT,
which 86Box advances with its CPU model, so a slow host makes the run longer
and leaves the figures alone. How credible each CPU model is, is
docs/xtmp3/FIELD.md's section 1.
"""
import argparse
import glob
import json
import os
import shutil
import signal
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOX = "/Applications/86Box.app/Contents/MacOS/86Box"
MACHINES = {"xt": "xt-sound", "286": "286-sound", "386": "386-sound",
            "486": "486", "pentium": "pentium"}
FIXES = {"c128": "C128.MP3 (MPEG-1 44.1 kHz L/R stereo, 128 kbps, 1.5 s)",
         "aftr": "AFTERHRS.MP3 (MPEG-2.5 11,025 Hz mono, 24 kbps, 10.0 s)"}
QNAME = ["AUTO", "ORIGINAL", "HALF", "QUARTER", "XT STANDARD", "XT FAST",
         "XT TURBO", "XT SPEECH"]
CPU = {0: "8086", 1: "286", 2: "386"}
PIT = 0.8380965e-6                  # seconds a PIT count (1,193,182 Hz)

WINID_SWIFT = r'''
import CoreGraphics
import Foundation
// "<windowid> <pid> <layer> <title>" for every window owned by 86Box
let opts = CGWindowListOption(arrayLiteral: .optionAll)
if let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] {
    for w in list {
        let owner = w[kCGWindowOwnerName as String] as? String ?? ""
        if owner.contains("86Box") {
            let id = w[kCGWindowNumber as String] as? Int ?? 0
            let pid = w[kCGWindowOwnerPID as String] as? Int ?? 0
            let name = w[kCGWindowName as String] as? String ?? ""
            let layer = w[kCGWindowLayer as String] as? Int ?? 0
            print("\(id) \(pid) \(layer) \(name)")
        }
    }
}
'''


def winid_tool():
    d = os.path.join(ROOT, "build", "x3field")
    exe = os.path.join(d, "winid")
    if not os.path.exists(exe):
        os.makedirs(d, exist_ok=True)
        src = os.path.join(d, "winid.swift")
        open(src, "w").write(WINID_SWIFT)
        subprocess.run(["swiftc", "-O", "-o", exe, src], check=True,
                       capture_output=True)
    return exe


def windows(pid):
    """our emulator's main window: the one whose title names 86Box (its
    untitled siblings capture as a white square)"""
    out = subprocess.run([winid_tool()], capture_output=True, text=True).stdout
    ids = []
    for line in out.splitlines():
        f = line.split(None, 3)
        if len(f) >= 4 and int(f[1]) == pid and int(f[2]) == 0 and \
                "86Box" in f[3]:
            ids.append((int(f[0]), f[3]))
    return ids


def capture(pid, path):
    for wid, title in windows(pid):
        r = subprocess.run(["screencapture", "-x", "-o", "-l", str(wid), path],
                           capture_output=True)
        if r.returncode == 0 and os.path.exists(path) and \
                os.path.getsize(path) > 20000:
            return True
    return False


def nvr_for(machine, cfg):
    """an NVRAM this machine's BIOS accepts: its CMOS standard checksum (the
    sum of bytes 10h..2Dh at 2Eh, high byte first) right and its floppy
    types set. Sought in vm/<machine>-xtmp3/nvr, then in every checkout's
    vm/*/nvr beside this one (an AT CMOS is per BOARD, and a machine nobody
    has set up stops in setup), and copied with byte 10h made two 1.44MB
    drives (44h) - the drives these profiles have - and the checksum
    re-summed. None for the XT, which has no CMOS"""
    if machine == "xt":
        return None, None
    board = next(l.split("=", 1)[1].strip() for l in cfg
                 if l.startswith("machine ="))
    cands = sorted(glob.glob(os.path.join(ROOT, "vm", "%s-xtmp3" % machine,
                                          "nvr", board + ".nvr")))
    cands += sorted(glob.glob(os.path.join(os.path.dirname(ROOT), "os8088*",
                                           "vm", "*", "nvr", board + ".nvr")))
    for c in cands:
        b = bytearray(open(c, "rb").read())
        if len(b) >= 0x40 and sum(b[0x10:0x2E]) == (b[0x2E] << 8 | b[0x2F]) \
                and b[0x10] != 0xFF:
            b[0x10] = 0x44
            cs = sum(b[0x10:0x2E])
            b[0x2E], b[0x2F] = cs >> 8, cs & 0xFF
            return c, (board, bytes(b))
    return None, None


def report(img):
    r = subprocess.run(["mtype", "-i", img, "::SYSTEM/APPDATA/X3REPORT.TXT"],
                       capture_output=True, text=True)
    if r.returncode != 0 or "EVENT" not in r.stdout:
        return None
    rep = {}
    for line in r.stdout.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] == "MEAS":
            rep["MEAS"] = [int(x, 16) for x in f[1:]]
            continue
        for i in range(0, len(f) - 1, 2):
            rep[f[i]] = f[i + 1]
        if f[0] == "CPU":
            rep["CPU"], rep["CPUF"] = f[1], f[2]
        if f[0] == "X3REPORT":
            rep["X3REPORT"] = f[1]
    return rep


def offpick(meas, srate, caps):
    """the CONVERSION's rung, x3l_offpick's rule replayed from the report's
    probes (a report written before OFFLINE existed has none of its own):
    the highest-quality rung from Original to XT Fast measured at 1/8 real
    time (12 x100) or better; else XT Standard where the device plays the
    source's rate (x3l_ok1: a DSP without SND_CAP_PCM_HI tops out at
    22,222 Hz); else the cheapest STANDARD rung over the 8,000 Hz floor"""
    for q in range(1, 6):
        v = meas[q - 1] if q - 1 < len(meas) else 0xFFFF
        if v != 0xFFFF and v >= 12:
            return QNAME[q]
    top = 44100 if caps & 0x20 else 22222
    if srate <= top:
        return "XT STANDARD"
    for q, d in ((3, 4), (2, 2), (1, 1)):
        if srate // d >= 8000 or srate <= 8000:
            return QNAME[q]
    return "ORIGINAL"


def decode(rep):
    """the report in words"""
    meas = {}
    for i, v in enumerate(rep.get("MEAS", [])):
        if v != 0xFFFF:
            meas[QNAME[i + 1]] = v / 100.0
    md = int(rep["MD"], 16)
    choice = int(rep["CHOICE"], 16)
    want = int(rep["WANT"], 16)
    rate, chans, bits = (int(rep["RATE"], 16), int(rep["CHANS"], 16),
                         int(rep["BITS"], 16))
    caps = int(rep["CAPS"], 16)
    srate = int(rep["SRC"], 16)
    off = None
    if "OFFLINE" in rep:
        off = QNAME[int(rep["OFFLINE"], 16)]
    elif choice == 0xFF:
        off = offpick(rep.get("MEAS", []), srate, caps) + " (derived)"
    return {
        "offline_rung": off,
        "event": rep["EVENT"],
        "cpu": CPU.get(int(rep["CPU"], 16), rep["CPU"]),
        "card": int(rep["CARD"], 16), "caps": "%04X" % caps,
        "source": "%d Hz, %d ch" % (int(rep["SRC"], 16), int(rep["CH"], 16)),
        "calibration_s": round(int(rep["CALPIT"], 16) * PIT, 3),
        "ladder_reruns": int(rep["PASS"], 16) if "PASS" in rep else None,
        "measured": meas,
        "choice": "Decode first" if choice == 0xFF else QNAME[choice],
        "override": QNAME[want] if want else None,
        "mode": "decode first dialog" if rep["EVENT"] == "D" else
        {1: "checking", 2: "live", 3: "finished file"}.get(md, "md %d" % md),
        "quality": QNAME[int(rep["QUAL"], 16)],
        "device_format": "%d Hz %s %d-bit" % (rate, "stereo" if chans == 2
                                              else "mono", bits)
        if chans else None,
        "flags": rep["FLAGS"],
        "underruns": int(rep["UNDER"], 16),
        "peak_kb": int(rep["PEAK"], 16),
        "granules": int(rep["GRANS"], 16),
        "ring": ("card %d, look-ahead %d, gulp %d" % tuple(
            int(rep[k], 16) for k in ("RING", "LA", "RD")))
        if "RD" in rep else None,
        "first_underrun": ("granule %d, %s" % (
            int(rep["UG"], 16), {0x55: "the card ran dry",
                                 0x53: "the look-ahead starved"}.get(
                int(rep["UK"], 16), "kind %s" % rep["UK"])))
        if int(rep.get("UK", "0"), 16) else None,
    }


def run(machine, fix, limit, shots):
    tag = "%s-%s" % (machine, fix)
    d = os.path.join(ROOT, "build", "x3field", tag)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    xt = machine == "xt"
    a = os.path.join(d, "a.img")
    b = os.path.join(d, "b.img")
    shutil.copy(os.path.join(ROOT, "build",
                             "x3field360.img" if xt else "x3field.img"), a)
    shutil.copy(os.path.join(ROOT, "build", "x3fb%s-%s.img" %
                             ("360" if xt else "", fix)), b)
    cfg = open(os.path.join(ROOT, "vm", "%s-xtmp3" % machine,
                            "86box.cfg")).read().splitlines()
    out = []
    for line in cfg:
        if line.startswith("fdd_01_fn = "):
            line = "fdd_01_fn = " + a
        elif line.startswith("fdd_02_fn = "):
            line = "fdd_02_fn = " + b
        out.append(line)
    open(os.path.join(d, "86box.cfg"), "w").write("\n".join(out) + "\n")
    nvr, got = nvr_for(machine, cfg)
    if got:
        os.makedirs(os.path.join(d, "nvr"))
        open(os.path.join(d, "nvr", got[0] + ".nvr"), "wb").write(got[1])
        flash = os.path.join(os.path.dirname(nvr), got[0] + ".bin")
        if os.path.exists(flash):
            shutil.copy(flash, os.path.join(d, "nvr"))
    log = open(os.path.join(d, "86box.log"), "w")
    p = subprocess.Popen([BOX, "-P", d, "-N"], stdout=log, stderr=log)
    t0 = time.time()
    rep, n, playing = None, 0, None
    try:
        while time.time() - t0 < limit:
            time.sleep(4)
            n += 1
            shot = os.path.join(d, "shot-%03d.png" % n)
            capture(p.pid, shot)
            rep = report(b)
            if rep:
                time.sleep(2)
                capture(p.pid, os.path.join(d, "final.png"))
                break
            if os.path.exists(shot):
                playing = shot
    finally:
        os.kill(p.pid, signal.SIGKILL)
        p.wait()
        log.close()
    res = {"machine": machine, "fixture": FIXES[fix], "nvr": nvr,
           "host_s": round(time.time() - t0, 1)}
    if rep:
        res.update(decode(rep))
    else:
        res["error"] = "no report in %d s" % limit
    for name, src in (("play", playing), ("final", os.path.join(d,
                                                                 "final.png"))):
        if src and os.path.exists(src) and shots:
            os.makedirs(shots, exist_ok=True)
            dst = os.path.join(shots, "%s-%s.png" % (tag, name))
            shutil.copy(src, dst)
            res["shot_" + name] = os.path.relpath(dst, ROOT)
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--machine", action="append", choices=sorted(MACHINES))
    ap.add_argument("--fix", action="append", choices=sorted(FIXES))
    ap.add_argument("--limit", type=float, default=300.0,
                    help="host seconds a run may take")
    ap.add_argument("--json")
    ap.add_argument("--shots", help="copy each run's screenshots here")
    a = ap.parse_args()
    if not os.path.exists(BOX):
        sys.exit("x3field: no 86Box at %s" % BOX)
    for f in ("x3field.img", "x3field360.img", "x3fb-c128.img"):
        if not os.path.exists(os.path.join(ROOT, "build", f)):
            sys.exit("x3field: build/%s is missing - make xtmp3field" % f)
    rows = []
    for m in a.machine or ["xt", "286", "386", "486", "pentium"]:
        for f in a.fix or ["c128", "aftr"]:
            print("x3field: %s on %s ..." % (f, m), flush=True)
            r = run(m, f, a.limit, a.shots)
            rows.append(r)
            print("   " + json.dumps(r, sort_keys=True), flush=True)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1, sort_keys=True)
    print()
    print("%-8s %-5s %-14s %-24s %-22s %s" % ("machine", "fix", "choice",
                                              "measured (x)", "device format",
                                              "underruns"))
    for r in rows:
        print("%-8s %-5s %-14s %-24s %-22s %s" % (
            r["machine"], r["fixture"][:4], r.get("choice", r.get("error")),
            " ".join("%s=%.2f" % (k.split()[-1][:4], v)
                     for k, v in r.get("measured", {}).items()),
            r.get("device_format"), r.get("underruns")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
