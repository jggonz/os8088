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
    os88vbt.py disk PROJECT [--disk N] [-o IMG]   BATTLE.VBT and VBATTLE.O88
                                      on a floppy or a hard disk (98.2.12.1)
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
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88vid as V  # noqa: E402

MAGIC = b"VBT1"
VERSION = 7
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
CF_LOOP = 32        # a title or end loop: record 0 from black, the last the
                    # seam onto frame 0, records 1.. round until a key
CF_ENTER = 64       # opening 0, entered at the title loop's frames (anchors)
ENDING_HOW = {"hold": 0, "fade": 1, "join": 2, "again": 3}

NO_SFX = 0xFF


# the menu's sounds, header bytes 24..28 (125.2.1)
UI_SOUNDS = ("move", "open", "close", "take", "deny")

ROWS_FULL = 200
REC_HDR = 4         # a record: its length (all of it), its sound's length


def pad(b, n=SECTOR):
    return bytes(b) + bytes(-len(b) % n)


class Rend:
    """A RENDITION's pixels on its adapter (VBATTLE-MEMORY-PLAN 9): CGA4's
    two banks of 80 bytes, VGA4's four planes of 40 (mode 0Dh), or
    Hercules' four banks of 90 with the canvas 640 wide at byte 5. The
    frames are the project's same frames; only the bytes differ"""
    KIND = {"cga4": 0, "vga4": 1, "herc": 2}

    def __init__(self, kind="cga4", rows=142, pal=None):
        self.kind, self.code = kind, self.KIND[kind]
        self.w = 640 if kind == "herc" else 320
        self.full = 348 if kind == "herc" else 200
        self.rows = rows
        self.pal = pal              # vga4: 16 (r, g, b), the DAC's six bits
        # a record's most: the rings' guard - 16 KB, or 32 where a whole
        # screen is more than 16 (VGA's planes, Hercules'), the engine's
        # [vb_cgkb] from the header's largest record
        self.recmax = REC_MAX if kind == "cga4" else 2 * REC_MAX
        self.geoms = {}

    def geom(self, rows):
        if rows not in self.geoms:
            if self.kind == "vga4":
                g = V.Geom(V.LAY_LIN80, 40, rows, bitplanes=True, stride=40)
            elif self.kind == "herc":
                g = V.Geom(V.LAY_HERC, 80, rows)
            else:
                g = V.Geom(V.LAY_CGA, 80, rows)
            self.geoms[rows] = g
        return self.geoms[rows]

    def blank(self):
        return bytearray(V.PLANE * 4) if self.kind == "vga4" \
            else bytearray(65536)

    def surface(self, px, rows):
        if self.kind == "cga4":
            s = bytearray(65536)
            p = px.astype(np.uint8)
            packed = (p[:, 0::4] << 6) | (p[:, 1::4] << 4) | \
                (p[:, 2::4] << 2) | p[:, 3::4]
            for y in range(rows):
                b = (y & 1) * 8192 + (y >> 1) * 80
                s[b:b + 80] = packed[y].tobytes()
            return s
        g = self.geom(rows)
        s = self.blank()
        if self.kind == "vga4":
            g.put(s, px[:rows].astype(np.uint8).tobytes())
        else:
            g.put(s, np.packbits(px[:rows].astype(np.uint8), axis=1).tobytes())
        return s

    def lists(self, a, b, rows):
        g = self.geom(rows)
        ops = V.seam_ops(a, b, g)
        if g.planes == 1:
            return V.to_lists(ops)[0]
        body = bytearray()          # VGA4: sub-records, each its Map Mask
        for mask, sub in ops:       # and ten lists, a 0 after the last
            if sub:
                body.append(mask)
                body += V.to_lists(sub)[0]
        body.append(0)
        return bytes(body)

    def empty(self):
        return b"\0" if self.kind == "vga4" else EMPTY_LISTS

    def decode(self, surf, lists):
        """The lists onto SURF; their length"""
        if self.kind != "vga4":
            return V.decode_lists(surf, lists)
        at = 0
        mv = memoryview(surf)
        while lists[at]:
            m = lists[at]
            used = 0
            for p in range(4):
                if m >> p & 1:
                    used = V.decode_lists(mv[p * V.PLANE:(p + 1) * V.PLANE],
                                          lists[at + 1:])
            at += 1 + used
        return at + 1


