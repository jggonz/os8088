#!/usr/bin/env python3
"""os88vbt - the host half of Video Battle (SPEC.md 125): a .VBT bundle.

A battle played as joined video clips. The bundle holds the openings, an
idle loop, a set of action clips (each a stream of frames carrying its own
sound), the menu's own sounds, the music and a battle script, laid out for the engine
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
    os88vbt.py build PROJECT [-o OUT.VBT] [--json]  a project (tools/vbtproj.py)
    os88vbt.py demo-project OUT.vbp [--bars N] [--floppy]   the demo as one
    os88vbt.py recommend PROJECT    the idle loop's and each clip's points
    os88vbt.py demo OUT.VBT [--bars N] [--preview DIR]   the original battle
    os88vbt.py info FILE.VBT
    os88vbt.py verify FILE.VBT        decode every path, check every join

Format version 5 (SPEC.md 125.2), little-endian:
    0             the header
    512           the clip table, 48 bytes a clip, at most 64
    4096          the RULES (tools/vbtrules.py, SPEC.md 125.6), to 16 KB
    then, each on 4 KB: the VIDEO RESIDENT block (the idle loop, every
                  clip's anchor table), the MUSIC (streamed), the MENU's
                  effects (resident), the ENTRIES (resident, any length,
                  each on a paragraph and named by it), the band's
                  IMAGES (resident), and each clip's
                  STREAM - its records, each carrying its frame's sound;
                  any length, streamed
"""

import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88vid as V  # noqa: E402

MAGIC = b"VBT1"
VERSION = 5
SECTOR = 512
ALIGN = 4096        # every region starts on 4 KB: OSAPI_FILE_READ_SEQ seeks
                    # and reads in CLUSTER multiples, and 4 KB covers every
                    # floppy and a FAT16 hard disk to 128 MB
REGION_MAX = 60 * 1024  # a RESIDENT region is read into one claim's piece
ENT_MAX = 0x10000 * 16   # the entries: a paragraph number names each
REC_MAX = 16384     # a record is read contiguously out of a clip's ring, so
                    # it is at most the ring's guard (VB_CGUARD, 125.4)
MAX_CLIPS = 64
MAX_OPEN = 8
MAX_ANCH = 16
CLIP_TAB = 512
CLIP_SZ = 48
SCRIPT_AT = 4096
META = 16384        # the header, the clip table and the RULES: one read
MAX_SFX = 16
MUSIC_MAX = 512 * 1024 * 1024   # (a 32-bit length; the engine streams it)
MUSIC_RES = 61440   # music this short the engine holds resident (VB_MRES)

# clip flags
CF_JOINS = 1        # the last record is a seam onto idle frame EXIT
CF_FULL = 2         # its records address the whole screen (an opening)
CF_ENDS = 4         # the battle ends on it; its last frame is held
CF_OWNSND = 8       # its sound is played INSTEAD of the music (an opening)
CF_OPENING = 16     # one of the openings, played in turn before the battle

NO_SFX = 0xFF


# the menu's sounds, header bytes 24..28 (125.2.1)
UI_SOUNDS = ("move", "open", "close", "take", "deny")

ROWS_FULL = 200
REC_HDR = 4         # a record: its length (all of it), its sound's length


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


def full_of(px):
    """A frame of the canvas's rows as a whole screen: the rows under it
    (the band's) black"""
    if px.shape[0] == ROWS_FULL:
        return px
    return np.vstack([px, np.zeros((ROWS_FULL - px.shape[0], px.shape[1]),
                                   np.uint8)])


GEOMS = {}


def geom(rows):
    if rows not in GEOMS:
        GEOMS[rows] = V.Geom(V.LAY_CGA, 80, rows)
    return GEOMS[rows]


def rec(a, b, rows, audio=b""):
    """The record that takes surface a to surface b, with its frame's sound:
    its length (all of it), the sound's length, the ten lists (SPEC.md
    98.1.3), the sound (125.2.3)"""
    lists = V.to_lists(V.seam_ops(a, b, geom(rows)))[0] if a is not None \
        else EMPTY_LISTS
    n = REC_HDR + len(lists) + len(audio)
    if n > REC_MAX:
        raise ValueError("a record of %d bytes, over %d" % (n, REC_MAX))
    return struct.pack("<HH", n, len(audio)) + lists + audio


EMPTY_LISTS = bytes(V.NLISTS)       # ten empty lists: a frame of sound only


