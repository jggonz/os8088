#!/usr/bin/env python3
"""os88vbt - the host half of Video Battle (SPEC.md 125): a .VBT bundle.

A battle played as joined video clips. The bundle holds an idle loop, a
set of action clips (each carrying the sound effects only it uses), the
menu's own sounds, the music and a battle script, laid out for the engine
(apps/vbattle/). Every frame is a record of the Video Player's ten lists
(SPEC.md 98.1.3, os88vid.to_lists), decoded by apps/video/vdec.inc. Every
JOIN is exact by construction: the records are made between the screens
the decoder will really hold, so

  - the idle loop's record 0 takes its last frame to its first;
  - an action's ENTRY records (one per anchor frame of the idle loop) take
    that idle frame's screen to the action's first frame;
  - an action's last record is a SEAM onto idle frame EXIT, and the loop
    goes on at EXIT + 1.

Commands:
    os88vbt.py demo OUT.VBT [--bars N] [--preview DIR]   the original battle
    os88vbt.py info FILE.VBT
    os88vbt.py verify FILE.VBT        decode every path, check every join

Format version 2 (SPEC.md 125.2), little-endian:
    0             the header
    512           the clip table, 48 bytes a clip
    2048          the battle script
    then, each on 4 KB: the VIDEO RESIDENT block, the MUSIC (streamed by
                  the engine), the MENU's effects (resident), and each
                  clip's region - its own effects, then its records
"""

import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88vid as V  # noqa: E402

MAGIC = b"VBT1"
VERSION = 2
SECTOR = 512
ALIGN = 4096        # every region starts on 4 KB: OSAPI_FILE_READ_SEQ seeks
                    # and reads in CLUSTER multiples, and 4 KB covers every
                    # floppy and a FAT16 hard disk to 128 MB
REGION_MAX = 60 * 1024  # a region is read into one claim's piece, in one
                    # capacity word, rounded up to ALIGN
MAX_CLIPS = 32
CLIP_TAB = 512
CLIP_SZ = 48
SCRIPT_AT = 2048
META = 4096         # the header, the clip table and the script: one read
MAX_SFX = 16
MUSIC_MAX = 512 * 1024 * 1024   # (a 32-bit length; the engine streams it)
MUSIC_RES = 61440   # music this short the engine holds resident (VB_MRES)

# clip flags
CF_JOINS = 1        # the last record is a seam onto idle frame EXIT
CF_FULL = 2         # a full screen clip (the intro): 200 rows
CF_ENDS = 4         # the battle ends on it; its last frame is held

NO_SFX = 0xFF

# the script's command kinds
K_HIT = 0           # damage the enemy
K_HEAL = 1          # heal the whole party

# the menu's sounds, header bytes 24..26 (125.2.1)
UI_SOUNDS = ("tick", "ok", "deny")

ROWS_FULL = 200


def pad(b, n=SECTOR):
    return bytes(b) + bytes(-len(b) % n)


def surface_of(px, rows):
    """A 64 KB CGA surface image (layout cga, 2 banks of 80 bytes a row)
    from pixel indexes 0..3, `rows` x 320"""
    s = bytearray(65536)
    p = px.astype(np.uint8)
    packed = (p[:, 0::4] << 6) | (p[:, 1::4] << 4) | (p[:, 2::4] << 2) | p[:, 3::4]
    for y in range(rows):
        b = (y & 1) * 8192 + (y >> 1) * 80
        s[b:b + 80] = packed[y].tobytes()
    return s


GEOMS = {}


def geom(rows):
    if rows not in GEOMS:
        GEOMS[rows] = V.Geom(V.LAY_CGA, 80, rows)
    return GEOMS[rows]


def rec(a, b, rows):
    """The record that takes surface a to surface b: a length word, then
    the ten lists (SPEC.md 98.1.3)"""
    ops = V.seam_ops(a, b, geom(rows))
    lists = V.to_lists(ops)[0]
    if len(lists) + 2 > 65000:
        raise ValueError("a record of %d bytes" % len(lists))
    return struct.pack("<H", len(lists) + 2) + lists


