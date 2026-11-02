#!/usr/bin/env python3
"""vidrmbtwice - two plays of a Catch-shaped file on a Sound Blaster, on
kern_mega's REAL-MODE bracket and on kern_big's shipped player: what a play
SOUNDS like from its first sample, which frame that sample is under, and what
its full-screen text is drawn from (SPEC.md 53.2.3, 98.1.8.2, 98.3.24;
KERN-MEGA-PLAN 14.131, 14.132, 14.133, 14.134).

    python3 tests/vidrmbtwice.py [--keep] [--leg mega|big]

The owner's Victory 286 (4 MB, a Paradise PVGA1A whose option ROM is slow,
a PicoMEM's Sound Blaster) reported these on the 9abdcaae kern_mega pair,
and the static on kern_big as well:

  - 07-Miku.V88 played with NO SOUND, every try, between files that had it.
    The player's FSXF_REAL list wrote the file's name as vp_name's 13 bytes;
    the kernel reads it to its NUL and takes the next byte as the next
    entry, so a name under 12 characters put a 0 - FSXL_END - where
    FSXL_DRV DRVC_SOUND was, and the bracket carried no sound driver
    (14.132). The file here is CATCH.V88, nine characters.
  - every play's first second was STATIC (14.133, 98.1.8.2). The
    sound-ahead lead of a file whose key leads are apart is read into the
    ring past the key, and was staged out of it only once the session had
    read more into the same ring - the key's later parts, the first fill:
    on kern_big a lead's last bytes were the reader's, and on kern_mega,
    read by a selector on the desktop and staged by a paragraph in the
    bracket, all of them were. It is staged where it is read now.
  - a play from a key began its sound a FRAME LATE: a key's lead is frames
    k + 1 .. k + A (98.1.8), so a play from key 0 at frame 0 never played
    frame 0's sound and the card's clock started under frame 1. The
    start's lead holds frame 0's, and is staged before the key's now, the
    clock anchored on the key's own frame (14.134).
  - from the owner's second play on, kern_mega's full-screen text (the
    buffering line, here "Paused") was NOISE in the right colour: the
    glyph table's copy (MEM_K_FONT) is above 1MB on kern_mega and the
    bracket handed OSAPI_FONT_GLYPHS its selector (14.131). FONTSLOW=1
    forces the copy - 86Box does not slow an option ROM down - so the mega
    leg boots a private `make FONTSLOW=1 mega` tree.

86Box's mr286 (vm/at286-port, an OTi VGA, the IDE disk the MR BIOS serves)
with an SB2.0 at IRQ 7, its DAC captured; SOUND.DRV wanted. C: holds the
leg's VIDEO.O88 and a Catch-shaped clip (16 colours on 320 x 200n, two pages
flipped, PCM8 SIX frames ahead with the key leads apart, keys every 2 s)
whose sound is WHITE NOISE from its first sample - a capture matches it in
one place, and the first byte it holds is frame 0's - over a black second,
so key 0 is frame 0 and not a pre-roll's. The MEGA leg is 4 MB on the
FONTSLOW tree; the BIG leg is build/os8088.img's kern_big at 1 MB, where
this clip's lead straddles what the reader takes. Each leg opens the file
and plays it to its end TWICE, pausing each play four seconds in with
Space. Asserted, of each play:

  1. (mega) a real-mode bracket ([fsx_rmode] 1) and the card OPEN in it;
     (big) the card open;
  2. (mega) while paused, the glyph table the player draws from
     ([vo_gseg]:[vo_goff], a real segment) holds the kernel's 760 glyph
     bytes - and play 2's "Paused" box is play 1's, pixel for pixel;
  3. the capture from its first non-silent sample IS the file's sound from
     byte 0, its first half second whole;
  4. FRAME 1 IS DRAWN A FRAME AFTER THE CARD STARTS: the guest time from
     the card open ([vp_sopn] 1) to the first frame past the key ([vp_done]
     2) is at least ALIGN of a frame - the card plays frame 0's sound under
     frame 0's picture;

and no kern_mega FAULT or CLOSED.

THE FAILING CASES (docs/WRITING-TESTS.md 1), each verified on the tree it
went in with: the 13-byte name copy back in vcold.asm's vp_rmklist takes
mega's 1 red (the card never opened) and its 3 with it; vp_lnow's
vp_lstage out (the lead staged at the session's start again) takes 3 red
on both legs; vp_lnow's frame-0 piece out takes 3 and 4 red on both legs;
fsx.inc's glyph copy out (fsx_rfont left 0) takes mega's 2 red.
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
import os88box                                                # noqa: E402
import os88build                                              # noqa: E402
import os88geom as G                                          # noqa: E402
import os88vid as vid                                         # noqa: E402

C_ZONE = (592, 165)
FIRST = (160, 127)
HALF = 11025                    # the samples the start is checked over
ALIGN = 0.6                     # ...and the frame frame 1 must wait (it
                                # waited ~0 while the card started a frame
                                # on)
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def runs(b):
    out = bytearray()
    last = None
    for x in b:
        if x != last:
            out.append(x)
            last = x
    return bytes(out)


def card_bytes(path):
    """the card's bytes back out of 86Box's capture (tests/vidsound.py's
    reading)"""
    import sndcheck
    rate, vals, _ = sndcheck.load(path)
    return bytes(max(0, min(255, int(round(v * 128.0)) + 128))
                 for v in vals)


def check_start(label, got, ref):
    """3: the play's capture, from its first non-silent sample, is REF's
    from byte 0 - the file's sound opens on noise, so a frame lost or a
    byte that is not the file's shows in the first distinct samples"""
    g = runs(got)
    k = 0
    while k < len(g) and g[k] == 0x80:
        k += 1
    if k >= len(g):
        want(False, "%s 3. the card played the file's sound (SILENT)"
             % label)
        return
    w = runs(ref[:HALF])
    W = runs(ref)
    at = W.find(g[k:k + 32])
    want(at == 0, "%s 3. the card's first sound is the file's byte 0 (it is "
         "the file's distinct sample %s)" % (label, at if at >= 0 else
                                             "nowhere"))
    if at < 0:
        return
    n = next((i for i in range(len(w)) if k + i >= len(g)
              or g[k + i] != W[at + i]), len(w))
    want(n == len(w), "%s 3. ...and the file's sound from there on, a half "
         "second of it whole (%d of %d distinct samples)"
         % (label, n, len(w)))