def decode(surf, r):
    n, alen = struct.unpack_from("<HH", r, 0)
    used = V.decode_lists(surf, r[REC_HDR:n - alen])
    if used != n - alen - REC_HDR:
        raise ValueError("a record's lists end at %d of %d"
                         % (used, n - alen - REC_HDR))


def music_bytes(x):
    """The music, held to 64..191 so that music + an effect never leaves
    0..255 (125.2): mixing is one add a sample"""
    return np.clip(np.round(x * 63.5 + 127.5), 64, 191).astype(np.uint8).tobytes()


def sfx_bytes(x):
    """An effect, signed, -64..63"""
    return np.clip(np.round(x * 63.5), -64, 63).astype(np.int8).tobytes()


def own_bytes(x):
    """A clip's OWN sound, played instead of the music: unsigned, 0..255"""
    return np.clip(np.round(x * 127 + 128), 0, 255).astype(np.uint8).tobytes()


def frame_sound(track, nframes, spf, own):
    """A clip's sound cut a frame each: bytes, or b"" for a silent frame
    (125.2.3: a frame with no sound carries none)"""
    out = []
    for f in range(nframes):
        x = track[f * spf:(f + 1) * spf] if track is not None else []
        if len(x) == 0 or not np.any(np.abs(x) > 1.0 / 128):
            out.append(b"")
            continue
        if len(x) < spf:
            x = np.concatenate([x, np.zeros(spf - len(x))])
        out.append(own_bytes(x) if own else sfx_bytes(x))
    return out


def place(effects, nframes, spf, sfx):
    """An action's sound: its effects placed at their frames, mixed.
    EFFECTS is {frame: name} or a list of (frame, name)"""
    if not effects:
        return None
    t = np.zeros(nframes * spf)
    items = effects.items() if isinstance(effects, dict) else effects
    for f, name in items:
        x = sfx[name]
        a = f * spf
        n = min(len(x), len(t) - a)
        if n > 0:
            t[a:a + n] += x[:n]
    return t


def cue_key(q, sfx, spf):
    """Cue Q's sound as the build places it, added to SFX under its own
    name: its LEVEL applied and, when it has an UNTIL (the clip frame it
    stops at), cut there. The Listen button (vbtana.clip_sound) and the
    build both come through here, so what is heard is what is built"""
    nm, lv, until = q["sound"], q.get("level", 1.0), q.get("until")
    if lv == 1.0 and until is None:
        return nm
    key = "%s*%g" % (nm, lv) + ("" if until is None else "@%d" % until)
    if key not in sfx:
        x = sfx[nm] * lv
        if until is not None:
            x = x[:max(0, until - q["frame"]) * spf]
        sfx[key] = x
    return key


class Battle:
    """What a bundle is made of, in host terms"""

    def __init__(self, cgapal, rows, fps, rate):
        self.cgapal, self.rows, self.fps, self.rate = cgapal, rows, fps, rate
        self.idle = []          # pixel frames
        self.anchors = []       # the idle frames a clip may start at, unless
                                # it names its own
        self.openings = []      # dicts: name, frames, sound (float), full
        self.music_at = 0       # the sample the music comes in at
        self.clips = []         # dicts: name, frames, cues {frame: sfx name},
                                # joins, exit, ends, anchors
        self.music = None       # float array, -1..1
        self.sfx = {}           # name -> float array: every effect
        self.ui = {}            # a UI_SOUNDS name -> an effect's name
        self.band = (0, 142, 40, 58)    # cells across, rows down, cells, rows
        self.canvas = (0, 0, 320, 142)  # pixels
        self.rules = None       # the RULES block (vbtrules.compile_rules)
        self.images = b""       # the band's pictures, back to back


