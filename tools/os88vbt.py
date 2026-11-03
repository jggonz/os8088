#!/usr/bin/env python3
"""os88vbt - the host half of Video Battle (SPEC.md 125): a .VBT bundle.

A battle played as joined video clips. The bundle holds an idle loop, a
set of action clips, the music, the sound effects and a battle script,
laid out for the engine (apps/vbattle/). Every frame is a record of the
Video Player's ten lists (SPEC.md 98.1.3, os88vid.to_lists), decoded by
apps/video/vdec.inc. Every JOIN is exact by construction: the records are
made between the screens the decoder will really hold, so

  - the idle loop's record 0 takes its last frame to its first;
  - an action's ENTRY records (one per anchor frame of the idle loop) take
    that idle frame's screen to the action's first frame;
  - an action's last record is a SEAM onto idle frame EXIT, and the loop
    goes on at EXIT + 1.

Commands:
    os88vbt.py demo OUT.VBT [--preview DIR]   the original test battle
    os88vbt.py info FILE.VBT
    os88vbt.py verify FILE.VBT                decode every path, check joins

Format (SPEC.md 125.2), little-endian:
    sector 0      the header
    sector 1-2    the clip table, 32 bytes a clip
    sector 3      the battle script
    then, each on 4 KB: the VIDEO RESIDENT block, the MUSIC, the EFFECTS,
                  and each clip's stream
"""

import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88vid as V  # noqa: E402

MAGIC = b"VBT1"
VERSION = 1
SECTOR = 512
ALIGN = 4096        # every region starts on 4 KB: OSAPI_FILE_READ_SEQ seeks
                    # and reads in CLUSTER multiples, and 4 KB covers every
                    # floppy and a FAT16 hard disk to 128 MB
REGION_MAX = 60 * 1024  # a region is read into one claim in one capacity
                    # word, rounded up to ALIGN
PAGE = 32768        # the music is read into 32 KB pages, a claim each
MAX_CLIPS = 32
CLIP_SZ = 32
NAME_LEN = 8

# clip flags
CF_JOINS = 1        # the last record is a seam onto idle frame EXIT
CF_FULL = 2         # a full screen clip (the intro): 200 rows
CF_ENDS = 4         # the battle ends on it; its last frame is held

NO_SFX = 0xFF

# the script's command kinds
K_HIT = 0           # damage the enemy
K_HEAL = 1          # heal the whole party

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