def decode(surf, r):
    n = struct.unpack_from("<H", r, 0)[0]
    used = V.decode_lists(surf, r[2:n])
    if used != n - 2:
        raise ValueError("a record's lists end at %d of %d" % (used, n - 2))


def music_bytes(x):
    """The music, held to 64..191 so that music + an effect never leaves
    0..255 (125.2): mixing is one add a sample"""
    return np.clip(np.round(x * 63.5 + 127.5), 64, 191).astype(np.uint8).tobytes()


def sfx_bytes(x):
    """An effect, signed, -64..63"""
    return np.clip(np.round(x * 63.5), -64, 63).astype(np.int8).tobytes()


class Battle:
    """What a bundle is made of, in host terms"""

    def __init__(self, cgapal, rows, fps, rate):
        self.cgapal, self.rows, self.fps, self.rate = cgapal, rows, fps, rate
        self.idle = []          # pixel frames
        self.anchors = []       # idle frame numbers an action may start at
        self.clips = []         # dicts: name, frames, cues {frame: sfx name},
                                # joins, exit, full, ends
        self.music = None       # float array, -1..1
        self.sfx = {}           # name -> float array: every effect
        self.ui = {}            # 'tick'/'ok'/'deny' -> an effect's name
        self.script = None      # dict


def build(b):
    rows = b.rows
    idle_s = [surface_of(f, rows) for f in b.idle]
    n = len(idle_s)
    if n < 2:
        raise ValueError("an idle loop of %d frames" % n)
    # --- the video resident block: the idle loop's records, then entries
    vres = bytearray()
    idle_off = []
    for i in range(n):
        idle_off.append(len(vres))
        vres += rec(idle_s[i - 1], idle_s[i], rows)
    clip_rows = []
    regions = []
    for c in b.clips:
        full = c.get("full", False)
        crow = ROWS_FULL if full else rows
        fs = [surface_of(f, crow) for f in c["frames"]]
        ent = []
        if not full:
            for k in b.anchors:
                ent.append(len(vres))
                vres += rec(idle_s[k], fs[0], rows)
        # the clip's own effects first, so they are in memory before any of
        # its records - a clip may start before it is all read (125.4)
        cues = sorted(c.get("cues", {}).items())[:2]
        effs, where = bytearray(), {}
        for _, name in cues:
            if name not in where:
                d = sfx_bytes(b.sfx[name])
                where[name] = (len(effs), len(d))
                effs += d
        rec_at = len(effs)
        body = bytearray(effs)
        prev = bytearray(65536) if full else fs[0]
        nrec = 0
        if full:                        # (its frame 0 too: it starts on black)
            body += rec(prev, fs[0], crow)
            prev = fs[0]
            nrec = 1
        for f in fs[1:]:                # (an action's frame 0 is its entry's)
            body += rec(prev, f, crow)
            prev = f
            nrec += 1
        exit_to = c.get("exit", 0)
        if c.get("joins"):
            # the exit seam: the last frame's screen onto idle frame EXIT.
            # The intro seams onto it with the band's rows black - its own
            # last frame holds them black
            tgt = idle_s[exit_to] if not full else surface_of(
                np.vstack([b.idle[exit_to],
                           np.zeros((ROWS_FULL - rows, 320), np.uint8)]),
                ROWS_FULL)
            body += rec(prev, tgt, crow)
            nrec += 1
        if len(body) > REGION_MAX:
            raise ValueError("clip %s is %d bytes, over %d"
                             % (c["name"], len(body), REGION_MAX))
        regions.append(bytes(body))
        clip_rows.append(dict(c, nrec=nrec, entries=ent, exit=exit_to,
                              rec_at=rec_at,
                              cueat=[(f, where[nm]) for f, nm in cues]))
    ent_tab = {}
    for i, c in enumerate(clip_rows):
        if c["entries"]:
            ent_tab[i] = len(vres)
            vres += struct.pack("<%dH" % len(c["entries"]), *c["entries"])
    idle_tab = len(vres)
    vres += struct.pack("<%dH" % n, *idle_off)
    if len(vres) > REGION_MAX:
        raise ValueError("the video resident block is %d bytes, over %d"
                         % (len(vres), REGION_MAX))

    # --- the music (streamed) and the menu's effects (resident)
    mus = music_bytes(b.music)
    if not ALIGN <= len(mus) <= MUSIC_MAX:
        raise ValueError("music of %d bytes" % len(mus))
    uib, ui_tab, ui_ids = bytearray(), [], []
    for k in UI_SOUNDS:
        nm = b.ui.get(k)
        if nm is None:
            ui_ids.append(NO_SFX)
            continue
        names = [x for x, _, _ in ui_tab]
        if nm not in names:
            d = sfx_bytes(b.sfx[nm])
            ui_tab.append((nm, len(uib), len(d)))
            uib += d
            names.append(nm)
        ui_ids.append(names.index(nm))
    if len(uib) > REGION_MAX or len(ui_tab) > MAX_SFX:
        raise ValueError("the menu's effects are %d bytes" % len(uib))

    # --- lay the file out
    vres_at = META
    mus_at = vres_at + len(pad(vres, ALIGN))
    ui_at = mus_at + len(pad(mus, ALIGN))
    pos = ui_at + len(pad(uib, ALIGN))
    offs = []
    for r in regions:
        offs.append(pos)
        pos += len(pad(r, ALIGN))
    total = pos

    h = bytearray(SECTOR)
    struct.pack_into("<4sHHBBHHHHHHH", h, 0, MAGIC, VERSION, SECTOR,
                     b.cgapal, rows, b.fps, b.rate, b.rate // b.fps,
                     len(clip_rows), n, len(b.anchors), len(ui_tab))
    h[24:27] = bytes(ui_ids)
    struct.pack_into("<IH", h, 30, vres_at, len(vres))
    struct.pack_into("<II", h, 36, mus_at, len(pad(mus, ALIGN)))
    struct.pack_into("<I", h, 44, len(mus))
    struct.pack_into("<H", h, 50, idle_tab)
    struct.pack_into("<I", h, 52, total)
    struct.pack_into("<IH", h, 56, ui_at, len(uib))
    for i, k in enumerate(b.anchors):           # 64: anchors, 16 words
        struct.pack_into("<H", h, 64 + 2 * i, k)
    for i, (nm, o, ln) in enumerate(ui_tab):    # 96: the menu's effects
        struct.pack_into("<IH", h, 96 + 6 * i, o, ln)
        h[192 + 8 * i:200 + 8 * i] = nm.upper().encode()[:8].ljust(8, b" ")

    ct = bytearray(SCRIPT_AT - CLIP_TAB)
    if len(clip_rows) > MAX_CLIPS:
        raise ValueError("%d clips" % len(clip_rows))
    for i, c in enumerate(clip_rows):
        flags = ((CF_JOINS if c.get("joins") else 0) |
                 (CF_FULL if c.get("full") else 0) |
                 (CF_ENDS if c.get("ends") else 0))
        at = i * CLIP_SZ
        cue = c["cueat"] + [(0xFFFF, (0, 0))] * 2
        struct.pack_into("<IIHHHHBBHHH", ct, at, offs[i], len(regions[i]),
                         c["nrec"], flags, c["exit"], ent_tab.get(i, 0),
                         1 if len(c["cueat"]) > 0 else 0,
                         1 if len(c["cueat"]) > 1 else 0,
                         cue[0][0], cue[1][0], c["rec_at"])
        ct[at + 24:at + 32] = c["name"].upper().encode()[:8].ljust(8, b" ")
        struct.pack_into("<HHHH", ct, at + 32, cue[0][1][0], cue[0][1][1],
                         cue[1][1][0], cue[1][1][1])

    sc = script_bytes(b, [c["name"] for c in clip_rows])
    meta = bytearray(h) + ct + sc
    assert len(meta) <= META
    out = pad(meta, ALIGN) + pad(vres, ALIGN) + pad(mus, ALIGN) + \
        pad(uib, ALIGN)
    for r in regions:
        out += pad(r, ALIGN)
    assert len(out) == total
    return bytes(out)