def build(b):
    rows = b.rows
    spf = b.rate // b.fps
    idle_s = [surface_of(f, rows) for f in b.idle]
    n = len(idle_s)
    if n < 2:
        raise ValueError("an idle loop of %d frames" % n)
    # --- the video resident block: the idle loop's records, the entries
    vres = bytearray()
    idle_off = []
    for i in range(n):
        idle_off.append(len(vres))
        vres += rec(idle_s[i - 1], idle_s[i], rows)
    clip_rows, regions = [], []
    maxrec = [0]

    def stream(recs):
        maxrec[0] = max([maxrec[0]] + [len(r) for r in recs])
        return b"".join(recs)

    # --- the openings, in turn from a black screen: every record over the
    # whole screen, a canvas frame with the band's rows black under it.
    # A battle with none still needs one: the loop's records are deltas, and
    # nothing would have drawn its first frame. So it gets a START opening,
    # one frame, silent: idle frame 0 from black
    openings = b.openings or [dict(name="start", frames=[b.idle[0]],
                                   sound=None, exit=0)]
    scr = np.zeros((ROWS_FULL, 320), np.uint8)
    for k, o in enumerate(openings):
        last = k == len(openings) - 1
        fr = [full_of(f) for f in o["frames"]]
        snd = frame_sound(o.get("sound"), len(fr), spf, True)
        prev = surface_of(scr, ROWS_FULL)
        recs = []
        for f, a in zip(fr, snd):
            cur = surface_of(f, ROWS_FULL)
            recs.append(rec(prev, cur, ROWS_FULL, a))
            prev = cur
        flags = CF_OPENING | CF_FULL | CF_OWNSND
        exit_to = 0
        if last:            # the scene opening seams onto the idle loop
            flags |= CF_JOINS
            exit_to = o.get("exit", 0)
            recs.append(rec(prev, surface_of(full_of(b.idle[exit_to]),
                                             ROWS_FULL), ROWS_FULL))
        scr = fr[-1]
        regions.append(stream(recs))
        clip_rows.append(dict(name=o["name"], nrec=len(recs), flags=flags,
                              exit=exit_to, anch=[]))
    # --- the actions: an entry from each anchor, into the ENTRIES (each on
    # a paragraph, named by it: version 4, so that they are bounded by
    # memory and not by a 16-bit offset); record 0 is frame 0's sound alone
    # (the entry drew its picture); the last record the seam
    ents = bytearray()
    for c in b.clips:
        fs = [surface_of(f, rows) for f in c["frames"]]
        anch = []
        for k in c.get("anchors", b.anchors):
            anch.append((k, len(ents) >> 4))
            ents += pad(rec(idle_s[k], fs[0], rows), 16)
            if len(ents) > ENT_MAX:
                raise ValueError("the entries are over %d bytes" % ENT_MAX)
        snd = frame_sound(place(c.get("cues", {}), len(fs), spf, b.sfx),
                          len(fs), spf, False)
        recs = [rec(None, None, rows, snd[0])]
        for i in range(1, len(fs)):
            recs.append(rec(fs[i - 1], fs[i], rows, snd[i]))
        exit_to = c.get("exit", 0)
        flags = CF_ENDS if c.get("ends") else 0
        if c.get("joins"):
            flags |= CF_JOINS
            recs.append(rec(fs[-1], idle_s[exit_to], rows))
        regions.append(stream(recs))
        clip_rows.append(dict(name=c["name"], nrec=len(recs), flags=flags,
                              exit=exit_to, anch=anch))
    if len(clip_rows) > MAX_CLIPS:
        raise ValueError("%d clips" % len(clip_rows))
    for c in clip_rows:
        if len(c["anch"]) > MAX_ANCH:
            raise ValueError("clip %s: %d anchors" % (c["name"], len(c["anch"])))
        c["anchtab"] = len(vres) if c["anch"] else 0
        for k, o in c["anch"]:
            vres += struct.pack("<HH", k, o)
    idle_tab = len(vres)
    vres += struct.pack("<%dH" % n, *idle_off)
    if len(vres) > REGION_MAX:
        raise ValueError("the video resident block is %d bytes, over %d"
                         % (len(vres), REGION_MAX))

    # --- the music (streamed) and the menu's effects (resident)
    mus = music_bytes(b.music)
    if not ALIGN <= len(mus) <= MUSIC_MAX:
        raise ValueError("music of %d bytes" % len(mus))
    if not 0 <= b.music_at < len(mus):
        raise ValueError("the music's join at %d of %d" % (b.music_at, len(mus)))
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
    meta_len = SCRIPT_AT + len(pad(b.rules, ALIGN))   # (8 KB to META)
    vres_at = meta_len
    mus_at = vres_at + len(pad(vres, ALIGN))
    ui_at = mus_at + len(pad(mus, ALIGN))
    ent_at = ui_at + len(pad(uib, ALIGN))
    img_at = ent_at + len(pad(ents, ALIGN))
    imgs = bytes(b.images)
    pos = img_at + len(pad(imgs, ALIGN))
    offs = []
    for r in regions:
        offs.append(pos)
        pos += len(pad(r, ALIGN))
    total = pos

    h = bytearray(SECTOR)
    struct.pack_into("<4sHHBBHHHHHHH", h, 0, MAGIC, VERSION, SECTOR,
                     b.cgapal, rows, b.fps, b.rate, spf, len(clip_rows), n,
                     len(openings), len(ui_tab))
    h[24:29] = bytes(ui_ids)
    struct.pack_into("<IH", h, 30, vres_at, len(vres))
    struct.pack_into("<II", h, 36, mus_at, len(pad(mus, ALIGN)))
    struct.pack_into("<I", h, 44, len(mus))
    struct.pack_into("<I", h, 48, b.music_at)
    struct.pack_into("<I", h, 52, total)
    struct.pack_into("<IH", h, 56, ui_at, len(uib))
    struct.pack_into("<H", h, 62, idle_tab)
    h[64:68] = bytes(b.band)
    struct.pack_into("<HHHH", h, 68, *b.canvas)
    struct.pack_into("<H", h, 76, maxrec[0])
    opens = list(range(len(openings)))
    if len(opens) > MAX_OPEN:
        raise ValueError("%d openings" % len(opens))
    h[80:88] = bytes(opens + [NO_SFX] * (MAX_OPEN - len(opens)))
    struct.pack_into("<II", h, 88, ent_at, len(ents))
    struct.pack_into("<IHH", h, 320, img_at, len(imgs), meta_len)
    for i, (nm, o, ln) in enumerate(ui_tab):    # 96: the menu's effects
        struct.pack_into("<IH", h, 96 + 6 * i, o, ln)
        h[192 + 8 * i:200 + 8 * i] = nm.upper().encode()[:8].ljust(8, b" ")

    ct = bytearray(SCRIPT_AT - CLIP_TAB)
    if len(clip_rows) * CLIP_SZ > len(ct):
        raise ValueError("%d clips" % len(clip_rows))
    for i, c in enumerate(clip_rows):
        at = i * CLIP_SZ
        struct.pack_into("<IIHHHHH", ct, at, offs[i], len(regions[i]),
                         c["nrec"], c["flags"], c["exit"], c["anchtab"],
                         len(c["anch"]))
        ct[at + 24:at + 32] = c["name"].upper().encode()[:8].ljust(8, b" ")

    meta = bytearray(h) + ct + bytes(b.rules)
    assert len(meta) <= META
    meta = bytes(meta).ljust(meta_len, b"\0")    # (as long as its rules)
    assert len(meta) <= META
    out = pad(meta, ALIGN) + pad(vres, ALIGN) + pad(mus, ALIGN) + \
        pad(uib, ALIGN) + pad(ents, ALIGN) + pad(imgs, ALIGN)
    for r in regions:
        out += pad(r, ALIGN)
    assert len(out) == total
    return bytes(out)