class Battle:
    """What a bundle is made of, in host terms"""

    def __init__(self, cgapal, rows, fps, rate):
        self.cgapal, self.rows, self.fps, self.rate = cgapal, rows, fps, rate
        self.idle = []          # pixel frames
        self.anchors = []       # idle frame numbers an action may start at
        self.clips = []         # dicts: name, frames, cues {frame: sfx}, joins,
                                # exit, full, ends
        self.music = None       # float array, -1..1
        self.sfx = []           # (name, float array)
        self.script = None      # dict

    def sfx_id(self, name):
        for i, (n, _) in enumerate(self.sfx):
            if n == name:
                return i
        raise KeyError(name)


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
    streams = []
    for c in b.clips:
        full = c.get("full", False)
        crow = ROWS_FULL if full else rows
        fs = [surface_of(f, crow) for f in c["frames"]]
        ent = []
        if not full:
            for k in b.anchors:
                ent.append(len(vres))
                vres += rec(idle_s[k], fs[0], rows)
        body = bytearray()
        start = 0 if full else 1        # an action's frame 0 is its entry's
        prev = bytearray(65536) if full else fs[0]
        if full:
            body += rec(prev, fs[0], crow)
            prev = fs[0]
            start = 1
        nrec = 1 if full else 0
        for f in fs[start:]:
            body += rec(prev, f, crow)
            prev = f
            nrec += 1
        exit_to = c.get("exit", 0)
        if c.get("joins"):
            # the exit seam: the last frame's screen onto idle frame EXIT.
            # A full-screen clip (the intro) seams onto idle EXIT with the
            # band's rows black - its own last frame holds them black
            tgt = idle_s[exit_to] if not full else surface_of(
                np.vstack([b.idle[exit_to],
                           np.zeros((ROWS_FULL - rows, 320), np.uint8)]),
                ROWS_FULL)
            body += rec(prev, tgt, crow)
            nrec += 1
        streams.append(bytes(body))
        clip_rows.append(dict(c, nrec=nrec, entries=ent, exit=exit_to))
    if len(vres) > 65535:
        raise ValueError("the video resident block is %d bytes, over 64 KB"
                         % len(vres))
    # anchor table and entry tables live after the records: per clip, an
    # entry table of len(anchors) words
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
    for c, st in zip(clip_rows, streams):
        if len(st) > REGION_MAX:
            raise ValueError("clip %s is %d bytes, over %d"
                             % (c["name"], len(st), REGION_MAX))

    # --- the audio block: the music (headroom: 64..191), then the effects
    #     (signed, -64..63), so music + effect never leaves 0..255
    mus = np.clip(np.round(b.music * 63.5 + 127.5), 64, 191).astype(np.uint8)
    aud = bytearray(mus.tobytes())
    sfxb = bytearray()
    sfx_tab = []
    for name, x in b.sfx:
        s = np.clip(np.round(x * 63.5), -64, 63).astype(np.int8)
        sfx_tab.append((len(sfxb), len(s)))
        sfxb += s.tobytes()
    if len(sfxb) > REGION_MAX:
        raise ValueError("the effects are %d bytes, over %d" % (len(sfxb),
                                                               REGION_MAX))
    if len(aud) > 16 * PAGE:
        raise ValueError("the music is %d bytes, over 16 pages" % len(aud))

    # --- lay the file out
    hdr_sz = SECTOR
    ctab_at = SECTOR
    script_at = 3 * SECTOR
    vres_at = ALIGN
    aud_at = vres_at + len(pad(vres, ALIGN))
    sfx_at = aud_at + len(pad(aud, ALIGN))
    pos = sfx_at + len(pad(sfxb, ALIGN))
    offs = []
    for s in streams:
        offs.append(pos)
        pos += len(pad(s, ALIGN))
    total = pos

    h = bytearray(SECTOR)
    struct.pack_into("<4sHHBBHHHHHHH", h, 0, MAGIC, VERSION, hdr_sz,
                     b.cgapal, rows, b.fps, b.rate, b.rate // b.fps,
                     len(clip_rows), n, len(b.anchors), len(b.sfx))
    # 30: the resident block, 36: the audio block, 44: music, 50: idle table
    struct.pack_into("<IH", h, 30, vres_at, len(vres))
    struct.pack_into("<II", h, 36, aud_at, len(aud))
    struct.pack_into("<I", h, 44, len(mus))
    struct.pack_into("<H", h, 50, idle_tab)
    struct.pack_into("<I", h, 52, total)
    struct.pack_into("<IH", h, 56, sfx_at, len(sfxb))
    for i, k in enumerate(b.anchors):           # 64: anchors, 16 words
        struct.pack_into("<H", h, 64 + 2 * i, k)
    for i, (o, ln) in enumerate(sfx_tab):       # 96: sfx, 6 bytes each (16)
        struct.pack_into("<IH", h, 96 + 6 * i, o, ln)
    for i, (name, _) in enumerate(b.sfx):       # 192: sfx names, 8 each
        h[192 + 8 * i:200 + 8 * i] = name.upper().encode()[:8].ljust(8, b" ")

    ct = bytearray(2 * SECTOR)
    if len(clip_rows) > MAX_CLIPS:
        raise ValueError("%d clips" % len(clip_rows))
    for i, c in enumerate(clip_rows):
        flags = ((CF_JOINS if c.get("joins") else 0) |
                 (CF_FULL if c.get("full") else 0) |
                 (CF_ENDS if c.get("ends") else 0))
        cues = sorted(c.get("cues", {}).items())[:2]
        cid = [b.sfx_id(s) for _, s in cues] + [NO_SFX] * 2
        cfr = [f for f, _ in cues] + [0xFFFF] * 2
        struct.pack_into("<IIHHHHBBHH", ct, i * CLIP_SZ, offs[i],
                         len(streams[i]), c["nrec"], flags, c["exit"],
                         ent_tab.get(i, 0), cid[0], cid[1], cfr[0], cfr[1])
        ct[i * CLIP_SZ + 24:i * CLIP_SZ + 32] = \
            c["name"].upper().encode()[:8].ljust(8, b" ")

    sc = script_bytes(b, [c["name"] for c in clip_rows])
    out = pad(bytearray(h) + ct + pad(sc), ALIGN) + pad(vres, ALIGN) + \
        pad(aud, ALIGN) + pad(sfxb, ALIGN)
    for s in streams:
        out += pad(s, ALIGN)
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
         self.spf, self.nclips, self.nidle, self.nanch, self.nsfx) = \
            struct.unpack_from("<4sHHBBHHHHHHH", data, 0)
        self.hsz = hsz
        if magic != MAGIC or ver != VERSION:
            raise ValueError("not a VBT1 file")
        self.vres_at, vlen = struct.unpack_from("<IH", data, 30)
        self.vres = data[self.vres_at:self.vres_at + vlen]
        self.aud_at, alen = struct.unpack_from("<II", data, 36)
        self.aud = data[self.aud_at:self.aud_at + alen]
        self.mlen = struct.unpack_from("<I", data, 44)[0]
        self.idle_tab = struct.unpack_from("<H", data, 50)[0]
        self.total = struct.unpack_from("<I", data, 52)[0]
        self.sfx_at, slen = struct.unpack_from("<IH", data, 56)
        self.sfxb = data[self.sfx_at:self.sfx_at + slen]
        self.anchors = [struct.unpack_from("<H", data, 64 + 2 * i)[0]
                        for i in range(self.nanch)]
        self.sfx = [struct.unpack_from("<IH", data, 96 + 6 * i)
                    for i in range(self.nsfx)]
        self.clips = []
        for i in range(self.nclips):
            at = SECTOR + i * CLIP_SZ
            (off, ln, nrec, flags, ex, ent, c0, c1, f0, f1) = \
                struct.unpack_from("<IIHHHHBBHH", data, at)
            name = data[at + 24:at + 32].decode().strip()
            self.clips.append(dict(off=off, len=ln, nrec=nrec, flags=flags,
                                   exit=ex, ent=ent, cues=[(c0, f0), (c1, f1)],
                                   name=name))

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
        out, at = [], 0
        while at < len(s):
            n = struct.unpack_from("<H", s, at)[0]
            out.append(s[at:at + n])
            at += n
        return out