def script_bytes(b, names):
    """The battle script (SPEC.md 125.2.4): 512 bytes.
       0  u8 members (<= 2), u8 commands a member (<= 4)
       2  the enemy: name 10, hp u16, its attack clip u8, damage u16,
          victory clip u8, defeat clip u8                       (18 bytes)
       24 a member each, 64 bytes: name 8, hp u16, mp u16 (the most),
          then 4 commands of 12: label 6, clip u8, kind u8, amount u16,
          mp u16"""
    s = b.script
    out = bytearray(SECTOR)
    cl = lambda n: names.index(n)
    out[0] = len(s["party"])
    out[1] = 4
    e = s["enemy"]
    out[2:12] = e["name"].upper().encode()[:10].ljust(10, b" ")
    struct.pack_into("<HBHBB", out, 12, e["hp"], cl(e["clip"]), e["damage"],
                     cl(s["victory"]), cl(s["defeat"]))
    for i, m in enumerate(s["party"]):
        at = 24 + 64 * i
        out[at:at + 8] = m["name"].upper().encode()[:8].ljust(8, b" ")
        struct.pack_into("<HH", out, at + 8, m["hp"], m["mp"])
        for j in range(4):
            ca = at + 12 + 12 * j
            if j < len(m["commands"]):
                c = m["commands"][j]
                out[ca:ca + 6] = c["label"].encode()[:6].ljust(6, b" ")
                struct.pack_into("<BBHH", out, ca + 6, cl(c["clip"]),
                                 c["kind"], c["amount"], c.get("mp", 0))
            else:
                out[ca + 6] = 0xFF
    return out