REND = Rend()


def surface_of(px, rows):
    """The rendition's surface image of pixel frame PX, its first ROWS"""
    return REND.surface(px, rows)


def full_of(px):
    """A frame of the canvas's rows as a whole screen: the rows under it
    (the band's) black"""
    if px.shape[0] == REND.full:
        return px
    return np.vstack([px, np.zeros((REND.full - px.shape[0], px.shape[1]),
                                   np.uint8)])


def geom(rows):
    return REND.geom(rows)


def rec(a, b, rows, audio=b""):
    """The record that takes surface a to surface b, with its frame's sound:
    its length (all of it), the sound's length, the lists (SPEC.md 98.1.3;
    VGA4's sub-records), the sound (125.2.3)"""
    lists = REND.lists(a, b, rows) if a is not None else REND.empty()
    n = REC_HDR + len(lists) + len(audio)
    if n > REND.recmax:
        raise ValueError("a record of %d bytes, over %d" % (n, REND.recmax))
    return struct.pack("<HH", n, len(audio)) + lists + audio


def rec_pad(r, k):
    """Record R with K zero bytes after its lists, before its sound: a
    loop's lap padded to whole 4 KB (format 7). The decoder ends at its
    tenth list and never reads them"""
    if not k:
        return r
    n, alen = struct.unpack_from("<HH", r, 0)
    if n + k > REND.recmax:
        raise ValueError("a padded record of %d bytes, over %d"
                         % (n + k, REND.recmax))
    return struct.pack("<HH", n + k, alen) + r[4:n - alen] + bytes(k) + \
        r[n - alen:]


EMPTY_LISTS = bytes(V.NLISTS)       # ten empty lists: a frame of sound only


def decode(surf, r):
    n, alen = struct.unpack_from("<HH", r, 0)
    used = REND.decode(surf, r[REC_HDR:n - alen])
    if used != n - alen - REC_HDR and \
            any(r[REC_HDR + used:n - alen]):    # (a loop's PADDING: zeros)
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
        self.title = None       # (125.3.1) dict: name, frames, sound - a
                                # loop over the whole screen
        self.endings = {}       # outcome -> (how, a loop dict or None);
                                # an end loop is the canvas's
        self.fade_steps = 16
        self.rend = Rend("cga4", rows)  # the adapter it is for


