#!/usr/bin/env python3
"""VIDEO.O88 decodes a file held in XMS IN PLACE - SPEC.md 98.3.18.11,
98.3.18.14.

    make && python3 tests/vidxip.py [--arm silent|snd|ahead|bank...] [--sim]

QEMU IS A 386, AND SINCE 98.3.18.14 A 386 GOES IN PLACE TOO: a borrow there
loads DS's hidden base through a moment of protected mode (apps/os88ub.inc)
where a 286 executes LOADALL. So the in-place play is the SHIPPED player
running the shipped instruction sequence - every part of it but the 286's
instruction itself, which is proven on the owner's 286 by VIDDISK L and
XMEM.DRV's attach (SPEC.md 41.13): vp_xipq's choice, the reader noting
offsets and copying nothing, vp_addr's handles, every VP_DSDX/VP_DSEND site,
the yields at vdec.inc's live sites, the session's end turning them off.

`--hd` puts the clip and the package on an IDE DISK instead of B: - QEMU's
SeaBIOS knows it, so HDD.DRV's rung 1 takes it (SPEC.md 52.1.1) - and so
every fill is ARMED and reads straight into XMS (SPEC.md 98.3.18.15,
52.1.4): both plays must report sectors that skipped the copy up
([vp_hsk]), and the bank arms' holds against the decode are what say the
bytes that went straight up are the right ones. `--noharm` is its A/B: the
player built VP_NOHARM, which never arms, and [vp_hsk] must stay 0. The
hold arms keep the disk (an IDE disk cannot be blanked); the hold still
plays from XMS whole. Broken on purpose - hd_xa_read's `rep insw` taken
out, so a redirected sector writes nothing - the bank arms' holds fail.

`--xspeed` builds the in-place play VP_XSPTEST, which takes vp_xspeed's
measurement (SPEC.md 98.3.18.15: the pool's KB copied, in tenths of RAM's
read) at the hold's open - only an EMS board asks for it otherwise, and no
emulator here has one beside XMS. [vp_xr10] must have been measured (its
VALUE is QEMU's host timing, not a machine's), and the 8 KB block it timed against must have been given back
(XMEM.DRV's own table, xmcheck's reader). Broken on purpose - vp_xspeed's
OSAPI_XMEM_FREE taken out - the block outlives the measurement and it fails.

`--sim` runs the in-place play on the LA_SIM build instead - a test build,
never shipped, in which a borrow COPIES the 64 KB window out of XMS into a
shadow through a flat FS. It was this row's only in-place play while a 386
copied; it is kept so the stand-in still assembles and still plays.

TWO BOOTS, ONE FLOW: the shipped player COPYING - `[vp_noinp]` written 1
before Play, the gate's A/B, which refuses in place on every tier - then the
shipped player in place. Each opens a 640 x 200 one-bit clip in the VGA's
own layout (so it decodes natively, not through the shadow in-place
refuses), lets the window's timer load it whole into XMS, makes B: a BLANK
floppy - nothing can come off the disk - and plays it to the last frame. The
in-place play must have been IN PLACE ([vp_xip] seen 1, [vp_xchk] 1), draw
every frame with no error, and leave the window's pixels identical to the
copying play's.

ARM snd adds QEMU's SB16 and a SYSTEM.CFG that wants SOUND.DRV, and a clip
with sound: the audio is then copied to the card's ring out of the hold,
inside a borrow (vp_aput's VP_DSDX), every frame.

Broken on purpose (each measured): vp_xaddr returning the chunk's slot
offset without the hold's base draws garbage and the windows differ; with
vd_ypatch's `mov word [bx], 0x9090` taken out the play is still right but
no yield opens a window ([vp_simyc], counted in every build) - the row also
asserts the yields ran. And with vp_ubld's `call ub_set` taken out the
in-place play reads the package's own bytes as the clip and fails on the
16-byte check (vp_xok), so the play copies and the row fails on it.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88qemu                                             # noqa: E402
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
import os88vid as vid                                       # noqa: E402
import xmcheck                                              # noqa: E402
from cycweb import pkg_syms                                 # noqa: E402
from pxswin import qpoke                                     # noqa: E402
from vidxms import Q, u16, u32                              # noqa: E402

NF, FPS = 240, 24.0
HOLDN = 10                              # the bank arms' hold: every 10 frames
WB, H = 80, 200


def level(f):
    """--arm ahead's sound: frame f's 459 samples all one level, distinct
    from its neighbours' and never 80h, the card's silence"""
    v = 1 + f % 250
    return v + 1 if v >= 0x80 else v


def clip(tmp, snd, ahead=0, lev=False):
    import random
    rnd = random.Random(1986)
    cv = bytearray(WB * H)
    paths = []
    for f in range(NF):
        for _ in range(300):
            a = rnd.randrange(WB * H - 8)
            cv[a:a + 8] = bytes(rnd.getrandbits(8) for _ in range(8))
        if f % 40 == 20:                    # a big frame now and then:
            for a in range(0, WB * H, 3):   # a record past VP_XYB, so the
                cv[a] ^= 0x5A               # yields have work
        p = os.path.join(tmp, "f%03d.pbm" % f)
        vid._write_pbm(p, WB, H, bytes(cv))
        paths.append(p)
    wav = None
    if snd:
        wav = os.path.join(tmp, "s.wav")
        with wave.open(wav, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(11025)
            n = int(11025 * NF / FPS)
            if lev:
                spf = round(11025 / FPS)
                w.writeframes(bytes(level(i // spf) for i in range(NF * spf)))
            else:
                w.writeframes(bytes((128 + (40 if (i // 25) % 2 else -40))
                                    & 0xFF for i in range(n)))
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, wav, "lin80", "vidxip clip",
                      ahead=ahead)
    vid.verify_v88(out)
    return out


LASIM = False                           # --sim: the in-place play on LA_SIM
HD = False                              # --hd: the clip on an IDE disk
NOHARM = False                          # --noharm: never armed (VP_NOHARM)
XSPEED = False                          # --xspeed: the in-place play
                                        # VP_XSPTEST


def build(tmp, sim, defs=()):
    b = os.path.join(tmp, "video%s.bin" % ("sim" if sim else ""))
    o = os.path.join(tmp, "sim" if sim else "ship", "VIDEO.O88")
    sim = sim and LASIM
    os.makedirs(os.path.dirname(o), exist_ok=True)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/"]
                   + (["-DLA_SIM"] if sim else [])
                   + ["-D%s" % d for d in defs]
                   + ["-o", b, "apps/video/video.asm"], check=True)
    subprocess.run([sys.executable, "tools/os88pkg.py", b, "-o", o],
                   check=True, capture_output=True)
    return o


def one(tmp, sim, snd, v88, bank=False, ekr=0, dry=0):
    tag = "sim" if sim else "ship"      # (sim: the IN-PLACE play)
    defs = ("VP_RDSLOW=%d" % dry,) if dry and sim else ()
    if NOHARM:
        defs = defs + ("VP_NOHARM",)
    if XSPEED and sim:
        defs = defs + ("VP_XSPTEST",)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",),
                       (("LA_SIM",) if sim and LASIM else ()) + defs)
    pkg = build(tmp, sim, defs)
    d = os.path.join(tmp, tag)
    disk = os.path.join(d, "d.img")
    blank = os.path.join(d, "blank.img")
    sysimg = os.path.join(d, "sys.img")
    shutil.copy(os88build.at("build/os8088.img"), sysimg)
    extra = []
    if snd:
        cfg = os.path.join(d, "SYSTEM.CFG")
        with open(cfg, "wb") as f:
            f.write(b"O88CFG\0\0" + struct.pack("<H", 3) + b"DW"
                    + bytes([1, 2]) + struct.pack("<H", 1) + b"\0\0")
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        cfg], check=True, capture_output=True)
        extra = ["-audiodev", "wav,id=snd,path=%s" % os.path.join(d, "o.wav"),
                 "-device", "sb16,audiodev=snd"]
    for out, files in ((disk, [] if HD else [v88, pkg]), (blank, [])):
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                        "--size", "1440"] + files, check=True,
                       capture_output=True)
    if HD:                              # THE CLIP ON C:, rung 1's
        hdimg = os.path.join(d, "hd.img")
        subprocess.run([sys.executable, "tools/os88hdd.py", "--raw",
                        "--noboot", "--out", hdimg, "--heads", "16",
                        "--spt", "63", "--cyls", "130",
                        "--file", "CLIP.V88=" + v88,
                        "--file", "VIDEO.O88=" + pkg],
                       check=True, capture_output=True)
        extra = extra + ["-drive", "file=%s,format=raw,if=ide,index=0,"
                         "media=disk" % hdimg]
    q = Q(d, sysimg, disk, 2 if bank else 128, extra)
    res = {}
    try:
        os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(q.sock, "vidxip")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.02):
                raise SystemExit("vidxip (%s): timed out waiting for %s"
                                 % (tag, what))
        drv = "C" if HD else "B"
        x, y = geom.drive_pt(q, drv, S)
        wbefore = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and (ui.fs_of(w) == (1, 0, 1) if not HD
                                  else w.i not in wbefore):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive %s's window" % drv, 30)
        os88qemu.pace(q, 1)
        w = box["w"]
        i, _ = ui.entry("CLIP.V88", w)
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))

        def player():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        wait(player, "the Video Player's window", 30)
        wi = box["p"].i
        rec = q.read(S("wm_wins") + wi * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rb(n):
            return q.read(base + syms[n], 1)[0]

        def rw(n):
            return u16(q.read(base + syms[n], 2))

        def rd(n):
            return u32(q.read(base + syms[n], 4))
        wait(lambda: rb("vp_loaded") == 1, "the clip's header", 30)
        if rb("vp_ok") != 1:
            raise SystemExit("vidxip (%s): the player will not play it" % tag)
        if not sim:                     # THE COPYING PLAY: the gate's A/B,
            qpoke(q, [(base + syms["vp_noinp"], b"\1")])   # every tier
        if bank:                        # BIGGER THAN THE POOL: a bank, and
            wait(lambda: rb("vp_xon") == 1 and rb("vp_xbank") == 1,
                 "the bank", 60)        # B: stays, the play reads it
        else:
            wait(lambda: rb("vp_xon") == 1 and rb("vp_xfull") == 1,
                 "the hold to be whole", 120)
            if not HD:                  # (an IDE disk cannot be blanked)
                q.hmp("change floppy1 %s raw" % blank)
            if XSPEED and sim:          # vp_xspeed's answer, and its block
                res["xr10"] = rw("vp_xr10")     # given back: only the
                res["blk8"] = [b for b in xmcheck.blocks(   # hold is live
                    q.sock, xmcheck.table_base(q.sock)) if b[2] == 8]
        os88qemu.pace(q, 1)
        if ekr:                         # a gate's cap on the ring in place
            qpoke(q, [(base + syms["vp_ekr"], struct.pack("<H", ekr))])
        stops = list(range(HOLDN, NF, HOLDN)) if bank and not dry else []
        if stops:                       # the gate's hold (tests/vidplay.py's)
            qpoke(q, [(base + syms["vp_stopat"], struct.pack("<H", stops[0]))])
        q.hmp("sendkey p")
        wait(lambda: rb("vp_ready") == 1, "the play to start", 30)
        import time
        t0 = time.time()
        xip = max(rb("vp_xip"), rb("vp_einp") == 2)   # (sampled now: a
        krm = rw("vp_kr")               # bank arm's holds run nearly to the end)
        # EVERY HOLDN FRAMES, the screen against the reference decode: this
        # clip's patches overwrite a wrong byte within ~30 frames, so the
        # window after the play cannot see a bank's wrap read wrong
        diffs = []
        if stops:
            rd88 = vid.Reader(v88)
            tg = vid.Geom(vid.LAY_LIN80, WB, 480)
            ty0 = (480 - H) // 2 // tg.banks * tg.banks
            row0 = tg.base[ty0]
        for k, n in enumerate(stops):
            wait(lambda: rb("vp_held") == 1 and rw("vp_done") == n,
                 "the hold before frame %d" % n, 60)
            raw = q.read(0xA0000 + row0, WB * H)
            got = b"".join(raw[tg.base[ty0 + yy] - row0:
                               tg.base[ty0 + yy] - row0 + WB]
                           for yy in range(H))
            want = vid.decode_at(rd88, n - 1)
            diffs.append((n, sum(1 for j in range(len(got))
                                 if got[j] != want[j])))
            nxt = stops[k + 1] if k + 1 < len(stops) else 0xFFFF
            qpoke(q, [(base + syms["vp_stopat"], struct.pack("<H", nxt)),
                      (base + syms["vp_held"], b"\0")])
        yields = 0
        lim = syms["vd_ylim"]
        while True:
            xip = max(xip, rb("vp_xip"), rb("vp_einp") == 2)
            krm = max(krm, rw("vp_kr"))     # (the session's: its end resets it)
            if u16(q.read(base + lim, 2)) != 0xFFFF:
                yields += 1                 # (a borrow open, mid-decode)
            if rb("vp_ready") == 0 and rb("vp_played") == 1:
                break
            if not os88qemu.acted(q, lambda: True, secs=1, what="a tick",
                                  poll=0.01):
                break
            if dry and time.time() - t0 > 240:
                res["hung"] = rw("vp_done")     # (a play that never ends:
                break                           # the freeze, at this frame)
        res.update(secs=time.time() - t0)
        res.update(diffs=diffs, done=rw("vp_done"), err=rb("vp_err"), xip=xip,
                   chk=rb("vp_xchk"), snd=rb("vp_snd"), yields=yields,
                   ywin=rw("vp_simyc"), lbk=rb("vp_lbk"),
                   hsk=rd("vp_hsk"), hnone=rb("vp_hnone"),
                   ahead=rb("vp_ahead"), xbn=rw("vp_xbn"),
                   kr=krm,
                   sites=u16(q.read(base + syms["..@vd_y1"], 2))
                   if "..@vd_y1" in syms else None)
        os88qemu.pace(q, 1)
        rec = q.read(S("wm_wins") + wi * geom.WIN_SIZE, geom.WIN_SIZE)
        shot = os.path.join(d, "win.png")
        subprocess.run([sys.executable, "tools/shot.py", q.sock, shot],
                       check=True, capture_output=True)
        from PIL import Image
        im = Image.open(shot).convert("L")
        wx, wy = u16(rec, geom.W_X), u16(rec, geom.W_Y)
        ww, wh = u16(rec, geom.W_W), u16(rec, geom.W_H)
        res["pix"] = im.crop((wx, wy + 20, wx + ww, wy + wh)).tobytes()
    finally:
        q.close()
    res["wav"] = os.path.join(d, "o.wav") if snd else None
    return res


def levels(path):
    """The card's output as the frame levels it held, in order: a run of
    one level 100 samples long or more, silence (80h) and repeats dropped -
    a card that ran dry is a gap, not a frame"""
    with wave.open(path, "rb") as w:
        sw, ch = w.getsampwidth(), w.getnchannels()
        raw = w.readframes(w.getnframes())
    if sw == 2:
        vals = [(struct.unpack_from("<h", raw, i)[0] >> 8) + 128
                for i in range(0, len(raw), 2 * ch)]
    else:
        vals = list(raw[::ch])
    out, run, cur = [], 0, None
    for v in vals:
        if v == cur:
            run += 1
            if run == 100 and v != 0x80 and (not out or out[-1] != v):
                out.append(v)
        else:
            cur, run = v, 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("silent", "snd", "ahead", "bank",
                                      "bankwrap", "banksnd", "bankdry",
                                      "bankdrysnd"),
                    default="silent")
    ap.add_argument("--sim", action="store_true",
                    help="the in-place play on the LA_SIM test build")
    ap.add_argument("--hd", action="store_true",
                    help="the clip on an IDE disk: fills read straight "
                         "into XMS (98.3.18.15)")
    ap.add_argument("--noharm", action="store_true",
                    help="with --hd: the VP_NOHARM build, never armed")
    ap.add_argument("--xspeed", action="store_true",
                    help="the in-place play VP_XSPTEST: vp_xspeed measured")
    a = ap.parse_args()
    os.chdir(ROOT)
    global NF, LASIM, HD, NOHARM, XSPEED
    LASIM, HD, NOHARM, XSPEED = a.sim, a.hd, a.noharm, a.xspeed
    snd = a.arm not in ("silent", "bank", "bankwrap", "bankdry")
    bank = a.arm.startswith("bank")
    # BANKDRY: the bank in place RUN DRY (the owner's Victory at 14 wait
    # states, 2026-10-16: garbage, then a freeze, at a frame that moved with
    # the machine's speed) - a reader VP_RDSLOW ticks a chunk, slower than
    # the stream, and no holds to let it catch up
    dry = 12 if a.arm.startswith("bankdry") else 0
    ekr = 3 if a.arm in ("bankwrap", "banksnd", "bankdry",
                         "bankdrysnd") else 0       # (THE MIRROR, read
                                                        # every lap)
    if bank:
        NF = 340 if snd else 400        # ~1.3 MB: past a 2 MB machine's pool,
                                        # and round its bank, and a 1.44 MB
                                        # floppy still holds it with sound
    ahead = 8 if a.arm == "ahead" else 0
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, snd, ahead, a.arm == "ahead")
        print("   clip: %d frames, %d bytes, %s"
              % (NF, os.path.getsize(v88), "with sound" if snd else "silent"))
        ship = one(tmp, False, snd, v88, bank, ekr, dry)
        print("   copying play: done %d err %d xip %d snd %d"
              % (ship["done"], ship["err"], ship["xip"], ship["snd"]))
        sim = one(tmp, True, snd, v88, bank, ekr, dry)
        if dry:
            print("   dry: the in-place play took %.1f s for a %.1f s clip "
                  "(the copying %.1f)%s" % (sim["secs"], NF / FPS,
                                            ship["secs"],
                  "; HUNG at frame %d" % sim["hung"] if "hung" in sim
                  else ""))
            if "hung" in sim:
                bad.append("the dry in-place play never ended: frame %d of "
                           "%d after 240 s" % (sim["hung"], NF))
            if sim["secs"] < 1.3 * NF / FPS:
                bad.append("the dry arm never ran dry (%.1f s for %.1f): "
                           "the leg tests nothing"
                           % (sim["secs"], NF / FPS))
        print("   in-place play (%s): done %d err %d xip %d check %d "
              "snd %d; %d yields opened a window"
              % ("LA_SIM" if LASIM else "386", sim["done"], sim["err"],
                 sim["xip"], sim["chk"], sim["snd"], sim["ywin"]))
        if ship["xip"]:
            bad.append("the copying play went in place: [vp_noinp] ignored")
        want = (1, 8) if bank else (4, 7)
        if (ship["lbk"], sim["lbk"]) != want:
            bad.append("the card's bank line: %d copying, %d in place, not "
                       "%d and %d" % ((ship["lbk"], sim["lbk"]) + want))
        if bank:
            print("   the bank: %d slots, %d of them the ring's in place"
                  % (sim["xbn"], sim["kr"]))
            for tag, r in (("copying", ship), ("in-place", sim)):
                wrong = [(n, d) for n, d in r["diffs"] if d]
                print("   %s play: %d holds against the decode, %d wrong%s"
                      % (tag, len(r["diffs"]), len(wrong),
                         " %s" % wrong[:4] if wrong else ""))
                if wrong or not (r["diffs"] or dry):
                    bad.append("the %s play's screen against the decode: "
                               "%d of %d holds wrong, first %s"
                               % (tag, len(wrong), len(r["diffs"]),
                                  wrong[:1]))
            if os.path.getsize(v88) <= sim["kr"] * 32768:
                bad.append("the clip does not go round the bank: the mirror "
                           "is never read")
        if ship["done"] != NF or ship["err"]:
            bad.append("the copying play drew %d of %d (error %d)"
                       % (ship["done"], NF, ship["err"]))
        if sim["xip"] != 1 or sim["chk"] != 1:
            bad.append("the in-place play was not in place (xip %d, check %d)"
                       % (sim["xip"], sim["chk"]))
        if sim["done"] != NF or sim["err"]:
            bad.append("the in-place play drew %d of %d (error %d)"
                       % (sim["done"], NF, sim["err"]))
        if sim["ywin"] < NF // 40:
            bad.append("only %d yields opened a window: the big frames' "
                       "records are past VP_XYB, so the decode did not yield"
                       % sim["ywin"])
        if XSPEED:
            xr = sim.get("xr10")
            print("   vp_xspeed: an XMS KB copied = %s tenths of RAM's read; "
                  "%d 8 KB block(s) left live" % (xr, len(sim.get("blk8", []))))
            if xr is None or xr == 0xFFFF:   # (QEMU cannot TIME: the value
                bad.append("vp_xspeed never measured (%s)" % xr)   # is the
                                        # host's, 0 to 41 seen; that it was
                                        # taken, and given back, is the row)
            if sim.get("blk8"):
                bad.append("vp_xspeed's 8 KB block outlived it: %s"
                           % sim["blk8"])
        if HD:
            for tag, r in (("copying", ship), ("in-place", sim)):
                print("   %s play: %d sectors read straight into XMS "
                      "(door %s)" % (tag, r["hsk"],
                                     "refused" if r["hnone"] else "open"))
                if NOHARM and r["hsk"]:
                    bad.append("the %s play skipped %d copies built "
                               "VP_NOHARM" % (tag, r["hsk"]))
                if not NOHARM and r["hsk"] == 0:
                    bad.append("the %s play read nothing straight into XMS "
                               "off rung 1 (door %s)"
                               % (tag, "refused" if r["hnone"] else "open"))
        if snd and sim["snd"] != 1:
            bad.append("the in-place play had no card (snd %d)" % sim["snd"])
        if sim["sites"] is not None and sim["sites"] != 0x0AEB:
            bad.append("a yield site left live after the session (%04X)"
                       % sim["sites"])
        if ship["pix"] != sim["pix"]:
            n = sum(1 for x, y in zip(ship["pix"], sim["pix"]) if x != y)
            bad.append("the window after the play differs in %d pixels" % n)
        else:
            print("   the window after both plays: identical, %d pixels"
                  % len(sim["pix"]))
        if a.arm == "ahead":
            # QEMU's SB16 drops the head of a stream (frame 0's sound, on
            # the copying play as on this one; tests/vidsound.py has
            # MartyPC's card play it from the first sample) and may repeat
            # a block past the end, so the levels are held from frame 1 to
            # the last - and the two plays' to each other, whole
            want = [level(f) for f in range(NF)]
            got = {}
            for tag, r in (("copying", ship), ("in-place", sim)):
                g = levels(r["wav"])
                if want[-1] in g:
                    g = g[:g.index(want[-1]) + 1]
                got[tag] = g
                if r["ahead"] != ahead:
                    bad.append("the %s play read sound ahead %d, not %d"
                               % (tag, r["ahead"], ahead))
                if g[-(NF - 1):] != want[1:] or len(g) > NF:
                    i = next((k for k, (x, y) in enumerate(zip(g[1:] if
                              len(g) == NF else g, want[1:])) if x != y), -1)
                    bad.append("the %s play's sound: %d frame levels, "
                               "frames 1-%d not whole and in order (first "
                               "wrong at %d)" % (tag, len(g), NF - 1, i + 1))
            if got["copying"][-(NF - 1):] == want[1:] \
                    and got["in-place"][-(NF - 1):] == want[1:]:
                print("   the sound, sound ahead %d: frames 1-%d whole and in "
                      "order on both plays" % (ahead, NF - 1))
    for b in bad:
        print("   FAIL  " + b)
    print("vidxip (%s): %s" % (a.arm, "ok" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