# --- reading one back -------------------------------------------------------------

class Bundle:
    def __init__(self, data):
        self.d = data
        (magic, ver, hsz, self.cgapal, self.rows, self.fps, self.rate,
         self.spf, self.nclips, self.nidle, self.nanch, self.nui) = \
            struct.unpack_from("<4sHHBBHHHHHHH", data, 0)
        if magic != MAGIC or ver != VERSION:
            raise ValueError("not a VBT1 file of version %d" % VERSION)
        self.ui_ids = list(data[24:27])
        self.vres_at, vlen = struct.unpack_from("<IH", data, 30)
        self.vres = data[self.vres_at:self.vres_at + vlen]
        self.mus_at = struct.unpack_from("<I", data, 36)[0]
        self.mlen = struct.unpack_from("<I", data, 44)[0]
        self.mus = data[self.mus_at:self.mus_at + self.mlen]
        self.idle_tab = struct.unpack_from("<H", data, 50)[0]
        self.total = struct.unpack_from("<I", data, 52)[0]
        self.ui_at, ulen = struct.unpack_from("<IH", data, 56)
        self.uib = data[self.ui_at:self.ui_at + ulen]
        self.anchors = [struct.unpack_from("<H", data, 64 + 2 * i)[0]
                        for i in range(self.nanch)]
        self.ui = [struct.unpack_from("<IH", data, 96 + 6 * i)
                   for i in range(self.nui)]
        self.clips = []
        for i in range(self.nclips):
            at = CLIP_TAB + i * CLIP_SZ
            (off, ln, nrec, flags, ex, ent, c0, c1, f0, f1, rec_at) = \
                struct.unpack_from("<IIHHHHBBHHH", data, at)
            name = data[at + 24:at + 32].decode().strip()
            e0, l0, e1, l1 = struct.unpack_from("<HHHH", data, at + 32)
            cues = [(f0, e0, l0)] if c0 else []
            if c1:
                cues.append((f1, e1, l1))
            self.clips.append(dict(off=off, len=ln, nrec=nrec, flags=flags,
                                   exit=ex, ent=ent, cues=cues, name=name,
                                   rec_at=rec_at))

    def idle_rec(self, i):
        o = struct.unpack_from("<H", self.vres, self.idle_tab + 2 * i)[0]
        n = struct.unpack_from("<H", self.vres, o)[0]
        return self.vres[o:o + n]

    def entry_rec(self, c, a):
        o = struct.unpack_from("<H", self.vres, c["ent"] + 2 * a)[0]
        n = struct.unpack_from("<H", self.vres, o)[0]
        return self.vres[o:o + n]

    def stream(self, c):
        s = self.d[c["off"]:c["off"] + c["len"]]
        out, at = [], c["rec_at"]
        while at < len(s):
            n = struct.unpack_from("<H", s, at)[0]
            out.append(s[at:at + n])
            at += n
        if len(out) != c["nrec"]:
            raise ValueError("clip %s: %d records, its row says %d"
                             % (c["name"], len(out), c["nrec"]))
        return out


