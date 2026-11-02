#!/usr/bin/env python3
"""vidmegaclaims - the Video Player's kern_mega arm PLAYS a Catch-shaped
file off a BIOS-served C: on a 4 MB 286 (SPEC.md 50's per-owner cap,
KERN-MEGA-PLAN 14.128).

    make mega && python3 tests/vidmegaclaims.py [--keep]

The field report (the owner's Victory 286, 4 MB, a Paradise VGA, C: through
the PicoMEM's option-ROM int 13h): Catch My Heart opened and showed its
first frame, and Play said "Not enough memory to play it" with 474K free in
one run. It was not memory. The arm holds EIGHT claims before a play's ring
- the block decoder part, the cold part (98.3.24.1, which made it eight),
the shaper part, the palette events, the poster, the keeper and the text's
second save among them - and MEM_OWNER_MAX was 8: the ring was the ninth
claim and the kernel refused it whatever the heap held (and the 80 KB the
bracket's room is held in, 98.3.24.2, the tenth).

This boots build/mega.img on 86Box's mr286 (vm/at286-port, 4 MB, an OTi
VGA, the ISA IDE disk the MR BIOS serves) with a C: of its own: the arm's
VIDEO.O88 and a file shaped like the owner's - 16 colours on the narrow
320 x 200 screen, two pages flipped, a palette a scene, PCM8 sound ahead at
22,050 Hz played on the speaker, ~24 fps - which tools/os88venc.py makes
from an ffmpeg source on the first run and keeps in build/vidmegaclaims/.
It opens the file from C:'s window and presses Space. Asserted:

  1. the play TAKES THE SCREEN: a bracket is up within 20 guest seconds;
  2. the player holds NINE OR MORE claims while it plays - the case the cap
     refused - read off the kernel's claim map by owner;
  3. the play ends by itself (Repeat off), the player still running, and
     its message is not "Not enough memory to play it". (os88spk_init's
     writes through CS under protected mode are completed #GP fixups, as
     they always were, and are counted, not failed.)

Red on f56a048f (and on 471fc07f, before the bracket's room was held):
no bracket comes up, and the card says "Not enough memory to play it".
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
import heapmap                                                # noqa: E402
import os88box                                                # noqa: E402
import os88geom as G                                          # noqa: E402
import os88mega86box as M                                     # noqa: E402
import os88sym                                                # noqa: E402

B = os.path.join(ROOT, "build")
CACHE = os.path.join(B, "vidmegaclaims")
C_ZONE = (592, 165)             # C:'s zone on the VGA desktop
FIRST = (160, 127)              # the first file of a fresh Disk window
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def clip():
    """the Catch-shaped file, made once (ffmpeg + os88venc, ~80 s)"""
    out = os.path.join(CACHE, "CATCH.V88")
    if os.path.exists(out):
        return out
    os.makedirs(CACHE, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="vidmegaclaims")
    src = os.path.join(tmp, "src.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error",
         "-f", "lavfi", "-i", "testsrc2=s=320x200:r=24:d=3",
         "-f", "lavfi", "-i", "mandelbrot=s=320x200:r=24,trim=duration=3",
         "-f", "lavfi", "-i", "life=s=320x200:r=24:mold=10:ratio=0.3:"
         "life_color=#ff8000:death_color=#002040,trim=duration=3",
         "-f", "lavfi", "-i", "rgbtestsrc=s=320x200:r=24:d=3",
         "-f", "lavfi", "-i", "sine=f=440:d=12:sample_rate=22050",
         "-filter_complex",
         "[0:v]format=yuv420p[a];[1:v]format=yuv420p[b];"
         "[2:v]format=yuv420p[c];[3:v]format=yuv420p[d];"
         "[a][b][c][d]concat=n=4:v=1:a=0,noise=alls=70:allf=t+u[v]",
         "-map", "[v]", "-map", "4:a", "-c:v", "ffv1", "-c:a", "pcm_u8",
         src], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88venc.py"),
                    src, out + ".part", "--quiet", "--preset", "vga4-full",
                    "--pixfmt", "vga4", "--palette", "colour16", "--screen",
                    "320x200n", "--flip", "--pages", "2", "--scene-palettes",
                    "--audio", "pcm8", "--rate", "22050", "--fps", "24"],
                   check=True)
    os.rename(out + ".part", out)
    shutil.rmtree(tmp, ignore_errors=True)
    return out


def vsyms():
    out = {}
    for ln in open(os.path.join(B, "megak", "apps", "video.map")):
        m = re.match(r"\s+([0-9A-F]+)\s+([0-9A-F]+)\s+(\w+)\s*$", ln)
        if m:
            out[m.group(3)] = int(m.group(1), 16)
    return out


def player(b):
    """(the instance's data selector, its linear base) or None"""
    raw = b.memr(b.sym("inst_tab"), G.INST_MAX * G.I_RECSZ)
    for i in range(G.INST_MAX):
        r = raw[i * G.I_RECSZ:(i + 1) * G.I_RECSZ]
        nm = r[G.I_NAME:G.I_NAME + 16].split(bytes(1))[0]
        if r[G.I_STATE] == 1 and nm == b"Video Player":
            sp = r[G.I_SPTR] | r[G.I_SPTR + 1] << 8
            return sp, b.lin(sp)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for f in ("mega.img", "megak/kernel.map", "megak/apps/video.o88",
              "megak/apps/video.map"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("vidmegaclaims: build/%s is not built - `make mega`" % f)
    v88 = clip()
    tmp = tempfile.mkdtemp(prefix="vidmegaclaims")
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88hdd.py"),
                    "--out", vhd, "--noboot", "--spt", "17", "--heads", "15",
                    "--cyls", "100",
                    "--file", "VIDEO.O88=" + os.path.join(B, "megak", "apps",
                                                          "video.o88"),
                    "--file", "CATCH.V88=" + v88],
                   check=True, stdout=subprocess.DEVNULL)
    sy = vsyms()
    off, _, equ = os88sym._parse_map(os.path.join(B, "megak", "kernel.map"))
    low = equ["LOW_SEG"] * 16           # (.lowbss: it moves with KERN_SIZE -
                                        # a size pass took it 0EA0 -> 0E40)
    m = M.launch(os.path.join(B, "mega.img"), cimg=vhd, label="vidmegaclaims",
                 machine="os8088_xt_vga")
    b = m.b
    b.cmd("hold")
    up, most, ended, msg, s_mem, f = False, 0, False, None, sy["vp_s_mem"], []
    try:
        b.ui_done(cap_ms=10000)
        since = sum(1 for _ in open(os.path.join(b.run, "stderr.log"),
                                    errors="replace"))
        b.to(*C_ZONE)
        b.click(2)
        b.pause(8000)
        b.ui_done(limit_ms=60000)
        b.to(*FIRST)
        b.click(2)
        b.until(lambda: player(b) is not None, "the Video Player to start",
                60000, 100)
        b.pause(10000)
        b.ui_done(limit_ms=60000)
        sp, base = player(b)

        class Q:
            def read(self, lin, n):
                return b.memr(lin, n)
        hs = {n: b.sym(n) for n in ("mem_base", "mem_top", "spl_live")}
        hs["mem_tab"] = low + off["mem_tab"]
        b.tap("space", gap=300)
        try:
            b.until(lambda: b.byte("fsx_task") != 0xFF, "the play's bracket",
                    20000, 100)
            up = True
        except SystemExit:
            pass
        if up:
            b.pause(2000)
            mp = heapmap.Map(Q(), hs)
            mine = [c for c in mp.claims if c.own == sp]
            most = len(mine)
            print("   the player's claims in the play: %d (%s)" % (
                most, " ".join("%.0fK" % c.kb for c in mine)))
            try:
                b.until(lambda: b.byte("fsx_task") == 0xFF, "the play to end",
                        90000, 200)
                ended = True
            except SystemExit:
                pass
            b.ui_done(limit_ms=60000, lock=False)
        pl = player(b)                  # (None: the player was closed)
        if pl:
            mv = b.memr(pl[1] + sy["vp_msg"], 2)
            msg = mv[0] | mv[1] << 8
        for k, line in enumerate(open(os.path.join(b.run, "stderr.log"),
                                      errors="replace")):
            mf = os88box.FAULT.search(line)
            if k >= since and mf and int(mf.group(2), 16) < 0xF000 and \
                    int(mf.group(2), 16) not in (0x0060, 0x0028):
                f.append(line.strip())
        if a.keep or not up:
            b.cmd("screenshot %s" % os.path.join(tmp, "after.png"))
    except SystemExit as e:
        want(False, "the machine stopped answering or a step missed: %s" % e)
    finally:
        b.quit(remove=not a.keep)
    want(up, "1. Space took the screen: a bracket came up")
    want(most >= 9, "2. the player held %d claims in its play (9 or more: "
         "the ninth was what the cap refused)" % most)
    want(ended, "3. the play ended by itself")
    want(msg is not None and msg != s_mem, "3. the player's message is not "
         "'Not enough memory to play it' ([vp_msg] %s)" %
         ("%04x" % msg if msg is not None else "-"))
    print("   (completed #GP fixups in loaded images: %d - the speaker "
          "library's init writes its own code, os88spk_init, which "
          "mega_gpfix finishes; the play is the same either way)" % len(f))
    if a.keep or FAILS:
        print("vidmegaclaims: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    print("vidmegaclaims: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