# --- reading one back -------------------------------------------------------------

class Bundle:
    def __init__(self, data):
        self.d = data
        (magic, ver, hsz, self.cgapal, self.rows, self.fps, self.rate,
         self.spf, self.nclips, self.nidle, self.nopen, self.nui) = \
            struct.unpack_from("<4sHHBBHHHHHHH", data, 0)
        if magic != MAGIC or ver != VERSION:
            raise ValueError("not a VBT1 file of version %d" % VERSION)
        self.ui_ids = list(data[24:29])
        self.vres_at, vlen = struct.unpack_from("<IH", data, 30)
        self.vres = data[self.vres_at:self.vres_at + vlen]
        self.mus_at = struct.unpack_from("<I", data, 36)[0]
        self.mlen = struct.unpack_from("<I", data, 44)[0]
        self.music_at = struct.unpack_from("<I", data, 48)[0]
        self.mus = data[self.mus_at:self.mus_at + self.mlen]
        self.total = struct.unpack_from("<I", data, 52)[0]
        self.ui_at, ulen = struct.unpack_from("<IH", data, 56)
        self.uib = data[self.ui_at:self.ui_at + ulen]
        self.idle_tab = struct.unpack_from("<H", data, 62)[0]
        self.band = tuple(data[64:68])
        self.canvas = struct.unpack_from("<HHHH", data, 68)
        self.maxrec = struct.unpack_from("<H", data, 76)[0]
        self.openings = [x for x in data[80:88] if x != NO_SFX]
        self.ent_at, elen = struct.unpack_from("<II", data, 88)
        self.ents = data[self.ent_at:self.ent_at + elen]
        self.img_at, ilen = struct.unpack_from("<IH", data, 320)
        self.imgs = data[self.img_at:self.img_at + ilen]
        import vbtrules
        self.meta_len = struct.unpack_from("<H", data, 326)[0]
        self.rules = vbtrules.Rules(data[SCRIPT_AT:self.meta_len])
        self.ui = [struct.unpack_from("<IH", data, 96 + 6 * i)
                   for i in range(self.nui)]
        self.clips = []
        for i in range(self.nclips):
            at = CLIP_TAB + i * CLIP_SZ
            (off, ln, nrec, flags, ex, anchtab, nanch) = \
                struct.unpack_from("<IIHHHHH", data, at)
            name = data[at + 24:at + 32].decode().strip()
            anch = [struct.unpack_from("<HH", self.vres, anchtab + 4 * j)
                    for j in range(nanch)]
            self.clips.append(dict(off=off, len=ln, nrec=nrec, flags=flags,
                                   exit=ex, anch=anch, name=name))

    def idle_rec(self, i):
        o = struct.unpack_from("<H", self.vres, self.idle_tab + 2 * i)[0]
        n = struct.unpack_from("<H", self.vres, o)[0]
        return self.vres[o:o + n]

    def entry(self, para):
        o = para * 16
        n = struct.unpack_from("<H", self.ents, o)[0]
        return self.ents[o:o + n]

    def stream(self, c):
        s = self.d[c["off"]:c["off"] + c["len"]]
        out, at = [], 0
        while at < len(s):
            n = struct.unpack_from("<H", s, at)[0]
            out.append(s[at:at + n])
            at += n
        if len(out) != c["nrec"]:
            raise ValueError("clip %s: %d records, its row says %d"
                             % (c["name"], len(out), c["nrec"]))
        return out