def verify(data, quiet=False):
    """Plays every path in the bundle through the reference decoder and
    checks each join lands on the idle loop's own screen"""
    b = Bundle(data)
    g = geom(b.rows)
    # the idle loop from its own frame 0: play intro first if there is one
    scr = bytearray(65536)
    idle = [None] * b.nidle
    # reach idle frame 0: the first FULL clip (the intro) seams onto it
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
    # two laps: every frame of the loop from the screen it really holds
    for lap in range(2):
        for k in range(b.nidle):
            i = (cur + 1 + k) % b.nidle
            decode(scr, b.idle_rec(i))
            canvas = bytes(scr[x] for base in g.base for x in
                           range(base, base + 80))
            if lap == 1 and idle[i] != canvas:
                raise ValueError("the idle loop's frame %d differs on lap 2" % i)
            idle[i] = canvas
    paths = 0
    for ci, c in enumerate(b.clips):
        if c["flags"] & CF_FULL:
            continue
        for a, k in enumerate(b.anchors):
            s = bytearray(65536)
            for base in g.base:
                pass
            # the screen at idle frame k
            put = idle[k]
            for y, base in enumerate(g.base):
                s[base:base + 80] = put[y * 80:(y + 1) * 80]
            decode(s, b.entry_rec(c, a))
            for r in b.stream(c):
                decode(s, r)
            if c["flags"] & CF_JOINS:
                got = bytes(s[x] for base in g.base for x in
                            range(base, base + 80))
                if got != idle[c["exit"]]:
                    raise ValueError("clip %s from anchor %d misses idle %d"
                                     % (c["name"], k, c["exit"]))
            paths += 1
    if not quiet:
        print("verify: idle %d frames, %d clips, %d paths joined exactly"
              % (b.nidle, len(b.clips), paths))
    return b


# --- the demo battle --------------------------------------------------------------

def demo():
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
    b.music = S.music(6.4)
    b.sfx = [(n, S.sfx(n)) for n in S.SFX]
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
    b = demo()
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
    print("  music %d bytes (%.1f s), %d effects in %d bytes"
          % (b.mlen, b.mlen / b.rate, b.nsfx, len(b.sfxb)))
    for c in b.clips:
        fl = "".join(n for bit, n in ((CF_FULL, "F"), (CF_JOINS, "J"),
                                      (CF_ENDS, "E")) if c["flags"] & bit)
        sec = c["nrec"] / b.fps
        print("  %-8s %3d records %6d bytes %5.1f KB/s  %-2s exit %d"
              % (c["name"], c["nrec"], c["len"], c["len"] / 1024 / max(sec, 0.01),
                 fl, c["exit"]))


def cmd_verify(a):
    verify(open(a.file, "rb").read())


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sp = p.add_subparsers(dest="cmd", required=True)
    q = sp.add_parser("demo", help="build the original test battle")
    q.add_argument("out")
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