def build(b):
    global REND
    REND = b.rend
    rows = b.rows
    spf = b.rate // b.fps
    idle_s = [surface_of(f, rows) for f in b.idle]
    n = len(idle_s)
    if n < 2:
        raise ValueError("an idle loop of %d frames" % n)
    # --- the IDLE loop's records, a region of their own named by PARAGRAPH
    # (format 7, VBATTLE-MEMORY-PLAN 3.4): resident, bounded by memory and
    # not by the block's 16-bit offsets. The block keeps the tables
    vres = bytearray()
    idle = bytearray()
    idle_off = []
    for i in range(n):
        idle_off.append(len(idle) >> 4)
        idle += pad(rec(idle_s[i - 1], idle_s[i], rows), 16)
        if len(idle) > ENT_MAX:
            raise ValueError("the idle loop is over %d bytes" % ENT_MAX)
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
    scr = np.zeros((REND.full, REND.w), np.uint8)
    title_s = [surface_of(full_of(f), REND.full) for f in b.title["frames"]] \
        if b.title else []
    ents = bytearray()
    for k, o in enumerate(openings):
        last = k == len(openings) - 1
        fr = [full_of(f) for f in o["frames"]]
        snd = frame_sound(o.get("sound"), len(fr), spf, True)
        prev = surface_of(scr, REND.full)
        recs = []
        anch = []
        # FULL SCREEN, or with the menu (125.3): only a full one's flag
        # says so, and the engine brings the band in at the first opening
        # without it - or, for a bundle where every opening has it (format
        # 7 before the choice), at the last
        flags = CF_OPENING | CF_OWNSND | (CF_FULL if o.get("full") else 0)
        if o.get("enter"):          # ENTERED at the title's frames, as an
            flags |= CF_ENTER       # action is at the idle loop's: an entry
            cur = surface_of(fr[0], REND.full)      # from each anchor, and
            for ka in o["anchors"]:                 # its record 0 its sound
                anch.append((ka, len(ents) >> 4))
                ents += pad(rec(title_s[ka], cur, REND.full), 16)
            recs.append(rec(None, None, REND.full, snd[0]))
            prev = cur
            fr, snd = fr[1:], snd[1:]
        for f, a in zip(fr, snd):
            cur = surface_of(f, REND.full)
            recs.append(rec(prev, cur, REND.full, a))
            prev = cur
        exit_to = 0
        if last:            # the scene opening seams onto the idle loop
            flags |= CF_JOINS
            exit_to = o.get("exit", 0)
            recs.append(rec(prev, surface_of(full_of(b.idle[exit_to]),
                                             REND.full), REND.full))
        scr = o["frames"][-1] if not fr else fr[-1]
        scr = full_of(scr)
        regions.append(stream(recs))
        clip_rows.append(dict(name=o["name"], nrec=len(recs), flags=flags,
                              exit=exit_to, anch=anch))
    # --- the actions: an entry from each anchor, into the ENTRIES (each on
    # a paragraph, named by it: version 4, so that they are bounded by
    # memory and not by a 16-bit offset); record 0 is frame 0's sound alone
    # (the entry drew its picture); the last record the seam
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
        elif c.get("join_loop") is not None:    # an ending that JOINS its
            recs.append(rec(fs[-1], surface_of(     # end loop: the seam onto
                c["join_loop"], rows), rows))       # its frame 0
        regions.append(stream(recs))
        clip_rows.append(dict(name=c["name"], nrec=len(recs), flags=flags,
                              exit=exit_to, anch=anch))
    # --- the LOOPS (125.3.1): the title over the whole screen, an end loop
    # in the canvas. Record 0 from black, records 1..n-1, and record n the
    # seam back onto frame 0; each carries its frame's sound (frame 0's
    # twice: record n shows it again)
    loop_ix = {}

    def loop_clip(lp, full):
        key = lp["name"]
        if key in loop_ix:
            return loop_ix[key]
        rw = REND.full if full else rows
        fr = [full_of(f) if full else f for f in lp["frames"]]
        ss = [surface_of(f, rw) for f in fr]
        snd = frame_sound(lp.get("sound"), len(ss), spf, True)
        recs = [rec(surface_of(np.zeros_like(fr[0]), rw), ss[0], rw, snd[0])]
        for i in range(1, len(ss)):
            recs.append(rec(ss[i - 1], ss[i], rw, snd[i]))
        recs.append(rec(ss[-1], ss[0], rw, snd[0]))
        # ...laid out to be read ROUND (format 7, VBATTLE-MEMORY-PLAN 3.1):
        # record 0 padded to end on 4 KB, the lap a whole number of 4 KB,
        # the padding inside records past their lists
        recs[0] = rec_pad(recs[0], -len(recs[0]) % ALIGN)
        lap = sum(len(r) for r in recs[1:])
        need = -lap % ALIGN
        for i in range(len(recs) - 1, 0, -1):   # the seam first, then back
            room = REND.recmax - len(recs[i])
            if need and room > 0:
                k = min(room, need)
                recs[i] = rec_pad(recs[i], k)
                need -= k
        if need:
            raise ValueError("loop %s: no room to pad its lap" % lp["name"])
        flags = CF_LOOP | (CF_FULL if full else 0) | \
            (CF_OWNSND if lp.get("sound") is not None else 0)
        regions.append(stream(recs))
        clip_rows.append(dict(name=lp["name"], nrec=len(recs), flags=flags,
                              exit=0, anch=[], lap=(len(recs[0]),
                              sum(len(r) for r in recs[1:]) // ALIGN)))
        loop_ix[key] = len(clip_rows) - 1
        return loop_ix[key]
    title_ix = loop_clip(b.title, True) if b.title else NO_SFX
    leave_ix = 0 if openings[0].get("enter") else NO_SFX
    ends = []
    for k in ("victory", "defeat"):
        how, lp = b.endings.get(k, ("hold", None))
        ends += [ENDING_HOW[how], loop_clip(lp, False) if lp else NO_SFX]
    if len(clip_rows) > MAX_CLIPS:
        raise ValueError("%d clips" % len(clip_rows))
    for c in clip_rows:
        if len(c["anch"]) > MAX_ANCH:
            raise ValueError("clip %s: %d anchors" % (c["name"], len(c["anch"])))
        c["anchtab"] = len(vres) if c["anch"] else 0
        for k, o in c["anch"]:
            vres += struct.pack("<HH", k, o)
    idle_tab = len(vres)
    vres += struct.pack("<%dH" % n, *idle_off)     # (paragraphs in IDLE)
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
    idl_at = ent_at + len(pad(ents, ALIGN))
    img_at = idl_at + len(pad(idle, ALIGN))
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
    h[330:337] = bytes([title_ix, leave_ix] + ends + [b.fade_steps])
    h[337] = REND.code                  # the adapter (cga4, vga4, herc)
    hold = hold_order(clip_rows, offs, regions, mus_at, len(mus))
    struct.pack_into("<H", h, 398, len(hold))
    for i, (a4, n4) in enumerate(hold):
        struct.pack_into("<HH", h, 400 + 4 * i, a4, n4)
    if REND.pal:                        # vga4: its sixteen, the DAC's six bits
        h[352:400] = bytes(v for c in REND.pal for v in c)
    struct.pack_into("<II", h, 338, idl_at, len(idle))
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
        if c.get("lap"):            # a loop's lap: where, and its 4 KBs
            struct.pack_into("<IH", ct, at + 18, *c["lap"])
        ct[at + 24:at + 32] = c["name"].upper().encode()[:8].ljust(8, b" ")

    meta = bytearray(h) + ct + bytes(b.rules)
    assert len(meta) <= META
    meta = bytes(meta).ljust(meta_len, b"\0")    # (as long as its rules)
    assert len(meta) <= META
    out = pad(meta, ALIGN) + pad(vres, ALIGN) + pad(mus, ALIGN) + \
        pad(uib, ALIGN) + pad(ents, ALIGN) + pad(idle, ALIGN) + \
        pad(imgs, ALIGN)
    for r in regions:
        out += pad(r, ALIGN)
    assert len(out) == total
    return bytes(out)


VB_HEAD = 40960         # what a clip's start waits for (vbattle.asm VB_HEAD)
HOLD_MAX = 28           # the hold order's ranges (header 398..511)
MUSIC_RES = 61440       # music this short is resident (VB_MRES)


def hold_order(clip_rows, offs, regions, mus_at, mlen):
    """The HOLD ORDER (VBATTLE-MEMORY-PLAN 5): ranges of the file in 4 KBs,
    most worth holding first, for a pool smaller than the bundle. The music
    when it streams (it reads all through the play); the loops; every
    action's HEAD, so no action waits on the disk to start; the openings;
    then the rest of each action's clip, smallest first"""
    def r4(at, n):
        return (at // ALIGN, -(-n // ALIGN))
    out = []
    if mlen > MUSIC_RES:
        out.append(r4(mus_at, mlen))
    acts = []
    for i, c in enumerate(clip_rows):
        if c["flags"] & CF_LOOP:
            out.append(r4(offs[i], len(regions[i])))
        elif not c["flags"] & CF_OPENING:
            acts.append(i)
    for i in acts:
        out.append(r4(offs[i], min(len(regions[i]), VB_HEAD)))
    for i, c in enumerate(clip_rows):
        if c["flags"] & CF_OPENING:
            out.append(r4(offs[i], len(regions[i])))
    for i in sorted(acts, key=lambda i: len(regions[i])):
        if len(regions[i]) > VB_HEAD:
            out.append(r4(offs[i] + VB_HEAD, len(regions[i]) - VB_HEAD))
    merged = []
    for a, n in out:                # (one after the other in the file: one)
        if merged and merged[-1][0] + merged[-1][1] == a:
            merged[-1] = (merged[-1][0], merged[-1][1] + n)
        else:
            merged.append((a, n))
    return merged[:HOLD_MAX]


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
        self.title, self.leave = data[330], data[331]
        self.endings = {"victory": (data[332], data[333]),
                        "defeat": (data[334], data[335])}
        self.fade_steps = data[336]
        global REND
        kind = {0: "cga4", 1: "vga4", 2: "herc"}[data[337]]
        REND = Rend(kind, self.rows, [tuple(data[352 + 3 * i:355 + 3 * i])
                                      for i in range(16)]
                    if kind == "vga4" else None)
        self.rend = REND
        self.idl_at, ilen2 = struct.unpack_from("<II", data, 338)
        nh = struct.unpack_from("<H", data, 398)[0]
        self.hold = [struct.unpack_from("<HH", data, 400 + 4 * i)
                     for i in range(nh)]
        self.idle_r = data[self.idl_at:self.idl_at + ilen2]
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
            lap = struct.unpack_from("<IH", data, at + 18) \
                if flags & CF_LOOP else None
            self.clips.append(dict(off=off, len=ln, nrec=nrec, flags=flags,
                                   exit=ex, anch=anch, name=name, lap=lap))

    def idle_rec(self, i):
        o = struct.unpack_from("<H", self.vres, self.idle_tab + 2 * i)[0] * 16
        n = struct.unpack_from("<H", self.idle_r, o)[0]
        return self.idle_r[o:o + n]

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
        return g.canvas(s)

    def loop_lap(c):
        """Loop clip C from black: the screens after record 0 and after
        each of records 1..n, twice round - lap 2 must be lap 1, and its
        last (the seam) frame 0 again"""
        rs = b.stream(c)
        s = REND.blank()
        decode(s, rs[0])
        first = bytes(s)
        laps = []
        for _ in range(2):
            got = []
            for r in rs[1:]:
                decode(s, r)
                got.append(bytes(s))
            laps.append(got)
        if laps[0] != laps[1] or laps[0][-1] != first:
            raise ValueError("loop %s does not go round exactly" % c["name"])
        return [first] + laps[0][:-1]       # frame k's screen

    title = None
    if b.title != NO_SFX:
        title = loop_lap(b.clips[b.title])
    for k, (how, li) in b.endings.items():
        if li != NO_SFX:
            loop_lap(b.clips[li])

    def openings_from(scr):
        cur = 0
        for ci in b.openings:
            c = b.clips[ci]
            if c["flags"] & CF_ENTER:
                continue
            for r in b.stream(c):
                decode(scr, r)
            cur = c["exit"]
        return cur
    scr = REND.blank()
    if b.openings and b.clips[b.openings[0]]["flags"] & CF_ENTER:
        lc = b.clips[b.openings[0]]
        if title is None or not lc["anch"]:
            raise ValueError("an entered opening and no title to enter from")
        ends = set()
        for k, o in lc["anch"]:     # left from each of the title's frames
            s2 = bytearray(title[k])
            decode(s2, b.entry(o))
            for r in b.stream(lc):
                decode(s2, r)
            cur = openings_from(s2)
            ends.add(bytes(s2))
        if len(ends) != 1:
            raise ValueError("the title's way out ends differently by anchor")
        scr = s2
    else:
        cur = openings_from(scr)
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
    vic, dft = b.rules.victory, b.rules.defeat
    ending_name = {"victory": b.clips[vic]["name"] if vic not in (None, NO_SFX)
                   else None,
                   "defeat": b.clips[dft]["name"] if dft not in (None, NO_SFX)
                   else None}
    for c in b.clips:
        if c["flags"] & (CF_OPENING | CF_LOOP):
            continue
        if not c["anch"]:
            raise ValueError("clip %s has no anchor" % c["name"])
        for k, o in c["anch"]:
            s = REND.blank()
            g.put(s, idle[k])
            decode(s, b.entry(o))
            for r in b.stream(c):
                decode(s, r)
            if c["flags"] & CF_JOINS and canvas(s) != idle[c["exit"]]:
                raise ValueError("clip %s from anchor %d misses idle %d"
                                 % (c["name"], k, c["exit"]))
            for out_, (how, li) in b.endings.items():   # an ending that
                if how == ENDING_HOW["join"] and li != NO_SFX and \
                        c["name"] == ending_name.get(out_):
                    lp = loop_lap(b.clips[li])               # JOINS its loop
                    if canvas(s) != canvas(lp[0]):
                        raise ValueError("ending %s misses its loop's frame 0"
                                         % c["name"])
            paths += 1
    if not quiet:
        print("verify: %d openings onto an idle loop of %d frames, %d clips, "
              "%d paths joined exactly" % (len(b.openings), b.nidle,
                                          len(b.clips), paths))
    return b


# --- the demo battle --------------------------------------------------------------

def demo(bars=16, floppy=False, kind="cga4"):
    """The demo battle: its project (demo_project), as a Battle - in
    rendition KIND (its made-up scenes are CGA's, which VGA's slots 0..3 and
    Hercules' two by two show as they are)"""
    return battle_of(demo_project(None, bars, floppy), kind=kind)


# --- a PROJECT (tools/vbtproj.py) built ----------------------------------------------

def battle_of(proj, progress=None, kind="cga4"):
    """A project as a Battle: every source's screens (tools/vbtana.py,
    cached), its sounds, and the script the engine of today plays"""
    import vbtana as A
    pg = progress or (lambda *a: None)
    d, t = proj.d, proj.d["target"]
    import vbtana as A_
    w_, rows_, full_ = A_.rend_size(proj, kind)
    b = Battle(t["palette"], rows_, t["fps"], t["rate"])
    pal = None
    if kind == "vga4":              # one palette for the battle, six bits,
        p8 = A_.battle_palette(proj, t.get("vga_colours", 8), progress or
                               (lambda *a: None))
        pal = [tuple(v >> 2 for v in c) for c in p8] + \
            [(0, 0, 0)] * (16 - len(p8))
    b.rend = Rend(kind, rows_, pal)
    b.band = tuple(t["band"])
    b.canvas = tuple(t["canvas"])
    b.idle = A.idle_of(proj, pg, kind)
    for sid in proj.sounds_used():      # (only what is used is read: a
        b.sfx[sid] = A.sound(proj, sid)  # sound left over is no file to find)
    opened = [o["clip"] for o in d["openings"]]
    for i, o in enumerate(d["openings"]):
        c = d["clips"][o["clip"]]
        pg("clips", i, len(d["clips"]), o["clip"])
        full = bool(o.get("full"))
        b.openings.append(dict(name=o["clip"], frames=A.frames_of(
            proj, o["clip"], pg, kind, full=full),
            sound=b.sfx[o["sound"]] if o.get("sound") else None,
            exit=c["exit"], full=full))
    # a clip only a loop or the title's way out plays is not an action's
    tt = d.get("title") or {}
    loops_only = {tt.get("clip"), tt.get("leave")} | {
        en["loop"]["clip"] for en in d["endings"].values() if en.get("loop")}
    rules_use = set()
    for a in d["actions"].values():
        for pool in a["pools"].values():
            rules_use |= set(pool)
        for per in a.get("by_target", {}).values():
            for pool in per.values():
                rules_use |= set(pool)
    rules_use |= {d["enemy"].get("victory"), d["enemy"].get("defeat")}
    for i, (cid, c) in enumerate(d["clips"].items()):
        if cid in opened or (cid in loops_only and cid not in rules_use):
            continue
        pg("clips", i, len(d["clips"]), cid)
        spf = t["rate"] // t["fps"]
        cues = [(q["frame"], cue_key(q, b.sfx, spf)) for q in c["cues"]]
        b.clips.append(dict(name=cid, frames=A.frames_of(proj, cid, pg, kind),
                            cues=cues, joins=c["joins"], exit=c["exit"],
                            ends=c["ends"], anchors=c["anchors"]))
    if d["music"]:
        b.music = b.sfx[d["music"]["sound"]]   # all music loops (10.2): the
        b.music_at = d["music"]["join"]        # region is the loop, and the
        lp = d["music"].get("loop")            # engine plays it round
        if lp:
            b.music = b.music[lp[0]:lp[1]]
            b.music_at -= lp[0]
    else:                   # NO MUSIC: a second of silence, played round as
        import numpy as np  # any music is - the engine is the same
        b.music = np.zeros(t["rate"])
        b.music_at = 0
    b.ui = dict(d["ui_sounds"])
    # --- the title loop and the endings (125.3.1)
    b.fade_steps = d.get("fade_steps", 16)

    def loop_of(lp, tag):
        c = d["clips"][lp["clip"]]
        fr = A.frames_of(proj, lp["clip"], pg, kind)[lp["loop"][0]:lp["loop"][1]]
        x = None
        if lp.get("sound"):
            x = b.sfx[lp["sound"]]
        return dict(name=("%s%d" % (tag, lp["loop"][0]))[:8], frames=fr,
                    sound=x, src=(lp["clip"], tuple(lp["loop"]),
                                  lp.get("sound"), c["in"]))
    t = d.get("title")
    if t:
        b.title = loop_of(t, "T")
        if t.get("leave"):              # the clip it is left by: opening 0,
            lc = d["clips"][t["leave"]]   # entered at the title's frames
            b.openings.insert(0, dict(      # (its sound: its cues, mixed,
                name=t["leave"], frames=A.frames_of(proj, t["leave"], pg, kind),
                sound=A.clip_sound(proj, t["leave"]),   # played as its own)
                exit=lc["exit"], enter=True, anchors=list(t["anchors"]),
                full=True))     # (the band not yet: the title's way out is
                                # before every opening, full-screen or not)
    made = {}
    for k in ("victory", "defeat"):
        en = d["endings"][k]
        lp = None
        if en.get("loop") and en["how"] in ("fade", "join"):
            key = json.dumps(en["loop"], sort_keys=True)
            if key not in made:
                made[key] = loop_of(en["loop"], "E")
                made[key]["name"] = ("E%d%s" % (len(made), en["loop"]["clip"])
                                     )[:8]
            lp = made[key]
        b.endings[k] = (en["how"] if lp or en["how"] in ("hold", "again")
                        else "hold", lp)
        clip = d["enemy"].get(k)
        if en["how"] == "join" and lp and clip:
            for c in b.clips:
                if c["name"] == clip:
                    c["join_loop"] = lp["frames"][0]
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


BUNDLE_NAMES = {"cga4": "BATTLE.VBT", "vga4": "BATTLEV.VBT",
                "herc": "BATTLEH.VBT"}      # the engine looks for its own first


def renditions_of(proj):
    """The renditions a project builds: CGA4 always, and VGA4 and Hercules
    as its target asks (target["renditions"])"""
    r = [k for k in proj.d["target"].get("renditions", ["cga4"])
         if k in BUNDLE_NAMES]
    return ["cga4"] + [k for k in r if k != "cga4"]


def build_project(proj, progress=None, kind="cga4"):
    errs = proj.errors()
    if errs:
        raise ValueError("the project has errors:\n  " +
                         "\n  ".join(map(repr, errs)))
    b = battle_of(proj, progress, kind)
    if kind == "herc":
        b.canvas = (0, 0, 640, b.rows)
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
    if floppy:                      # (the 360 KB disk has no room for the
        p.add_opening("title", "fanfare")   # loops: its demo opens as
    p.add_opening("reveal", "riser")        # before)
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
    if not floppy:                  # THE TITLE AND THE ENDINGS (125.3.1): the
        p.set_title("title", (19, 35))      # title card round until a key,
        p.set_ending("victory", "fade", "victory", (21, 31))   # a fade away;
        p.set_ending("defeat", "again")     # victory's end loop; defeat
    if path:                                # plays again
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
    b = demo(a.bars, a.floppy, a.rendition)
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
          "largest record %d" % (b.nidle, len(b.idle_r), len(b.ents), b.maxrec))
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


def engine_path(build=None):
    """VBATTLE.O88 for a disk: build/'s, or one shipped BESIDE this tool
    (the builder's zip carries it). None when there is neither"""
    here = os.path.dirname(os.path.abspath(__file__))
    b = build or os.path.join(os.path.dirname(here), "build")
    for p in (os.path.join(b, "vbattle.o88"), os.path.join(here, "VBATTLE.O88"),
              os.path.join(here, "vbattle.o88")):
        if os.path.exists(p):
            return p
    return None


def disk_plan(proj, disk=None, bundle=None, out=None, build=None, stage=None,
              kinds=None):
    """(argv, image, files, note) making DISKS[disk] - the profile's disk by
    default (os88mkdisk.DISK_OF_PROFILE) - of the built BUNDLE (BATTLE.VBT
    beside the project) and the engine. Raises ValueError for what cannot
    be: no bundle, no engine, a floppy too small"""
    import os88mkdisk as M
    if disk is None:
        disk = M.DISK_OF_PROFILE.get(proj.d["target"]["profile"], 6)
    here = os.path.dirname(proj.path)
    eng = engine_path(build)
    if not eng:
        raise ValueError("no VBATTLE.O88 here to put beside it")
    files = [("VBATTLE.O88", eng)]
    if bundle:
        files.append(("BATTLE.VBT", bundle))
    else:                           # the renditions asked for, or every one
        for k in kinds or renditions_of(proj):      # built beside it
            f = os.path.join(here, BUNDLE_NAMES[k])
            if os.path.exists(f):
                files.append((BUNDLE_NAMES[k], f))
    if len(files) == 1 or not os.path.exists(files[1][1]):
        raise ValueError("no bundle: build it first")
    big = M.fits(files, disk)
    if big:
        raise ValueError("it needs %d KB and %s holds %d KB" % (
            big[0] // 1024, M.DISKS[disk][0], big[1] // 1024))
    label, kind, what = M.DISKS[disk]
    img = out or os.path.join(here, "BATTLE-%s" % (
        "%d.img" % what if kind == "fd" else "%s.VHD" % what[0]))
    argv, img = M.disk_of(files, disk, img, build, stage)
    note = None
    if kind == "hd" and M.hd_missing(build):
        note = ("It does not boot (no built os8088 here to put on it): boot "
                "os8088 from its system floppy, tick the hard-disk driver in "
                "the Control Panel, and it comes up as C:.")
    elif kind == "fd":
        note = "A floppy for B:, beside the system disk in A:."
    return argv, img, files, note


def cmd_disk(a):
    import json
    import subprocess
    import tempfile
    import vbtproj as P
    proj = P.Project.load(a.project)
    with tempfile.TemporaryDirectory() as stage:
        try:
            argv, img, files, note = disk_plan(proj, a.disk, a.bundle, a.out,
                                               stage=stage, kinds=a.rendition)
        except ValueError as e:
            raise SystemExit("os88vbt disk: %s" % e)
        r = subprocess.run(argv, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("os88vbt disk: %s" % (r.stdout + r.stderr).strip())
    o = dict(stage="done", out=img, files=[n for n, _ in files], note=note)
    print(json.dumps(o) if a.json else "%s: %s%s" % (
        img, " ".join(o["files"]), "\n" + note if note else ""), flush=True)


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
    kinds = [a.rendition] if a.rendition else renditions_of(proj)
    res = []
    for kind in kinds:
        data = build_project(proj, progress, kind)
        out = a.out if a.out and len(kinds) == 1 else os.path.join(
            os.path.dirname(proj.path), BUNDLE_NAMES[kind])
        with open(out, "wb") as f:
            f.write(data)
        bud = A.budget(proj, data)
        res.append(dict(kind=kind, out=out, bytes=len(data), budget=bud))
        if not a.json:
            print("%s (%s): %d bytes; idle loop %d bytes, entries %d" % (
                out, kind, len(data), bud["idle_bytes"], bud["entries"]))
            for c in bud["clips"]:
                print("  %-14s %7d bytes %5.1f KB/s (%3.0f%% of the disk) "
                      "anchors %s" % (c["name"], c["bytes"], c["rate"] / 1024,
                                      c["of_disk"] * 100, c["anchors"]))
    if a.json:
        r0 = res[0]
        say(dict(stage="done", out=r0["out"], bytes=r0["bytes"],
                 budget=r0["budget"], renditions=res))


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
    q.add_argument("--rendition", choices=("cga4", "vga4", "herc"),
                   default="cga4", help="the adapter it is for")
    q.set_defaults(fn=cmd_demo)
    q = sp.add_parser("build", help="build a project to a bundle")
    q.add_argument("project")
    q.add_argument("-o", "--out")
    q.add_argument("--json", action="store_true",
                   help="progress and the result as JSON lines")
    q.add_argument("-v", "--verbose", action="store_true",
                   help="also say what the engine does not play yet")
    q.add_argument("--rendition", choices=sorted(BUNDLE_NAMES),
                   help="this rendition alone (all the project's by default: "
                   "BATTLE.VBT, BATTLEV.VBT, BATTLEH.VBT)")
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
    q = sp.add_parser("disk", help="the built bundle and the engine on a "
                      "floppy or a hard disk")
    q.add_argument("project")
    q.add_argument("--disk", type=int, help="os88mkdisk.DISKS's number "
                   "(0-3 the floppies, 4-6 the hard disks); the profile's "
                   "by default")
    q.add_argument("--bundle", help="the bundle (BATTLE.VBT beside the "
                   "project by default)")
    q.add_argument("--rendition", action="append",
                   choices=sorted(BUNDLE_NAMES), help="this rendition's "
                   "bundle (again for more); every one built by default")
    q.add_argument("-o", "--out")
    q.add_argument("--json", action="store_true")
    q.set_defaults(fn=cmd_disk)
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