def verify(data, quiet=False):
    """Plays every path in the bundle through the reference decoder and
    checks each join lands on the idle loop's own screen"""
    b = Bundle(data)
    g = geom(b.rows)

    def canvas(s):
        return bytes(s[x] for base in g.base for x in range(base, base + 80))

    scr = bytearray(65536)
    idle = [None] * b.nidle
    intro = [c for c in b.clips if c["flags"] & CF_FULL]
    if intro:
        for r in b.stream(intro[0]):
            decode(scr, r)
        if not intro[0]["flags"] & CF_JOINS:
            raise ValueError("the intro does not join the loop")
        cur = intro[0]["exit"]
    else:
        decode(scr, b.idle_rec(0))
        cur = 0
    for lap in range(2):            # two laps: lap 2 must be lap 1
        for k in range(b.nidle):
            i = (cur + 1 + k) % b.nidle
            decode(scr, b.idle_rec(i))
            cv = canvas(scr)
            if lap == 1 and idle[i] != cv:
                raise ValueError("the idle loop's frame %d differs on lap 2" % i)
            idle[i] = cv
    paths = 0
    for c in b.clips:
        for f, o, ln in c["cues"]:
            if o + ln > c["rec_at"]:
                raise ValueError("clip %s: an effect past its own" % c["name"])
        if c["flags"] & CF_FULL:
            continue
        for a, k in enumerate(b.anchors):
            s = bytearray(65536)
            for y, base in enumerate(g.base):
                s[base:base + 80] = idle[k][y * 80:(y + 1) * 80]
            decode(s, b.entry_rec(c, a))
            for r in b.stream(c):
                decode(s, r)
            if c["flags"] & CF_JOINS and canvas(s) != idle[c["exit"]]:
                raise ValueError("clip %s from anchor %d misses idle %d"
                                 % (c["name"], k, c["exit"]))
            paths += 1
    if not quiet:
        print("verify: idle %d frames, %d clips, %d paths joined exactly"
              % (b.nidle, len(b.clips), paths))
    return b


# --- the demo battle --------------------------------------------------------------