def verify(data, quiet=False):
    """Plays every path in the bundle through the reference decoder: the
    openings in turn from black onto the idle loop, two laps of the loop,
    and every action from every one of its anchors, each seam required to
    land on the loop's own screen"""
    b = Bundle(data)
    g = geom(b.rows)

    def canvas(s):
        return bytes(s[x] for base in g.base for x in range(base, base + 80))

    scr = bytearray(65536)
    cur = 0
    for ci in b.openings:
        c = b.clips[ci]
        for r in b.stream(c):
            decode(scr, r)
        cur = c["exit"]
    if not b.openings:
        decode(scr, b.idle_rec(0))
    elif not b.clips[b.openings[-1]]["flags"] & CF_JOINS:
        raise ValueError("the last opening does not join the loop")
    idle = [None] * b.nidle
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
        if c["flags"] & CF_OPENING:
            continue
        if not c["anch"]:
            raise ValueError("clip %s has no anchor" % c["name"])
        for k, o in c["anch"]:
            s = bytearray(65536)
            for y, base in enumerate(g.base):
                s[base:base + 80] = idle[k][y * 80:(y + 1) * 80]
            decode(s, b.entry(o))
            for r in b.stream(c):
                decode(s, r)
            if c["flags"] & CF_JOINS and canvas(s) != idle[c["exit"]]:
                raise ValueError("clip %s from anchor %d misses idle %d"
                                 % (c["name"], k, c["exit"]))
            paths += 1
    if not quiet:
        print("verify: %d openings onto an idle loop of %d frames, %d clips, "
              "%d paths joined exactly" % (len(b.openings), b.nidle,
                                          len(b.clips), paths))
    return b


# --- the demo battle --------------------------------------------------------------

def demo(bars=16, floppy=False):
    """The demo battle: its project (demo_project), as a Battle"""
    return battle_of(demo_project(None, bars, floppy))


# --- a PROJECT (tools/vbtproj.py) built ----------------------------------------------

