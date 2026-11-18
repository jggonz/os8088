#!/usr/bin/env python3
"""Record a Spotlight page's hero video from a live QEMU boot.

    spotrec.py boot   --sock /tmp/spot/q.sock --a build/os8088.img [--b build/<disk>.img]
    spotrec.py record --sock /tmp/spot/q.sock --out <frames-dir> --secs 20 --fps 15 \
                      [--at "1.2:mouse_button 1;sleep 0.1;mouse_button 0" ...]
    spotrec.py encode --frames <frames-dir> --out <web>/public/img/<name>/<id>.mp4 \
                      [--fps 15] [--poster-frame N]
    spotrec.py quit   --sock /tmp/spot/q.sock

This is how Breadboard XT's hero clip (os8088-web public/img/breadxt/bx-run.mp4)
was made, written down so every Spotlight page gets one the same way.

`boot` starts QEMU headless with a serial mouse, both floppies -snapshot (the
build's images are never written) and QMP on --sock. Keep the socket path
short (/tmp/...): a scratchpad-length path overruns AF_UNIX.

`record` screendumps on a fixed cadence of the HOST clock. QEMU runs the guest
in real time against its own emulated PIT, so host time is guest time, and the
clip shows what a fast modern machine shows. It prints the achieved rate and
fails below 90% of --fps.

QEMU's QMP server takes ONE client. A tools/mouse.py click sent while the
recorder holds the socket does NOTHING, silently. So input DURING a take goes
down the recorder's own connection: each --at "T:CMD;CMD" runs its HMP
commands (`mouse_move DX DY`, `mouse_button 1`, `sendkey x 300`, `sleep N`)
T seconds after the first frame. Set the scene up BEFORE the take with
tools/mouse.py and tools/qmp.py as usual, while the recorder is not running.
Pace a pointer glide as several `mouse_move` steps with `sleep 0.06` between.

`encode` writes the MP4 the site embeds: 2x nearest-neighbour (crisp pixels
at 1280x960 from VGA), H.264 yuv420p, `-tune animation`, CRF 20, faststart,
no audio track. Beside it go `<id>-poster.png` and `<id>-poster@2x.png`
from --poster-frame (default: the last), and `<id>-sheet.png`, six frames
across the take. Open the sheet with the Read tool before using the clip.
"""
import argparse
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time


