#!/usr/bin/env python3
"""Video Battle on KERN_MEGA, in a REAL-MODE bracket (SPEC.md 125.4.5,
docs/plans/VBATTLE-MEGA-PLAN.md W1).

    make && make mega && python3 tests/vbtmega.py [--emu bochs|86box]
        [--arm play|refuse|card|hold|ems] [--adapter vga|cga|herc]

ON BOCHS (a 386, VGA, 16 MB; the default) all arms but ems run on the CGA4
demo in mode 4. ON 86Box (W4, --emu 86box: the owner's mr286 through
kern_mega's port, docs/86BOX.md) the 286 leaves protected mode by the CMOS
shutdown RESET, and the legs are play on CGA, play on HERCULES (the
Hercules rendition, B000's 32 KB at byte 5), card (a Sound Blaster 2.0 on
the emulated clock: the counters, no capture), hold (read in place by a
286's LOADALL) and ems.

The SHIPPED engine on kern_mega, in Bochs (a 386, VGA, 16 MB), the system
disk kern_mega's and B: a floppy with VBATTLE.O88 and the CGA4 demo, its
enemy at 1 HP. Before this, the first Play was CLOSED by a #GP: vb_load
added paragraphs to the resident claim, an LDT selector there. Now the
claims are made at the door and everything that sums paragraphs runs
inside an FSXF_REAL bracket:

  play    1. the play's bracket is a REAL-MODE one - the kernel's own
             [fsx_rmode] seen 1 while it runs;
          2. the title, then Enter: the menu; Enter: Attack, victory and
             its end loop - the whole battle inside that one bracket;
          3. Enter: the play again, read inside it too (vb_lread) - and the
             title is then the host's decode in B8000, byte for byte;
          4. Esc: back on the desktop, the window's word 0 and the rings
             given back; and Enter again plays a SECOND time to the title,
             the resident claim's parts derived afresh in that bracket;
          5. no kern_mega FAULT and no CLOSED, anywhere.
  card    W2: the same play with Bochs's SB16 (FSXL_DRV DRVC_SOUND in the
          list, SOUND.DRV's low copy): the card's ring claimed at the door,
          the music never dry ([vb_munder]) and the card never found halted
          ([vb_nunder]), the card consuming seconds of the stream ([vb_cons32])
          - and Bochs's capture (OUT/sb.wav) is the battle's MUSIC: its best
          correlation with the bundle's music is at the bundle's own rate
          and stands clear of every other resampling. BROKEN ON PURPOSE
          (vb_mklist leaving DRVC_SOUND out) the play is SILENT - the card
          consumes nothing and the capture is empty - and this goes red.
  hold    W3: the bundle HELD in XMS (Bochs's 16 MB): opened at the door
          (an XMS block, a bounce claim - both refused inside a real-mode
          bracket), filled inside through OSAPI_XMEM_COPY's twin, and READ
          IN PLACE by a 386's loaded base (os88ub.inc) - the in-place check
          passing in the bracket: the token is the block's linear address
          while the pool leaves it where it is. From the fill on, a battle
          and its play again with no disk read, the decode yielding, the
          title again exact; and a second bracket checks in place again.
          BROKEN ON PURPOSE (the door's vb_hopen left out) no hold opens and
          this goes red. A block the pool MOVED between plays fails that
          check and the hold copies instead - not provoked here.
  ems     W4: the hold in EXPANDED memory - a Lo-tech board added to 86Box's
          mr286 (the name's _ems) and kern_mega's system disk given a
          SYSTEM.CFG wanting EMS.DRV: the hold opened at the door through
          EMS.DRV, the list carrying DRVC_EMS, filled through the twin and
          READ IN PLACE through the frame; no disk read from the fill on.
  refuse  the kernel made to fall back: at OSAPI_FSX_RUN's call the list
          gains FSXL_DRV DRVC_NET, a class the real-mode bracket does not
          carry (FSXR_DRV) - so the bracket runs in PROTECTED mode: vb_main
          does nothing, [fsx_rmode] never 1, no fault, and the window says
          "Needs real mode: a driver".

BROKEN ON PURPOSE (verified when the row was written): vb_door's `and ah,
CPU_F_PM` made `and ah, 0`, the door blind to kern_mega - the first Play is
CLOSED by the #GP again and "the play" never happens. And before the fix it
names, each ring's END summed from the selector at the door (vb_rclaim's
.set) left 3 red: the first title exact, the play again - whose reads wrap
the ring - 192 bytes of garbage in B8000.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import shutil
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.environ["OS88_MEGA"] = "1"
if "--emu" in sys.argv and sys.argv[sys.argv.index("--emu") + 1] == "86box":
    os.environ["OS88_MEGA_EMU"] = "86box"       # (before os88marty loads)
import os88build    # noqa: E402
import os88geom as geom    # noqa: E402
import os88marty    # noqa: E402
import os88ui       # noqa: E402
import os88vbt as B         # noqa: E402
import vbtproj as P         # noqa: E402
from cycweb import pkg_syms     # noqa: E402

# THE ADAPTER: Bochs is a VGA (the CGA4 demo in mode 4); 86Box gives the
# owner's mr286 a CGA or a Hercules (docs/86BOX.md, kern_mega rows)
OPT = {"emu": "bochs", "adapter": "vga"}
SCREEN = {"vga": (0xB8000, 16384), "cga": (0xB8000, 16384),
          "herc": (0xB0000, 32768)}


def screens_of(b, ci):
    """Clip CI's frames as the adapter's screen, decoded by the host"""
    n = SCREEN[OPT["adapter"]][1]
    rs = b.stream(b.clips[ci])
    s = bytearray(65536)
    out = []
    for r in rs[:-1]:
        B.decode(s, r)
        if OPT["adapter"] == "herc":    # (centred: at byte 5, vbtadapters's
            out.append(bytes(5) + bytes(s[:n - 5]))     # host_full)
        else:
            out.append(bytes(s[:n]))
    return out


def faults(m, S):
    """kern_mega's own record, the same on Bochs and 86Box: [mega_infault]
    a FATAL fault, [mega_fkn] the packages CLOSED by one. A list, empty
    when clean"""
    inf = m.read(S("mega_infault"), 1)[0]
    fkn = m.read(S("mega_fkn"), 1)[0]
    return (["FATAL fault"] if inf else []) + \
        (["%d package(s) CLOSED" % fkn] if fkn else [])


def glass(m):
    a, n = SCREEN[OPT["adapter"]]
    return bytes(m.read(a, n))

GS_MENU = 1
DRVC_NET = 4
FSXL_DRV = 2
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def run(arm, tmp, syms):
    vbp = os.path.join(tmp, "m.vbp")
    B.demo_project(vbp, bars=2)
    p = P.Project.load(vbp)
    p.set(("enemy", "hp"), 1)
    if arm == "play":                   # (W5: a title picture, kept)
        from PIL import Image, ImageDraw
        im = Image.new("RGB", (640, 480), (0, 0, 0))
        ImageDraw.Draw(im).ellipse((100, 80, 540, 440), fill=(220, 160, 40))
        im.save(os.path.join(tmp, "cover.png"))
        p.set(("cover",), {"image": "cover.png", "line": "Press Enter"})
    p.save()
    herc = OPT["adapter"] == "herc"
    data = B.build_project(p, kind="herc") if herc else B.build_project(p)
    bd = B.Bundle(data)
    vbt = os.path.join(tmp, "BATTLEH.VBT" if herc else "BATTLE.VBT")
    open(vbt, "wb").write(data)
    disk = os.path.join(tmp, "vbt.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                    "--size", "1440", os88build.at("build/vbattle.o88"),
                    vbt], check=True, capture_output=True)
    sysimg = "build/os8088.img"
    if arm == "ems":
        # kern_mega's system disk wanting EMS.DRV (SYSTEM.CFG bit 7), as
        # tests/vidrmbems.py makes it, and a Lo-tech board (the name's _ems)
        sysimg = os.path.join(tmp, "mega.img")
        shutil.copy(os.path.join(os88build.mega_build(), "mega.img"), sysimg)
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                              + b"DW" + bytes([1, 2])
                              + (1 << 7).to_bytes(2, "little") + b"\0\0")
        fat = [sys.executable, "tools/os88fat.py"]
        subprocess.run(fat + ["del", sysimg, "SYSTEM.CFG"],
                       capture_output=True)
        subprocess.run(fat + ["add", sysimg, cfg], check=True,
                       capture_output=True)
    # MACHINES: Bochs's are MartyPC names (os8088_xt_vga[_sb]); 86Box's
    # mr286 shapes have no MartyPC twin, so they carry no os8088_ prefix
    # (docs/86BOX.md) - the port reads the tokens (cga, herc, _sb, _ems)
    if OPT["emu"] == "bochs":
        machine = "os8088_xt_vga_sb" if arm == "card" else "os8088_xt_vga"
    else:
        machine = "at286_" + OPT["adapter"] + \
            {"card": "_sb", "ems": "_ems"}.get(arm, "")
    with os88ui.boot(sysimg, apps=disk, machine=machine) as ui:
        m = ui.m
        S = ui._S
        w = ui.path("B:/VBATTLE.O88")
        W[:] = [w]

        def sel():
            return u16(m.read(geom.winptr(m, w.i, S) + geom.W_SEG, 2))

        def rb(n):
            return m.readseg(sel(), syms[n], 1)[0]

        def rw(n):
            return u16(m.readseg(sel(), syms[n], 2))

        rmode = [0]

        def until(cond, what, guest=120.0):
            def c(_mm):
                if m.read(S("fsx_rmode"), 1)[0]:
                    rmode[0] = 1
                f = faults(m, S)
                if f:
                    raise SystemExit("vbtmega: %s while waiting for %s"
                                     % (f, what))
                return cond()
            os88marty.until(m, c, what, poll=0.2, limit=900.0, guest=guest)

        def key(k):
            m.key(k)
            until(lambda: True, "a key", guest=0.3)

        if arm == "refuse":
            # AT THE CALL: the list's end made FSXL_DRV DRVC_NET, a class
            # the real-mode bracket does not carry - the kernel's FSXR_DRV
            lin = m.lin(sel(), syms["vb_play.run"])
            m.bp_exec(lin)
            m.key("Enter")
            if not m.wait_stop(limit=120):
                raise SystemExit("vbtmega: never reached vb_play.run")
            fsxl = m.readseg(sel(), syms["vb_fsxl"], 24)
            end = fsxl.index(b"\0", 1) + 1          # past the file's NUL
            if fsxl[end] == FSXL_DRV:
                end += 2
            m.write(m.lin(sel(), syms["vb_fsxl"] + end),
                    bytes([FSXL_DRV, DRVC_NET, 0]))
            m.bp_exec()
            m.run()
            until(lambda: rb("vb_ready") == 0 and rw("vb_status") != 0
                  and rw("vb_status") == rw("vb_err"), "the window's word",
                  guest=60.0)
            os88marty.ui_done(m)
            txt = m.readseg(sel(), rw("vb_status"), 40).split(b"\0")[0]
            ok(rmode[0] == 0 and not faults(m, S),
               "refuse: the bracket ran in PROTECTED mode ([fsx_rmode] "
               "never 1), nothing faulted")
            ok(txt.strip() == b"Needs real mode: a driver",
               "refuse: the window says %r" % txt.decode("latin-1"))
            return
        if arm == "play":
            # W5: OS88_HIGH and OS88_HIGHMEM - the region and the picture
            # above 1MB on the desktop (where Bochs's pool has the room)
            rbase = m._desc(sel())[0]
            cv0 = rw("vb_cvseg")
            cbase = m._desc(cv0)[0] if cv0 else 0
            ok(rbase >= 0x100000 and cbase >= 0x100000 and rb("vb_cvon"),
               "W5: the region above 1MB (%06X) and the title picture too "
               "(%06X), up in the window" % (rbase, cbase))
        key("Enter")
        until(lambda: rb("vb_ready") == 1, "the play")
        if arm == "play":
            ok(rw("vb_cvseg") == cv0 and rb("vb_cvon"),
               "W5: the picture KEPT through the play - it costs the play no "
               "conventional byte")
        if arm == "card":
            return card(m, S, rb, rw, until, key, bd, tmp)
        if arm in ("hold", "ems"):
            return hold(m, S, rb, rw, until, key, bd, data, sel,
                        2 if arm == "ems" else 1)
        until(lambda: rb("vb_tplay") == 1, "the title")
        ok(rmode[0] == 1, "1. the play's bracket is REAL MODE "
           "([fsx_rmode] seen 1)")
        title = screens_of(bd, bd.title)

        def looks(n=12):
            hits, best = 0, 1 << 20
            for _ in range(n):
                b8 = glass(m)
                hits += b8 in title
                best = min([best] + [sum(x != y for x, y in zip(t, b8))
                                     for t in title])
                until(lambda: True, "a look", guest=0.15)
            return hits, best
        h, d = looks()
        ok(h >= 6, "1. the first title is the host's decode on the glass "
           "(%d of 12 looks exact, %d bytes off at best)" % (h, d))
        key("Enter")
        until(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
        if os.environ.get("VBTMEGA_SHOT"):
            b8 = glass(m)
            nz = sum(1 for i in range(0, 5) if b8[i] == 0xFF)
            print("   at the menu: B8000[0:5] %s" % b8[:5].hex(), flush=True)
        key("Enter")                    # Attack, at the 1-HP enemy
        until(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
        if os.environ.get("VBTMEGA_SHOT"):
            allf = [f for ci in range(len(bd.clips)) for f in
                    screens_of(bd, ci)]
            for k in range(6):
                b8 = glass(m)
                print("   end loop look %d: %d bytes off the nearest frame"
                      % (k, min(sum(x != y for x, y in zip(f, b8))
                                for f in allf)), flush=True)
                until(lambda: True, "a look", guest=0.3)
        ok(rb("vb_over") == 1, "2. the battle to victory and its end "
           "loop, in the bracket")
        key("Enter")                    # play again: the title
        until(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
              "the play again, at the title", 240)
        if os.environ.get("VBTMEGA_SHOT"):
            from PIL import Image
            fw, fh, rgb = m.fbuf()
            Image.frombytes("RGB", (fw, fh), rgb).save(
                os.environ["VBTMEGA_SHOT"] + "-again.png")
            open(os.environ["VBTMEGA_SHOT"] + "-b8.bin", "wb").write(
                glass(m))
        title = screens_of(bd, bd.title)
        hits = 0
        for _ in range(12):
            hits += glass(m) in title
            until(lambda: True, "a look", guest=0.15)
        ok(hits >= 6, "3. the play again is read in the bracket, and the "
           "title is the host's decode on the glass (%d of 12 looks exact)"
           % hits)
        key("Escape")
        until(lambda: rb("vb_ready") == 0, "Esc to the desktop", 60)
        os88marty.ui_done(m)
        ok(rb("vb_cvback") == 1 and rb("vb_cvon") == 1,
           "W5: the window is the picture as the play returns - no text "
           "layout first")
        ok(rw("vb_err") == 0 and rw("vb_rclm") == 0 and rw("vb_ring") == 0,
           "4. back on the desktop: no error, the rings and the card's "
           "ring given back")
        rmode[0] = 0
        key("Enter")
        until(lambda: rb("vb_ready") == 1 and rb("vb_tplay") == 1,
              "the second play's title")
        ok(rmode[0] == 1 and rw("vb_err") == 0,
           "4. a SECOND play reaches its title, real mode again")
        key("Escape")
        until(lambda: rb("vb_ready") == 0, "Esc again", 60)
        ok(not faults(m, S), "5. no kern_mega FAULT or CLOSED")


def card(m, S, rb, rw, until, key, bd, tmp):
    """W2: the play through Bochs's SB16 - see the module note"""
    ring = rw("vb_ring")
    until(lambda: rb("vb_tplay") == 1, "the title")
    key("Enter")
    until(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
    until(lambda: True, "the music", guest=8.0)
    key("Enter")                    # Attack: its own sound
    until(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
    def rd(n):
        return struct.unpack("<I", m.readseg(
            u16(m.read(geom.winptr(m, W[0].i, S) + geom.W_SEG, 2)),
            SYMS[n], 4))[0]
    st = dict(cons=rd("vb_cons32"), hper=rd("vb_hper"),
              ring=ring, under=rw("vb_munder"), nunder=rw("vb_nunder"),
              err=rw("vb_err"), reopen=rw("vb_sreopen") if "vb_sreopen"
              in SYMS else None)
    key("Escape")
    until(lambda: rb("vb_ready") == 0, "Esc to the desktop", 60)
    wav = os.path.join(m.box.out, "sb.wav") if hasattr(m, "box") else ""
    cap = open(wav, "rb").read() if os.path.exists(wav) else b""
    keep = os.environ.get("VBTMEGA_WAV")
    if keep:
        open(keep, "wb").write(cap)
    print("   card: %r, capture %d bytes" % (st, len(cap)), flush=True)
    ok(st["ring"] and st["err"] == 0 and st["under"] == 0
       and st["nunder"] == 0,
       "card: the card's ring claimed (%04X), the music never dry, the card "
       "never found halted" % st["ring"])
    ok(st["cons"] > 5 * bd.rate, "card: the card consumed %.1f s of the "
       "stream in the bracket" % (st["cons"] / float(bd.rate)))
    if OPT["emu"] != "bochs":
        return                      # (86Box's capture is not this one)
    byrate = music_heard(cap, bd)
    best = max(byrate, key=byrate.get)
    ok(best == 4 and byrate[4] >= 0.55 and
       all(v < byrate[4] - 0.15 for k, v in byrate.items() if k != 4),
       "card: the capture is the battle's music - its best match %.2f at "
       "the bundle's own rate (44,100 / %d), %s elsewhere"
       % (byrate[4], bd.rate, ", ".join("%.2f" % v for k, v in
                                       sorted(byrate.items()) if k != 4)))


def hold(m, S, rb, rw, until, key, bd, data, sel, htype):
    """W3/W4: the bundle HELD - XMS (htype 1) or EMS (2) - module note"""
    m.write(m.lin(sel(), SYMS["vb_xyb"]), struct.pack("<H", 256))
    ok(rb("vb_hon") == 1 and rb("vb_htype") == htype and rb("vb_hrn") == 1
       and rw("vb_hkb4") * 4096 >= len(data),
       "hold: opened at the door - %s, one range, the whole bundle (%d KB)"
       % ("XMS" if htype == 1 else "EMS", rw("vb_hkb4") * 4))
    until(lambda: rb("vb_tplay") == 1, "the title")
    until(lambda: rb("vb_hfull") == 1, "the hold to fill", 300)
    if htype == 2:
        ok(rb("vb_hinp") == 1 and rb("vb_eq") == 0x0F,
           "hold: EMS.DRV carried low (FSXL_DRV DRVC_EMS), filled through "
           "its twin, and READ IN PLACE through the frame (all four quarters "
           "ours, the board %.1fx RAM's time)" % (rw("vb_er10") / 10.0))
    else:
        ok(rb("vb_hinp") == 1 and rb("vb_tier") >= 1,
       "hold: filled in the bracket through OSAPI_XMEM_COPY's twin, and "
       "READ IN PLACE (tier %d: %s) - its 16 bytes in place matched the "
       "twin's copy" % (rb("vb_tier"), "a 286's LOADALL" if rb("vb_tier")
                        == 1 else "a 386's loaded base"))
    r0, i0, y0 = rw("vb_rcalls"), rw("vb_icalls"), rw("vb_iyc")
    key("Enter")
    until(lambda: rb("vb_state") == GS_MENU, "the menu", 240)
    key("Enter")
    until(lambda: rb("vb_eplay") == 1, "victory's end loop", 240)
    key("Enter")
    until(lambda: rb("vb_tplay") == 1 and rb("vb_over") == 0,
          "the play again, at the title", 240)
    title = screens_of(bd, bd.title)
    hits = 0
    for _ in range(12):
        hits += glass(m) in title
        until(lambda: True, "a look", guest=0.15)
    nr = (rw("vb_rcalls") - r0) & 0xFFFF
    ni = (rw("vb_icalls") - i0) & 0xFFFF
    ny = (rw("vb_iyc") - y0) & 0xFFFF
    ok(nr == 0 and ni > 0 and (ny > 0 or htype == 2),
       "hold: from the fill on, a battle and the play again with NO disk "
       "read (%d), records read in place %d, the decode yielding %d times%s"
       % (nr, ni, ny, " (EMS: the hook maps, nothing yields)"
          if htype == 2 else ""))
    ok(hits >= 6, "hold: the play again's title is the host's decode, read "
       "in place (%d of 12 looks exact)" % hits)
    key("Escape")
    until(lambda: rb("vb_ready") == 0, "Esc to the desktop", 60)
    os88marty.ui_done(m)
    key("Enter")
    until(lambda: rb("vb_ready") == 1 and rb("vb_tplay") == 1,
          "the second play's title")
    ok(rb("vb_hinp") == 1 and rw("vb_err") == 0,
       "hold: a SECOND bracket checks the read in place again and keeps it")
    key("Escape")
    until(lambda: rb("vb_ready") == 0, "Esc again", 60)
    ok(not faults(m, S), "hold: no kern_mega FAULT or CLOSED")


def music_heard(cap, bd):
    """{step: the best normalised correlation of a 0.36 s window of the
    bundle's music against Bochs's capture (44,100 Hz, 16-bit stereo; its
    first channel) resampled by STEP}. Bochs's own resampler softens a
    square-ish synth, so a clean copy scores ~0.6-0.7 here; the TEST is that
    the bundle's rate (step 4) stands clear of every other."""
    import numpy as np
    if b"data" not in cap or len(cap) < 200000:
        return {k: 0.0 for k in (2, 3, 3.5, 4, 4.5, 5, 6)}
    d = cap[cap.index(b"data") + 8:]
    a = np.frombuffer(d[:len(d) // 4 * 4], dtype="<i2").reshape(-1, 2)
    a = a[:, 0].astype(float)
    mus = np.frombuffer(bd.mus, dtype=np.uint8).astype(float) - 128
    win = mus[bd.rate:bd.rate + 4000]
    win = win - win.mean()
    n = len(win)
    out = {}
    for step in (2.0, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0):
        c = np.interp(np.arange(0, len(a) - 1, step), np.arange(len(a)), a)
        c = c - c.mean()
        cc = np.correlate(c, win, "valid")
        cs = np.sqrt(np.convolve(c * c, np.ones(n), "valid"))
        out[int(step) if step == int(step) else step] = float(
            (cc / (cs * np.linalg.norm(win) + 1e-9)).max())
    return out


CARD = {}
W = []
SYMS = {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("play", "refuse", "card", "hold",
                                      "ems", "both"),
                    default="both")
    ap.add_argument("--emu", choices=("bochs", "86box"), default="bochs")
    ap.add_argument("--adapter", choices=("vga", "cga", "herc"),
                    default=None)
    a = ap.parse_args()
    OPT.update(emu=a.emu, adapter=a.adapter or
               ("vga" if a.emu == "bochs" else "cga"))
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",))
    SYMS.update(syms)
    if a.arm == "both" and a.emu == "bochs":
        legs = [(x, "vga") for x in ("play", "refuse", "card", "hold")]
    elif a.arm == "both":               # W4: 86Box's mr286 - the reset path
        legs = [("play", "cga"), ("play", "herc"), ("card", "cga"),
                ("hold", "cga"), ("ems", "cga")]
    else:
        legs = [(a.arm, OPT["adapter"])]
    for arm, adapter in legs:
        OPT["adapter"] = adapter
        print("-- %s, %s, %s" % (arm, adapter, a.emu), flush=True)
        tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
        try:
            run(arm, tmp, syms)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtmega: %d FAILED" % len(fails))
        return 1
    print("vbtmega: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