def battle_of(proj, progress=None):
    """A project as a Battle: every source's screens (tools/vbtana.py,
    cached), its sounds, and the script the engine of today plays"""
    import vbtana as A
    pg = progress or (lambda *a: None)
    d, t = proj.d, proj.d["target"]
    b = Battle(t["palette"], t["canvas"][3], t["fps"], t["rate"])
    b.band = tuple(t["band"])
    b.canvas = tuple(t["canvas"])
    b.idle = A.idle_of(proj, pg)
    for sid in d["sounds"]:
        b.sfx[sid] = A.sound(proj, sid)
    opened = [o["clip"] for o in d["openings"]]
    for i, o in enumerate(d["openings"]):
        c = d["clips"][o["clip"]]
        pg("clips", i, len(d["clips"]), o["clip"])
        b.openings.append(dict(name=o["clip"], frames=A.frames_of(proj, o["clip"], pg),
                               sound=b.sfx[o["sound"]] if o.get("sound") else None,
                               exit=c["exit"]))
    for i, (cid, c) in enumerate(d["clips"].items()):
        if cid in opened:
            continue
        pg("clips", i, len(d["clips"]), cid)
        spf = t["rate"] // t["fps"]
        cues = [(q["frame"], cue_key(q, b.sfx, spf)) for q in c["cues"]]
        b.clips.append(dict(name=cid, frames=A.frames_of(proj, cid, pg),
                            cues=cues, joins=c["joins"], exit=c["exit"],
                            ends=c["ends"], anchors=c["anchors"]))
    b.music = b.sfx[d["music"]["sound"]]   # all music loops (10.2): the
    b.music_at = d["music"]["join"]        # region is the loop, and the
    lp = d["music"].get("loop")            # engine plays it round
    if lp:
        b.music = b.music[lp[0]:lp[1]]
        b.music_at -= lp[0]
    b.ui = dict(d["ui_sounds"])
    import vbtrules
    # the clips as the bundle numbers them: the openings first - and a
    # battle with none gets START at 0 (build) - then the rest
    opened = b.openings or [dict(name="start")]
    idx = {c["name"]: i for i, c in enumerate(opened + b.clips)}
    b.images, places = images_of(proj)
    b.rules = vbtrules.compile_rules(proj, idx, places)
    return b


def images_of(proj):
    """The project's band pictures (its "images": name, path, col, row,
    dither) in its palette: CGA4 bytes, two a cell, a row after a row, each
    on a paragraph of the IMAGES region. Returns (the region, [(col, row,
    cells, rows, paragraph)])"""
    out, places = bytearray(), []
    pal = proj.d["target"]["palette"]
    for im in proj.d.get("images", []):
        px = band_picture(proj.here(im["path"]), pal, im.get("dither", True),
                          im.get("cells"), im.get("rows"))
        rows, w = px.shape
        cells = w // 8
        packed = (px[:, 0::4] << 6) | (px[:, 1::4] << 4) | \
            (px[:, 2::4] << 2) | px[:, 3::4]
        while len(out) % 16:
            out.append(0)
        places.append((im.get("col", 0), im.get("row", 0), cells, rows,
                       len(out) >> 4))
        out += packed.astype(np.uint8).tobytes()
    return bytes(out), places


BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9],
                   [15, 7, 13, 5]]) / 16.0 - 0.5