def clip():
    """a Catch-shaped clip with WHITE NOISE for its sound from the first
    sample, over a black second (no pre-roll: key 0 is frame 0), made once
    into build/vidrmbtwice/"""
    out = os.path.join(ROOT, "build", "vidrmbtwice", "CLIP0.V88")
    if not os.path.exists(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        src = tempfile.mkdtemp(prefix="vidrmbtwice")
        mkv = os.path.join(src, "src.mkv")
        subprocess.run(
            ["ffmpeg", "-v", "error",
             "-f", "lavfi", "-i", "color=c=black:s=320x200:r=24:d=1",
             "-f", "lavfi", "-i", "testsrc2=s=320x200:r=24:d=3",
             "-f", "lavfi", "-i", "mandelbrot=s=320x200:r=24,trim=duration=3",
             "-f", "lavfi", "-i", "rgbtestsrc=s=320x200:r=24:d=3",
             "-f", "lavfi", "-i", "anoisesrc=d=10:c=white:r=22050:a=0.5:"
             "seed=88",
             "-filter_complex",
             "[0:v]format=yuv420p[z];[1:v]format=yuv420p[a];"
             "[2:v]format=yuv420p[b];[3:v]format=yuv420p[d];"
             "[a][b][d]concat=n=3:v=1:a=0,noise=alls=70:allf=t+u[n];"
             "[z][n]concat=n=2:v=1:a=0[v]",
             "-map", "[v]", "-map", "4:a", "-c:v", "ffv1", "-c:a", "pcm_u8",
             mkv], check=True)
        subprocess.run([sys.executable,
                        os.path.join(ROOT, "tools", "os88venc.py"),
                        mkv, out + ".part", "--quiet", "--preset", "vga4",
                        "--pixfmt", "vga4", "--palette", "colour16",
                        "--screen", "320x200n", "--flip", "--pages", "max",
                        "--page-ahead", "off", "--scene-palettes", "--audio",
                        "pcm8", "--rate", "22050", "--fps", "24",
                        "--ahead-frames", "6", "--buffer-sound-ahead", "on"],
                       check=True)
        os.rename(out + ".part", out)
        shutil.rmtree(src, ignore_errors=True)
    r = vid.Reader(out)
    if r.keys[0][0] != 0 or not r.ahead or not r.kleadat or \
            r.sound(0)[:1] == b"\x80":
        sys.exit("vidrmbtwice: the clip is not what the row needs (key 0 at "
                 "%d, ahead %d, leads apart %s)" % (r.keys[0][0], r.ahead,
                                                    bool(r.kleadat)))
    return out


def leg(kind, v88, ref, keep):
    mega = kind == "mega"
    lab0 = "%s " % kind
    print(" -- %s" % ("kern_mega, 4 MB, the FONTSLOW tree, the real-mode "
                      "bracket" if mega else "kern_big, 1 MB, the shipped "
                      "player"), flush=True)
    if mega:
        t = os88build.tree("FONTSLOW=1", targets=(
            "mega.img", "megak/apps/video.o88")).apply()
        os.environ["OS88_MEGA_BUILD"] = t.dir
        os.environ["OS88_MEGA_SND"] = "sb2.0"
        bld = t.dir
        pkg = os.path.join(bld, "megak", "apps", "video.o88")
        vmap = os.path.join(bld, "megak", "apps", "video.map")
    else:
        bld = os.path.join(ROOT, "build")
        pkg = os.path.join(bld, "video.o88")
        vmap = os.path.join(bld, "video.map")
    tmp = tempfile.mkdtemp(prefix="vidrmbtwice-" + kind)
    cap = os.path.join(tmp, "cap")
    os.environ["MARTYPC_WAV"] = cap
    wav = cap + ".sound_blaster.wav"
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88hdd.py"),
                    "--out", vhd, "--noboot", "--spt", "17", "--heads", "15",
                    "--cyls", "100", "--file", "CATCH.V88=" + v88,
                    "--file", "VIDEO.O88=" + pkg],
                   check=True, stdout=subprocess.DEVNULL)
    vs = {}
    for ln in open(vmap):
        mm = re.match(r"\s+([0-9A-F]+)\s+([0-9A-F]+)\s+(\w+)\s*$", ln)
        if mm:
            vs[mm.group(3)] = int(mm.group(1), 16)
    if mega:
        import os88mega86box as M
        m = M.launch(os.path.join(bld, "mega.img"), cimg=vhd, cfg=0x01,
                     apps=os.path.join(ROOT, "build", "megaapps.img"),
                     label="vidrmbtwice", machine="os8088_xt_vga")
    else:
        import os88box86api as BA
        m = BA.launch(os.path.join(bld, "os8088.img"), cimg=vhd, cfg=0x01,
                      megs=1, label="vidrmbtwice",
                      machine="os8088_xt_vga_hdd_sb")   # (an AT by
                                        # its 1.44MB A:: vm/at286-port)
    b = m.b
    b.cmd("hold")
    boxes, starts, bad = [], [], []

    def player():
        raw = b.memr(b.sym("inst_tab"), G.INST_MAX * G.I_RECSZ)
        for i in range(G.INST_MAX):
            rr = raw[i * G.I_RECSZ:(i + 1) * G.I_RECSZ]
            nm = rr[G.I_NAME:G.I_NAME + 16].split(bytes(1))[0]
            if rr[G.I_STATE] == 1 and nm == b"Video Player":
                return rr[G.I_SPTR] | rr[G.I_SPTR + 1] << 8
        return None

    def base():                         # where the playing player's bss is:
        if mega:                        # the bracket's copy of it, or the
            return b.word("fsx_rseg") * 16  # window's own
        return player() * 16

    def cw(name, n=2):
        d = b.memr(base() + vs[name], n)
        return d[0] | d[1] << 8 if n == 2 else d[0]

    try:
        b.ui_done(cap_ms=10000)
        since = sum(1 for _ in open(os.path.join(b.run, "stderr.log"),
                                    errors="replace"))
        glyphs = None
        if mega:
            fs = b.word("font_seg")
            glyphs = b.memr(b.lin(fs) + b.word("font_base"), 760)
            print("   the glyph table: selector %04X, base %06X (the copy: "
                  "above 1MB)" % (fs, b.lin(fs)))
            if b.lin(fs) < 0x100000:
                FAILS.append("the glyph table is not the copy above 1MB - "
                             "FONTSLOW did not reach the kernel")
        b.to(*C_ZONE)
        b.click(2)
        b.pause(8000)
        b.ui_done(limit_ms=60000)
        b.to(*FIRST)
        b.click(2)
        b.until(lambda: player() is not None, "the Video Player to start",
                60000, 100)
        b.pause(10000)
        b.ui_done(limit_ms=60000)
        per = 1000.0 / vid.Reader(v88).fps
        for n in (1, 2):
            lab = "%splay %d:" % (lab0, n)
            starts.append(os.path.getsize(wav) if os.path.exists(wav) else 0)
            b.tap("space", gap=1)
            b.until(lambda: b.byte("fsx_task") != 0xFF, "the bracket", 20000,
                    1)
            t, t_open, t_draw, shut = 0, None, None, False
            while t < 3000 and t_draw is None:  # 4: frame 1 a frame after
                b.cmd("step 1")                 # the card started - OPENED,
                t += 1                          # 0 then 1: the bracket's
                o = cw("vp_sopn", 1)            # copy holds the last play's
                shut = shut or o == 0           # state until the session
                if t_open is None and shut and o == 1:  # starts
                    t_open = t
                if t_open is not None and cw("vp_done") >= 2:
                    t_draw = t
            want(t_open is not None and t_draw is not None and
                 t_draw - t_open >= ALIGN * per,
                 "%s 4. frame 1 drawn %s ms after the card opened (a frame "
                 "is %.1f ms: at least %.1f)" % (
                     lab, "-" if t_draw is None or t_open is None else
                     t_draw - t_open, per, ALIGN * per))
            b.pause(4000)                   # (past the half second 3 reads:
            rm = b.byte("fsx_rmode") if mega else 1   # a halted card's DAC
            snd, sopn = cw("vp_snd", 1), cw("vp_sopn", 1)   # is silence)
            b.tap("space", gap=300)         # PAUSED: the box
            b.pause(1500)
            if mega:
                gs, go = cw("vo_gseg"), cw("vo_goff")
                seen = b.memr(gs * 16 + go, 760)
                png = os.path.join(tmp, "paused%d.png" % n)
                b.cmd("screenshot %s" % png)
                boxes.append(png)
            b.tap("space", gap=300)
            want(rm == 1 and snd == 1 and sopn == 1,
                 "%s 1. %sthe card open in it ([vp_snd] %d, [vp_sopn] %d)"
                 % (lab, "a real-mode bracket ([fsx_rmode] %d) and " % rm
                    if mega else "", snd, sopn))
            if mega:
                want(seen == glyphs, "%s 2. the text's glyphs at %04X:%04X "
                     "are the kernel's table (%d of 760 bytes differ)"
                     % (lab, gs, go,
                        sum(x != y for x, y in zip(seen, glyphs))))
            b.until(lambda: b.byte("fsx_task") == 0xFF, "the play to end",
                    90000, 200)
            b.ui_done(limit_ms=60000, lock=False)
            b.pause(2000)
        starts.append(None)
        for k, ln in enumerate(open(os.path.join(b.run, "stderr.log"),
                                    errors="replace")):
            mf = os88box.FAULT.search(ln)
            if k >= since and mf and int(mf.group(2), 16) < 0xF000 and \
                    int(mf.group(2), 16) not in (0x0060, 0x0028):
                bad.append(ln.strip())
    except SystemExit as e:
        want(False, "%sthe machine stopped answering or a step missed: %s"
             % (lab0, e))
    finally:
        b.quit(remove=not keep)
    if len(boxes) == 2:
        from PIL import Image, ImageChops
        box = (16, 40, 144, 64)             # "Paused": its ground and text

        def mask(path):                     # the text, as INK against the
            im = Image.open(path).convert("RGB").crop(box)   # box's ground:
            g = im.getpixel((0, 0))         # the ink's colour is the scene
            return Image.eval(              # palette's brightest, which a
                ImageChops.difference(im, Image.new("RGB", im.size, g))
                .convert("L"), lambda v: 255 if v else 0)   # pause can land
        diff = ImageChops.difference(mask(boxes[0]), mask(boxes[1]))  # on
        want(diff.getbbox() is None, "%s2. play 2's \"Paused\" box is play "
             "1's, pixel for pixel in its ink (differ in %s)"
             % (lab0, diff.getbbox(),))
    if os.path.exists(wav) and len(starts) == 3:
        cut = max(0, (starts[1] - 44) // 2)
        g = card_bytes(wav)
        check_start(lab0 + "play 1:", g[:cut], ref)
        check_start(lab0 + "play 2:", g[cut:], ref)
    else:
        want(False, "%s3. the card's capture (%s)" % (lab0, wav))
    if bad:
        want(False, "%sno kern_mega FAULT: %s" % (lab0, bad[0]))
    if keep or FAILS:
        print("vidrmbtwice: kept %s" % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--leg", action="append", choices=("mega", "big"))
    a = ap.parse_args()
    v88 = clip()
    r = vid.Reader(v88)
    ref = b"".join(r.sound(f) for f in range(r.frames))
    for k in a.leg or ("big", "mega"):    # (big first: the mega leg's
                                            # tree.apply() points os88sym at it)
        leg(k, v88, ref, a.keep)
    print("vidrmbtwice: %s" % ("FAIL - %d" % len(FAILS) if FAILS else "ok"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