def qmp_open(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    f = s.makefile("rwb")
    json.loads(f.readline())                    # the greeting
    lock = threading.Lock()

    def call(cmd, **args):
        msg = {"execute": cmd}
        if args:
            msg["arguments"] = args
        with lock:
            f.write((json.dumps(msg) + "\n").encode())
            f.flush()
            while True:
                r = json.loads(f.readline())
                if "error" in r:
                    raise SystemExit(f"QMP {cmd}: {r['error']}")
                if "return" in r:
                    return r["return"]

    call("qmp_capabilities")
    return call


def cmd_boot(a):
    os.makedirs(os.path.dirname(os.path.abspath(a.sock)), exist_ok=True)
    if os.path.exists(a.sock):
        os.unlink(a.sock)
    pid = os.path.splitext(os.path.abspath(a.sock))[0] + ".pid"
    argv = ["qemu-system-i386", "-snapshot",
            "-chardev", "msmouse,id=m0", "-serial", "chardev:m0",
            "-drive", f"file={os.path.abspath(a.a)},format=raw,if=floppy",
            "-boot", "a"]
    if a.b:
        argv += ["-drive",
                 f"file={os.path.abspath(a.b)},format=raw,if=floppy,index=1"]
    argv += ["-display", "none",
             "-qmp", f"unix:{os.path.abspath(a.sock)},server,nowait",
             "-pidfile", pid,
             "-audiodev", "none,id=devsnd", "-device", "sb16,audiodev=devsnd",
             "-daemonize"] + a.extra
    subprocess.run(argv, check=True)
    time.sleep(a.settle)
    print(f"booted; QMP at {a.sock}, pid file {pid}")


def cmd_record(a):
    events = []
    for spec in a.at:
        t, cmds = spec.split(":", 1)
        events.append((float(t), [c.strip() for c in cmds.split(";")
                                  if c.strip()]))
    os.makedirs(a.out, exist_ok=True)
    for old in glob.glob(os.path.join(a.out, "f*.ppm")):
        os.unlink(old)
    call = qmp_open(a.sock)
    t0 = time.monotonic()

    def driver():
        for at, cmds in sorted(events):
            d = t0 + at - time.monotonic()
            if d > 0:
                time.sleep(d)
            for c in cmds:
                if c.startswith("sleep "):
                    time.sleep(float(c.split()[1]))
                else:
                    call("human-monitor-command", **{"command-line": c})

    th = threading.Thread(target=driver, daemon=True)
    th.start()
    n = int(a.secs * a.fps)
    for i in range(n):
        d = t0 + i / a.fps - time.monotonic()
        if d > 0:
            time.sleep(d)
        call("screendump", filename=os.path.abspath(
            os.path.join(a.out, f"f{i:04d}.ppm")))
    th.join(timeout=10)
    el = time.monotonic() - t0
    got = n / el
    print(f"{n} frames in {el:.2f}s = {got:.2f} fps")
    if got < 0.9 * a.fps:
        sys.exit(f"recorder fell behind ({got:.1f} of {a.fps} fps): the host "
                 "is loaded. Wait for builds to finish and take it again.")


def cmd_encode(a):
    for tool in ("ffmpeg", "magick"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is not installed (brew install {tool})")
    frames = sorted(glob.glob(os.path.join(a.frames, "f*.ppm")))
    if not frames:
        sys.exit(f"no frames in {a.frames}")
    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    stem = os.path.splitext(out)[0]
    subprocess.run(
        ["ffmpeg", "-loglevel", "error", "-y", "-framerate", str(a.fps),
         "-i", os.path.join(a.frames, "f%04d.ppm"),
         "-vf", "scale=iw*2:ih*2:flags=neighbor,format=yuv420p",
         "-c:v", "libx264", "-preset", "veryslow", "-tune", "animation",
         "-crf", str(a.crf), "-movflags", "+faststart", "-an", out],
        check=True)
    pf = frames[a.poster_frame] if a.poster_frame is not None else frames[-1]
    png = ["-colors", "64", "-define", "png:compression-level=9"]
    subprocess.run(["magick", pf] + png + [stem + "-poster.png"], check=True)
    subprocess.run(["magick", pf, "-filter", "point", "-resize", "200%"] + png
                   + [stem + "-poster@2x.png"], check=True)
    picks = [frames[round(i * (len(frames) - 1) / 5)] for i in range(6)]
    subprocess.run(["magick"] + picks[:3] + ["+append", stem + "-r1.tmp.png"],
                   check=True)
    subprocess.run(["magick"] + picks[3:] + ["+append", stem + "-r2.tmp.png"],
                   check=True)
    subprocess.run(["magick", stem + "-r1.tmp.png", stem + "-r2.tmp.png",
                    "-append", "-resize", "50%", stem + "-sheet.png"],
                   check=True)
    for t in (stem + "-r1.tmp.png", stem + "-r2.tmp.png"):
        os.unlink(t)
    kb = os.path.getsize(out) // 1024
    secs = len(frames) / a.fps
    print(f"{out}: {kb} KB, {len(frames)} frames, {secs:.1f} s")
    print(f"poster {stem}-poster.png (+@2x) from {os.path.basename(pf)}")
    print(f"LOOK AT {stem}-sheet.png, then delete it - it is not published")
    if kb > 2048:
        print("WARNING: over 2 MB; shorten the take or raise --crf")


def cmd_quit(a):
    try:
        qmp_open(a.sock)("quit")
    except (OSError, ConnectionError):
        pass
    print("QEMU stopped")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("boot")
    b.add_argument("--sock", required=True)
    b.add_argument("--a", required=True, help="the system floppy")
    b.add_argument("--b", help="the software floppy for drive B:")
    b.add_argument("--settle", type=float, default=10)
    b.add_argument("extra", nargs="*", help="more QEMU arguments, after --")
    r = sub.add_parser("record")
    r.add_argument("--sock", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--secs", type=float, default=20)
    r.add_argument("--fps", type=float, default=15)
    r.add_argument("--at", action="append", default=[],
                   help='"T:HMP;HMP" run T seconds into the take')
    e = sub.add_parser("encode")
    e.add_argument("--frames", required=True)
    e.add_argument("--out", required=True)
    e.add_argument("--fps", type=float, default=15)
    e.add_argument("--crf", type=int, default=20)
    e.add_argument("--poster-frame", type=int)
    q = sub.add_parser("quit")
    q.add_argument("--sock", required=True)
    a = ap.parse_args()
    {"boot": cmd_boot, "record": cmd_record, "encode": cmd_encode,
     "quit": cmd_quit}[a.cmd](a)


if __name__ == "__main__":
    main()