def band_picture(path, pal, dither=True, cells=None, rows=None):
    """A picture file as pixel indexes 0..3 in CGA4 palette PAL, its width
    a whole number of 8-pixel cells: the nearest of the four colours, after
    an ordered dither when DITHER (SPEC.md 125.5)"""
    from PIL import Image
    import vbtana as A
    im = Image.open(path).convert("RGB")
    w = (cells * 8) if cells else max(8, im.width // 8 * 8)
    h = rows or im.height
    im = im.resize((w, h), Image.LANCZOS)
    x = np.asarray(im, np.float32)
    cols = np.array(A.palette_rgb(pal), np.float32)
    if dither:
        y, xx = np.mgrid[0:h, 0:w]
        x = x + BAYER4[y % 4, xx % 4][..., None] * 96
    dd = ((x[:, :, None, :] - cols[None, None]) ** 2).sum(-1)
    return dd.argmin(-1).astype(np.uint8)


def build_project(proj, progress=None):
    errs = proj.errors()
    if errs:
        raise ValueError("the project has errors:\n  " +
                         "\n  ".join(map(repr, errs)))
    b = battle_of(proj, progress)
    data = build(b)
    verify(data, quiet=True)
    return data


def demo_project(path, bars=16, floppy=False):
    """The demo battle (demo()) as a PROJECT: every source a scene of
    tools/vbtscene.py, so that it needs no footage. Building it must give
    demo()'s bundle byte for byte (tests/vbtbuild.py)"""
    import vbtproj as P
    import vbtscene as S
    p = P.Project()
    p.set(("name",), "The demo battle")
    p.set(("target", "palette"), 0x50)
    p.set(("target", "fps"), S.FPS)
    p.set(("target", "rate"), S.RATE)
    p.set(("target", "canvas"), [0, 0, 320, S.H])
    for n in S.SFX + S.UI_SFX:
        p.add_sound(n, scene=n)
    p.set(("sounds", "music"), {"kind": "scene", "name": "music",
                                "bars": 2 if floppy else bars, "level": 1.0})
    p.set_music("music", 0)
    p.add_source("idle", scene="idle")
    p.set_idle("idle", (0, S.IDLE_N))
    for n in ("title", "reveal"):
        p.add_source(n, scene=n)
        p.add_clip(n, n, (0, len(P_frames(n)) - 1), anchors=(), exit=0,
                   joins=n == "reveal")
    p.add_opening("title", "fanfare")
    p.add_opening("reveal", "riser")
    for name, mk, joins in S.CLIPS:
        if floppy and name == "ari_limit":
            continue
        fr, cues = mk()
        p.add_source(name, scene=name)
        # (the floppy's: two join points a clip, not four - its entries are
        # half the bytes, and a command waits 0.8 s at most, not 0.4)
        p.add_clip(name, name, (0, len(fr) - 1),
                   anchors=(0, 12) if floppy else (0, 6, 12, 18),
                   exit=0, joins=joins, ends=not joins,
                   cues=[{"frame": f, "sound": c, "level": 1.0}
                         for f, c in cues.items()])
    for k, v in dict(move="tick", take="ok", deny="deny").items():
        p.set(("ui_sounds", k), v)

    def act(aid, label, clip, amount, mp=0, heal=False):
        p.add_action(aid, label, target="allies" if heal else "enemy",
                     effects=[{"verb": "heal" if heal else "damage",
                               "amount": amount}],
                     cost={"mp": mp} if mp else {}, pools={"hit": [clip]})
    act("attack", "Attack", "ari_attack", 95)
    act("bolt", "Bolt", "ari_bolt", 180, 10)
    if not floppy:
        act("limit", "Limit", "ari_limit", 420, 20)
    act("fire", "Fire", "bram_attack", 130, 8)
    act("heal", "Heal", "bram_heal", 90, 12, heal=True)
    act("lunge", "Lunge", "beetle_attack", 74)
    p.set(("actions", "lunge", "target"), "character")
    ari = p.add_character("ARI", hp=320, mp=40)
    for a in ("attack", "bolt") + (() if floppy else ("limit",)):
        p.menu_add(ari, action=a)
    bram = p.add_character("BRAM", hp=260, mp=60)
    for a in ("fire", "heal"):
        p.menu_add(bram, action=a)
    p.set(("enemy", "name"), "BEETLE")
    p.set(("enemy", "hp"), 1200)
    p.enemy_move("lunge")
    p.set(("enemy", "victory"), "victory")
    p.set(("enemy", "defeat"), "defeat")
    if path:
        p.save(path)
    return p


def P_frames(name):
    import vbtana as A
    return A._scene(name)


def preview(b, d):
    import vbtscene as S
    from PIL import Image
    os.makedirs(d, exist_ok=True)
    for c in b.openings + b.clips:
        fr = c["frames"]
        sel = [fr[i] for i in range(0, len(fr), max(1, len(fr) // 6))][:6]
        im = np.concatenate([np.vstack([f, np.zeros((200 - f.shape[0], 320),
                                                    np.uint8)]) for f in sel], 1)
        Image.fromarray(S.to_rgb(im)).save(os.path.join(d, c["name"] + ".png"))


def cmd_demo(a):
    b = demo(a.bars, a.floppy)
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
    print("%s: %d bytes, CGA4 palette %02Xh, canvas %s, band %s, %d fps, "
          "%d Hz" % (a.file, len(d), b.cgapal, b.canvas, b.band, b.fps, b.rate))
    print("  idle loop %d frames; video resident %d bytes; entries %d bytes; "
          "largest record %d" % (b.nidle, len(b.vres), len(b.ents), b.maxrec))
    print("  music %d bytes (%.1f s, %s), in at %d; the menu's %d effects in "
          "%d bytes" % (b.mlen, b.mlen / b.rate,
                        "resident" if b.mlen <= MUSIC_RES else "streamed",
                        b.music_at, b.nui, len(b.uib)))
    for c in b.clips:
        fl = "".join(n for bit, n in ((CF_OPENING, "O"), (CF_FULL, "F"),
                                      (CF_OWNSND, "S"), (CF_JOINS, "J"),
                                      (CF_ENDS, "E")) if c["flags"] & bit)
        sec = c["nrec"] / b.fps
        snd = sum(struct.unpack_from("<H", r, 2)[0] for r in b.stream(c))
        print("  %-8s %3d records %7d bytes (%6d of sound) %5.1f KB/s  %-4s "
              "anchors %s exit %d" % (c["name"], c["nrec"], c["len"], snd,
                                      c["len"] / 1024 / max(sec, 0.01), fl,
                                      [k for k, _ in c["anch"]], c["exit"]))


def cmd_verify(a):
    verify(open(a.file, "rb").read())


def cmd_build(a):
    import json
    import vbtproj as P
    import vbtana as A
    proj = P.Project.load(a.project)

    def say(o):
        if a.json:
            print(json.dumps(o), flush=True)

    def progress(stage, done, of, what):
        say(dict(stage=stage, done=done, of=of, what=what))
    for x in proj.check():
        if a.json:
            say(dict(stage="check", level=x.level, where=x.where, text=x.text))
        elif x.level != "INFO" or a.verbose:
            print(x)
    data = build_project(proj, progress)
    out = a.out or os.path.join(os.path.dirname(proj.path), "BATTLE.VBT")
    with open(out, "wb") as f:
        f.write(data)
    bud = A.budget(proj, data)
    if a.json:
        say(dict(stage="done", out=out, bytes=len(data), budget=bud))
        return
    print("%s: %d bytes; idle loop %d bytes, entries %d" % (
        out, len(data), bud["idle_bytes"], bud["entries"]))
    for c in bud["clips"]:
        print("  %-14s %7d bytes %5.1f KB/s (%3.0f%% of the disk) anchors %s"
              % (c["name"], c["bytes"], c["rate"] / 1024, c["of_disk"] * 100,
                 c["anchors"]))


def cmd_demo_project(a):
    p = demo_project(a.out, a.bars, a.floppy)
    print("%s: %d clips, %d characters" % (a.out, len(p.d["clips"]),
                                          len(p.d["characters"])))


def cmd_recommend(a):
    import vbtproj as P
    import vbtana as A
    proj = P.Project.load(a.project)
    i = proj.d["idle"]
    src = A.screens(proj, i["source"])
    print("idle (%s, %d frames): loop now %s" % (i["source"], len(src),
                                                  i["loop"]))
    for sc, s0, s1 in A.loop_candidates(src, a.min_loop):
        print("  loop %3d..%3d (%d frames): its seam %.2f%% of pixels"
              % (s0, s1 - 1, s1 - s0, sc * 100))
    idle = A.idle_of(proj)
    for cid, c in proj.d["clips"].items():
        if any(o["clip"] == cid for o in proj.d["openings"]):
            continue
        fr = A.screens(proj, c["source"])
        r = A.attach(fr, idle)
        print("%s: in at %d (%.2f%%), anchors %s, out at %d onto idle %d "
              "(%.2f%%); now in %d anchors %s out %d exit %d" % (
                  cid, r["first"], r["in_score"] * 100, r["anchors"],
                  r["last"], r["exit"], r["exit_score"] * 100, c["in"],
                  c["anchors"], c["out"], c["exit"]))


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sp = p.add_subparsers(dest="cmd", required=True)
    q = sp.add_parser("demo", help="build the original test battle")
    q.add_argument("out")
    q.add_argument("--bars", type=int, default=16,
                   help="the music's length in bars of 1.6 s (default 16)")
    q.add_argument("--floppy", action="store_true",
                   help="the floppy's battle: no Limit, and 3.2 s of music, "
                        "short enough to be resident (SPEC.md 125.4)")
    q.add_argument("--preview", help="write a contact sheet per clip here")
    q.set_defaults(fn=cmd_demo)
    q = sp.add_parser("build", help="build a project to a bundle")
    q.add_argument("project")
    q.add_argument("-o", "--out")
    q.add_argument("--json", action="store_true",
                   help="progress and the result as JSON lines")
    q.add_argument("-v", "--verbose", action="store_true",
                   help="also say what the engine does not play yet")
    q.set_defaults(fn=cmd_build)
    q = sp.add_parser("demo-project", help="the demo battle as a project")
    q.add_argument("out")
    q.add_argument("--bars", type=int, default=16)
    q.add_argument("--floppy", action="store_true")
    q.set_defaults(fn=cmd_demo_project)
    q = sp.add_parser("recommend", help="the loop and attachment points")
    q.add_argument("project")
    q.add_argument("--min-loop", type=int, default=20)
    q.set_defaults(fn=cmd_recommend)
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