def demo(bars=16):
    import vbtscene as S
    b = Battle(0x50, S.H, S.FPS, S.RATE)
    b.idle = S.clip_idle()
    b.anchors = [0, 6, 12, 18]
    intro, _ = S.clip_intro()
    b.clips.append(dict(name="intro", frames=intro, full=True, joins=True,
                        exit=0))
    for name, mk, joins in S.CLIPS:
        fr, cues = mk()
        b.clips.append(dict(name=name, frames=fr, cues=cues, joins=joins,
                            exit=0, ends=not joins))
    b.music = S.music(bars=bars)
    b.sfx = {n: S.sfx(n) for n in S.SFX + S.UI_SFX}
    b.ui = dict(tick="tick", ok="ok", deny="deny")
    b.script = dict(
        enemy=dict(name="BEETLE", hp=900, clip="beetle_attack",
                   damage=74, victory="victory", defeat="defeat"),
        party=[
            dict(name="ARI", hp=320, mp=40, commands=[
                dict(label="Attack", clip="ari_attack", kind=K_HIT, amount=95),
                dict(label="Bolt", clip="ari_bolt", kind=K_HIT, amount=180,
                     mp=10)]),
            dict(name="BRAM", hp=260, mp=60, commands=[
                dict(label="Fire", clip="bram_attack", kind=K_HIT, amount=130,
                     mp=8),
                dict(label="Heal", clip="bram_heal", kind=K_HEAL, amount=90,
                     mp=12)]),
        ],
        victory="victory", defeat="defeat")
    return b


def preview(b, d):
    import vbtscene as S
    from PIL import Image
    os.makedirs(d, exist_ok=True)
    for c in b.clips:
        fr = c["frames"]
        sel = [fr[i] for i in range(0, len(fr), max(1, len(fr) // 6))][:6]
        im = np.concatenate([np.vstack([f, np.zeros((200 - f.shape[0], 320),
                                                    np.uint8)]) for f in sel], 1)
        Image.fromarray(S.to_rgb(im)).save(os.path.join(d, c["name"] + ".png"))


def cmd_demo(a):
    b = demo(a.bars)
    data = build(b)
    verify(data)
    with open(a.out, "wb") as f:
        f.write(data)
    print("%s: %d bytes" % (a.out, len(data)))
    if a.preview:
        preview(b, a.preview)


def cmd_info(a):
    d = open(a.file, "rb").read()
    b = Bundle(d)
    print("%s: %d bytes, CGA4 palette %02Xh, canvas 320 x %d, %d fps, %d Hz"
          % (a.file, len(d), b.cgapal, b.rows, b.fps, b.rate))
    print("  idle loop %d frames, anchors %s; video resident %d bytes"
          % (b.nidle, b.anchors, len(b.vres)))
    print("  music %d bytes (%.1f s, %s); the menu's %d effects in %d "
          "bytes (resident)" % (b.mlen, b.mlen / b.rate,
                                "resident" if b.mlen <= MUSIC_RES else "streamed",
                                b.nui, len(b.uib)))
    for c in b.clips:
        fl = "".join(n for bit, n in ((CF_FULL, "F"), (CF_JOINS, "J"),
                                      (CF_ENDS, "E")) if c["flags"] & bit)
        sec = c["nrec"] / b.fps
        print("  %-8s %3d records %6d bytes (%5d of effects) %5.1f KB/s  %-2s "
              "exit %d" % (c["name"], c["nrec"], c["len"], c["rec_at"],
                           c["len"] / 1024 / max(sec, 0.01), fl, c["exit"]))


def cmd_verify(a):
    verify(open(a.file, "rb").read())


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sp = p.add_subparsers(dest="cmd", required=True)
    q = sp.add_parser("demo", help="build the original test battle")
    q.add_argument("out")
    q.add_argument("--bars", type=int, default=16,
                   help="the music's length in bars of 1.6 s (default 16)")
    q.add_argument("--preview", help="write a contact sheet per clip here")
    q.set_defaults(fn=cmd_demo)
    q = sp.add_parser("info")
    q.add_argument("file")
    q.set_defaults(fn=cmd_info)
    q = sp.add_parser("verify")
    q.add_argument("file")
    q.set_defaults(fn=cmd_verify)
    a = p.parse_args(argv)
    a.fn(a)


if __name__ == "__main__":
    main()
