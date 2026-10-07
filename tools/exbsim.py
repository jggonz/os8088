#!/usr/bin/env python3
"""exbsim: EXCITEBIKE's reference model (SPEC.md 102.6).  Wave 2 carries the
REFERENCE RENDERER; the physics (wave 3) lands in the same file.

The renderer is the video gate's oracle (plan 4.8).  It produces the expected
320 x 200 field from (course, scroll column, bike list, HUD text) with the
plan's world function

    W(x, l) = l < 64 ? top[x mod 64][l] : band[cid[x]][l - 64]     (l < 192)

and the pose art, WITHOUT any of the guest's memory tricks: no spiral layout,
no pages, no latches, no skip lists, no ring.  It is written from the SOURCE
text (tools/excitebike_assets.Art parses tiles.txt, pieces.txt, top.txt,
poses.txt, font.txt and the courses), never from the compiled GFX files, so a
mistake in the compiler's binary encoding or in the guest's decoding is a
difference and not a shared blind spot.  Standard library only; nothing is read
from a ROM or a disassembly.

    python3 tools/exbsim.py --selfcheck
    python3 tools/exbsim.py --render out.png [--course 0] [--col 100] [--cga]
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
import importlib.util  # noqa: E402


def _compiler():
    """tools/excitebike_assets.py by PATH: tests/excitebike_assets.py is another
    module with the same name, and whichever a caller's sys.path finds first
    must not decide which compiler the reference model reads."""
    spec = importlib.util.spec_from_file_location(
        "excitebike_assets_tool", os.path.join(HERE, "excitebike_assets.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["excitebike_assets_tool"] = mod
    spec.loader.exec_module(mod)
    return mod


X = _compiler()

XB_PH_V = 4                # the VGA sprite phases (x mod 8 in 0, 2, 4, 6)
COLS = 40                 # tile columns on screen
W_PX, H_PX = 320, 200
TOP_LINES = 64            # crowd + banner
BAND_LINES = 128          # 14 track tile rows + the hedge pair
WORLD_LINES = 192
HUD_COL = 10              # the HUD text starts at this tile column
HUD_LEN = 20
XH_COLS = 45              # the Hercules picture: 45 tile columns of two-card-pixel game pixels
XH_HUDCOL = 12
INK_SLOT = X.INK_SLOT     # pose ink k -> palette slot


class Ref:
    def __init__(self, art=None):
        self.art = art or X.Art()
        a = self.art
        self._band = {}
        self._top = {}
        self.plain = [a.col_ids[n] for n in ("plain_a", "plain_b", "plain_c")]
        self.cga_ink = a.palette["cga"]["p0_high"]
        # Hercules (SPEC.md 102.7): a slot's 2-bit PAIR of card pixels when the mid grey is a pose's (10)
        self.herc_ink = [X.HERC_PAIR[lv] for lv in a.palette["herc"]]

    # ---- the world ---------------------------------------------------------------
    def course_cids(self, course, flag=0):
        """The runway then both laps as dictionary column ids, exactly the
        stream the guest expands (43 alternating plain columns, then lap 1 and
        lap 2).  flag = 1, the second pass round the courses, rides the harder
        set (`pass2`) on BOTH laps."""
        a = self.art
        t = a.tracks[course]
        ids = [a.col_ids["plain_a" if i % 2 == 0 else "plain_b"] for i in range(X.RUNWAY)]
        for plist in ((t["pass2"], t["pass2"]) if flag else (t["pass1"], t["pass2"])):
            cols, _ = X.lap_columns(plist, a.pieces_by_name)
            ids += [a.col_ids[c] for c in cols]
        return ids

    def band_lines(self, cid, kind="vga"):
        """128 lines of 8 slots for band dictionary column `cid`, as adapter `kind` draws them
        (a `mono=` tile differs on the CGA and the Hercules, Art.rows)."""
        if (cid, kind) not in self._band:
            a = self.art
            name = a.col_order[cid]
            t14, hedge = a.cols[name]
            lines = []
            for t in list(t14) + list(hedge):
                lines += [list(r) for r in a.rows(t, kind)]
            assert len(lines) == BAND_LINES
            self._band[(cid, kind)] = lines
        return self._band[(cid, kind)]

    def top_lines(self, x, kind="vga"):
        """64 lines of 8 slots for world column x (the picture repeats every 64)."""
        c = x % X.__dict__.get("TOPW", 64)
        if (c, kind) not in self._top:
            a = self.art
            lines = []
            for r in range(8):
                lines += [list(row) for row in a.rows(a.top[r][c], kind)]
            self._top[(c, kind)] = lines
        return self._top[(c, kind)]

    def world_px(self, cids, x, line, kind="vga"):
        """The 8 slots of world column x at pixel line `line` (0..191)."""
        if line < TOP_LINES:
            return self.top_lines(x, kind)[line]
        return self.band_lines(cids[x], kind)[line - TOP_LINES]

    # ---- sprites -----------------------------------------------------------------
    def pose_rows(self, pose):
        return self.art.poses[pose][1]

    def draw_pose(self, img, pose, x, y, slot_of_ink, xlimit=W_PX):
        """Draw a 24 x 24 pose with its top-left at pixel (x, y).  `slot_of_ink`
        maps pose ink 0..3 to the value written (a palette slot, or a CGA ink)."""
        for r, row in enumerate(self.pose_rows(pose)):
            yy = y + r
            if not 0 <= yy < WORLD_LINES:
                continue
            for c, ch in enumerate(row):
                if ch == ".":
                    continue
                xx = x + c
                if 0 <= xx < xlimit:
                    img[yy][xx] = slot_of_ink[int(ch)]

    # ---- HUD ---------------------------------------------------------------------
    def draw_hud(self, img, text, ink, col=HUD_COL):
        assert len(text) <= HUD_LEN
        for i, ch in enumerate(text):
            g = self.art.font[ord(ch.upper()) - 32]
            # glyph row 0 sits on the HUD's SECOND pixel row: which scan line the
            # VGA's split starts on is the emulator's (and possibly the card's)
            # business (SPEC.md 102.1), so the first row is blank and page line
            # 192 is kept black, and glyph row 7 is blank in every glyph
            for y in range(7):
                for x in range(8):
                    if g[y] & (0x80 >> x):
                        img[193 + y][(col + i) * 8 + x] = ink

    # ---- frames ------------------------------------------------------------------
    def frame_herc(self, cids, S, bikes=(), hud=""):
        """The Hercules field: 45 columns (360 game pixels) of 2-bit card-pixel PAIRS.  A tile's mid
        level is 10 or 01 by (line + column) parity - a checkerboard - and a pose's is always 10
        (tools/excitebike_assets.py herc_tile: a pose lands on any row, a tile on a fixed one)."""
        lv = self.art.palette["herc"]
        HP = X.HERC_PAIR
        W = 8 * XH_COLS
        img = [[0] * W for _ in range(H_PX)]
        for c in range(XH_COLS):
            x = S + c
            for line in range(WORLD_LINES):
                slots = self.world_px(cids, x, line, "herc")
                row = img[line]
                for k in range(8):
                    l = lv[slots[k]]
                    row[c * 8 + k] = (2 if (line + c * 8 + k) % 2 == 0 else 1) if l == 1 else HP[l]
        pal = self.herc_ink
        for b in bikes:
            slots = [pal[s] for s in INK_SLOT]
            self.draw_pose(img, b["pose"], b["x"] & ~1, b["y"], slots, xlimit=W)
        self.draw_hud(img, hud, 3, XH_HUDCOL)
        return img

    def frame(self, cids, S, bikes=(), hud="", cga=False, herc=False):
        if herc:
            return self.frame_herc(cids, S, bikes, hud)
        """The 320 x 200 field.  VGA: palette slots.  CGA: 2-bit inks (the
        tiles through p0_high, the poses through the same map, HUD ink 3).
        `bikes` = [{"pose": n, "x": px, "y": px, "rider": slot}] with x, y
        SCREEN pixels (even x; the guest forces it)."""
        img = [[0] * W_PX for _ in range(H_PX)]
        for c in range(COLS):
            x = S + c
            for line in range(WORLD_LINES):
                img[line][c * 8:c * 8 + 8] = self.world_px(cids, x, line, "cga" if cga else "vga")
        if cga:
            m = self.cga_ink
            img = [[m[v] for v in row] for row in img]
        for b in bikes:
            slots = list(INK_SLOT)
            slots[1] = b.get("rider", INK_SLOT[1])
            if cga:
                slots = [self.cga_ink[s] for s in slots]
                slots[1] = self.cga_ink[INK_SLOT[1]]
            self.draw_pose(img, b["pose"], b["x"] & ~1, b["y"], slots)
        self.draw_hud(img, hud, 3 if cga else 15)
        return img


def png_rows(ref, img, theme=0, cga=False):
    a = ref.art
    if cga:
        pal = X.CGA_RGB_HIGH
        return [[c for v in row for c in pal[v]] for row in img]
    rgb = X.slot_rgb(a, theme)
    return [[c for v in row for c in rgb[v]] for row in img]


# =================================================================================
# THE SIMULATION (SPEC.md 102.3)
# =================================================================================
# One step = one NES-frame-length tick (60.1 a second).  Everything is integer, 16-bit
# words and one 32-bit position, so the guest's sim.inc can be compared with this
# file step for step (tests/excitebike_ref.py).  The rules are this project's own
# (plan section 0.3): speed steps, heat, lanes, ramps, landings and crashes are
# numbers in `PHYS` below, mirrored in apps/excitebike/const.inc as XP_<name>.

INP_U, INP_D, INP_L, INP_R, INP_A, INP_B = 1, 2, 4, 8, 0x10, 0x20

PHYS = dict(
    SPD_A=0x0320,       # throttle-only cap, 8.8 pixels a step (3.125)
    SPD_B=0x0340,       # turbo cap (3.25)
    SPD_MAX=0x0466,     # the most a kicker can give (4.4)
    SPD_MUD=0x00C0, SPD_ROUGH=0x0180,
    ACC_A=0x0018, ACC_B=0x003F,          # added every 4th step
    DEC=0x0038, DEC_AIR=0x001C, DEC_AIR_L=0x003C, DEC_ROUGH=0x0020, DEC_MUD=0x0040, DEC_CRASH=0x0030, DEC_STALL=0x0020,
    KICK=0x0080,        # a kicker's speed boost
    GRAV=0x0034, GRAV_L=0x0018,          # 8.8 pixels a step a step; Left held = floaty
    TEMP_MIN=8 * 256, TEMP_A=17 * 256, TEMP_MAX=32 * 256,
    HEAT_B=0x0F, HEAT_A=0x07, COOL=0x0B, HEAT_MUD=0x0F,
    STALL_STEPS=258, CRASH_STEPS=72, WHEELIE=13, SQUASH=4,
    LANE_MIN=7, LANE_MAX=0x39,
    PITCH_MAX=4, PITCH_MIN=-3,           # pitch relative to the ground, and in the air
    PT_GROUND=5, PT_AIR=3,               # steps between pitch steps, less one
    RISE_FLAT=0x0400, FALL_FLAT=0x0400,  # a step in the ground (a hurdle) moves this fast
    HHI_CLEAR=0x0C00, HLO_CLEAR=0x0300,
    A_MAX=0x6000, TAKEOFF_NUM=3,         # take-off vy = vg + vg/2
    BIKE_X=88, FEET0=118, CLOCK16=1664, SHADOW_MIN=0x1800,
    FINISH_BACK=3,      # the finish line is the third column from the end
    COL_AHEAD=12,       # the bike's column is S + 12
)

M_RIDE, M_AIR, M_CRASH, M_STALL = 0, 1, 2, 3
(C_NONE, C_PLAIN, C_ROUGH, C_MUD, C_HLO, C_HHI, C_ARROW, C_KICKER,
 C_RAMP, C_HILL, C_FINISH, C_GATE) = range(12)
LANE_CENTRES = (14, 26, 38, 50)
KEYFRAME = {"ANG": 0, "DEFER": 1, "H": 2}


def lane_row(lane):
    """Collision row 0 (nearest the camera) .. 5 (farthest) of a lane number."""
    if lane < 16:
        return 5
    return max(0, 5 - ((lane - 8) >> 3))


def slope_pitch(sc):
    return sc if sc < 4 else 3 - sc


class Sim:
    """The player's bike over one course.  `art` is the parsed source (Art)."""

    def __init__(self, art, course, flag=0, cols=COLS):
        self.art = art
        a = art
        ref = Ref(a)
        self.flag = flag
        self.cid = ref.course_cids(course, flag)
        self.ncols = len(self.cid)
        self.smax = self.ncols - cols
        self.posmax = (8 * self.ncols - 112) << 8    # the bike's box must stay on the screen
        t = a.tracks[course]
        laps = (t["pass2"], t["pass2"]) if flag else (t["pass1"], t["pass2"])
        self.len1 = len(X.lap_columns(laps[0], a.pieces_by_name)[0])
        self.len2 = len(X.lap_columns(t["pass2"], a.pieces_by_name)[0])
        self.lap2col = X.RUNWAY + self.len1
        # dictionary column -> six collision-row classes (what rec_coll's tiles are)
        self.ccls = [a.ccls_bytes()[6 * i:6 * i + 6] for i in range(len(a.col_order))]
        ids = {n: i + 1 for i, n in enumerate(a.scripts)}
        self.scripts = {}
        for n, i in ids.items():
            kf = []
            for x, kind, v in a.scripts[n]:
                kf.append((x, KEYFRAME[kind], v))
            self.scripts[i] = kf
        self.trig = []
        for lap, plist in enumerate(laps):
            base = X.RUNWAY + (0 if lap == 0 else self.len1)
            _, starts = X.lap_columns(plist, a.pieces_by_name)
            lst = []
            for x, p in starts:
                sn = a.pieces_by_name[p]["script"]
                if sn != "-":
                    lst.append((base + x, ids[sn]))
            self.trig.append(lst)
        self.reset()

    def reset(self):
        self.pos = 0                  # 8.8 pixels, 32 bit: the window's left edge
        self.speed = 0
        self.A = 0                    # height above the lane's ground, 8.8 pixels
        self.vy = 0
        self.gh = 0                   # ground height under the bike
        self.th = 0
        self.sc = 0                   # slope code of the terrain, ANGn
        self.vg = 0                   # the last sloped rise: what a take-off keeps
        self.pitch = 0
        self.ptimer = 0
        self.wcount = 0
        self.sqt = 0
        self.bounced = 0
        self.lane = 26
        self.ldir = 0
        self.mode = M_RIDE
        self.mtimer = 0
        self.temp = PHYS["TEMP_MIN"]
        self.stepn = 0
        self.col = PHYS["COL_AHEAD"]
        self.lastcol = self.col
        self.cls = C_PLAIN
        self.lap = 1
        self.fin = 0
        self.trig_i = 0
        self.trig_lap = 0
        self.scr = None               # (keyframe list, index)
        self.sbase = 0
        self.sdefer = 0
        self.lastfire = -1
        self.ct10 = 0
        self.cs = 0
        self.pose = 0
        self.events = []              # names, for the sound the guest will make (wave 5)

    # ---- helpers -------------------------------------------------------------------
    def _col_of(self):
        return (self.pos >> 11) + PHYS["COL_AHEAD"]

    def _cls_now(self):
        return self.ccls[self.cid[self.col]][lane_row(self.lane)]

    def rel_pitch(self):
        return self.pitch - slope_pitch(self.sc)

    def _crash(self):
        self.mode = M_CRASH
        self.mtimer = PHYS["CRASH_STEPS"]
        self.A = self.gh
        self.vy = 0
        self.vg = 0
        self.pitch = 0
        self.wcount = 0
        self.sqt = 0
        self.events.append("crash")

    # ---- one step ------------------------------------------------------------------
    def step(self, inp):
        P = PHYS
        n = self.stepn
        self.stepn = (n + 1) & 0xFFFF
        if self.fin:
            inp = 0
        # the game clock: 1.664 hundredths a step (60.1 steps/s = real time), in thousandths
        if not self.fin:
            self.ct10 += P["CLOCK16"]
            while self.ct10 >= 1000:
                self.ct10 -= 1000
                if self.cs < 59999:
                    self.cs += 1
        m = self.mode
        # ---- lanes: on the ground only
        if m in (M_RIDE, M_STALL):
            if inp & INP_U:
                if self.lane > P["LANE_MIN"]:
                    self.lane -= 1
                self.ldir = -1
            elif inp & INP_D:
                if self.lane < P["LANE_MAX"]:
                    self.lane += 1
                self.ldir = 1
            elif self.ldir:
                if self.lane in LANE_CENTRES:
                    self.ldir = 0
                else:
                    self.lane += self.ldir
                    if self.lane <= P["LANE_MIN"] or self.lane >= P["LANE_MAX"]:
                        self.ldir = 0
        self.cls = self._cls_now()
        # ---- engine heat
        t = self.temp
        if m == M_STALL:
            if t > P["TEMP_MIN"]:
                t = max(P["TEMP_MIN"], t - P["COOL"])
        else:
            if (inp & INP_B) and m in (M_RIDE, M_AIR):
                t += P["HEAT_B"]
            elif inp & INP_A:
                if t < P["TEMP_A"]:
                    t += P["HEAT_A"]
                elif t > P["TEMP_A"]:
                    t -= P["COOL"]
            else:
                if t > P["TEMP_MIN"]:
                    t = max(P["TEMP_MIN"], t - P["COOL"])
            if m == M_RIDE and self.cls == C_MUD:
                t += P["HEAT_MUD"]
            if t >= P["TEMP_MAX"]:
                t = P["TEMP_MAX"]
        self.temp = t
        if m == M_RIDE and t >= P["TEMP_MAX"]:
            self.mode = m = M_STALL
            self.mtimer = P["STALL_STEPS"]
            self.vg = 0                               # a stalled bike must not take off at stall end
            self.events.append("overheat")
        # ---- speed, every fourth step
        if (n & 3) == 0:
            if m == M_RIDE and not self.fin:
                cap = P["SPD_B"] if inp & INP_B else (P["SPD_A"] if inp & INP_A else 0)
                dec = P["DEC"]
                if self.cls == C_MUD:
                    cap = min(cap, P["SPD_MUD"])
                    dec = P["DEC_MUD"]
                elif self.cls == C_ROUGH:
                    cap = min(cap, P["SPD_ROUGH"])
                    dec = P["DEC_ROUGH"]
                acc = P["ACC_B"] if inp & INP_B else P["ACC_A"]
                if self.speed < cap:
                    self.speed = min(self.speed + acc, cap)
                elif self.speed > cap:
                    self.speed = max(self.speed - dec, cap)
            elif m == M_CRASH:
                self.speed = max(self.speed - P["DEC_CRASH"], 0)
            elif m == M_STALL or self.fin:
                self.speed = max(self.speed - P["DEC_STALL"], 0)
            elif m == M_AIR:                         # in the air: Right holds the speed, Left sheds it
                d = 0 if inp & INP_R else (P["DEC_AIR_L"] if inp & INP_L else P["DEC_AIR"])
                self.speed = max(self.speed - d, 0)
        # ---- pitch
        if m in (M_RIDE, M_AIR):
            if self.ptimer == 0:
                self.ptimer = P["PT_AIR"] if m == M_AIR else P["PT_GROUND"]
                if m == M_AIR:
                    if inp & INP_L:
                        if self.pitch < P["PITCH_MAX"]:      # (a pitch already past the stop stays)
                            self.pitch += 1
                    elif inp & INP_R:
                        if self.pitch > P["PITCH_MIN"]:
                            self.pitch -= 1
                else:
                    ps = slope_pitch(self.sc)
                    if (inp & INP_L) and self.pitch < ps + P["PITCH_MAX"]:
                        self.pitch += 1
                    elif not (inp & INP_L) and self.pitch > ps:
                        self.pitch -= 1
                    elif not (inp & INP_L) and self.pitch < ps:
                        self.pitch += 1
            else:
                self.ptimer -= 1
            if m == M_RIDE:
                if self.pitch - slope_pitch(self.sc) >= P["PITCH_MAX"]:
                    self.wcount += 1
                    if self.wcount >= P["WHEELIE"]:
                        self._crash()
                        m = self.mode
                else:
                    self.wcount = 0
        # ---- the world moves
        self.pos = self.pos + self.speed
        if self.pos > self.posmax:                   # the far end of the world: the barrier
            self.pos = self.posmax
            self.speed = 0
        col = self._col_of()
        self.col = col
        if col != self.lastcol:
            self.lastcol = col
            self._column(inp)
            m = self.mode
        # ---- vertical
        if m == M_RIDE:
            self._ground()
        elif m == M_AIR:
            self._air(inp)
        # ---- timers
        m = self.mode
        if m == M_CRASH:
            self.mtimer -= 1
            if self.mtimer == 0:
                self.mode = M_RIDE
                self.speed = 0
                self.pitch = 0
                self.ptimer = 0
                self.sqt = 0
        elif m == M_STALL:
            self.mtimer -= 1
            if self.mtimer == 0:
                self.mode = M_RIDE
                self.temp = P["TEMP_MIN"]
        elif m == M_RIDE and self.sqt:
            self.sqt -= 1
        # ---- the finish
        if (not self.fin and self.lap == 2 and
                col >= self.ncols - P["FINISH_BACK"]):
            self.fin = 1
            self.events.append("finish")
        self.pose = self._pose()

    def _column(self, inp):
        """The bike's column changed: lap, script triggers, keyframes, hazards."""
        P = PHYS
        col = self.col
        if self.lap == 1 and col >= self.lap2col:
            self.lap = 2
            self.trig_lap = 1
            self.trig_i = 0
            self.events.append("lap")
        lst = self.trig[self.trig_lap]
        while self.trig_i < len(lst) and lst[self.trig_i][0] == col:
            self.scr = [self.scripts[lst[self.trig_i][1]], 0]
            self.sbase = col
            self.sdefer = 0
            self.lastfire = -1
            self.trig_i += 1
        self.cls = self._cls_now()
        cls = self.cls
        m = self.mode
        if m in (M_RIDE, M_AIR) and not self.fin:
            if cls == C_HLO:
                if m == M_RIDE and self.A < P["HLO_CLEAR"] and self.rel_pitch() < 2:
                    self.speed -= self.speed >> 1
                    self.events.append("bump")
            elif cls == C_HHI:
                if self.A < P["HHI_CLEAR"] and not (m == M_RIDE and self.rel_pitch() >= 3):
                    self._crash()
            elif cls == C_ARROW:
                if self.temp > P["TEMP_MIN"]:
                    self.temp = P["TEMP_MIN"]
            elif cls == C_KICKER:
                if m == M_RIDE:
                    self.speed = min(self.speed + P["KICK"], P["SPD_MAX"])
        # the script
        if self.scr is not None:
            kf, i = self.scr
            off = col - self.sbase
            while True:
                if i >= len(kf):
                    if self.lastfire != col:
                        self.th = 0
                        self.sc = 0
                        self.scr = None
                    break
                x, kind, v = kf[i]
                if x + self.sdefer > off:
                    break
                i += 1
                self.lastfire = col
                if kind == 0:
                    self.sc = v
                elif kind == 1:
                    self.sdefer += v
                    break
                else:
                    self.th = v << 9
            if self.scr is not None:
                self.scr[1] = i

    def _rate(self, table_sc):
        """Ground speed of a slope: speed x (1/4, 1/2, 3/4) for a slope code 1..3."""
        s = self.speed
        if table_sc == 1:
            return s >> 2
        if table_sc == 2:
            return s >> 1
        return (s >> 1) + (s >> 2)

    def _ground(self):
        P = PHYS
        gh, th = self.gh, self.th
        if gh < th:
            sc = self.sc
            if 1 <= sc <= 3:
                d = self._rate(sc)
                self.gh = min(gh + d, th)
                self.vg = d
            else:
                self.gh = min(gh + P["RISE_FLAT"], th)
        elif gh > th:
            if self.vg:                              # the ground drops away from a slope
                self.mode = M_AIR
                self.vy = self.vg + (self.vg >> 1)
                self.vg = 0
                self.A = gh
                self.ptimer = P["PT_AIR"]
                self.bounced = 0
                self.gh = th                         # the air's ground is the target at once
                self.events.append("takeoff")
                return
            sc = self.sc
            if 4 <= sc <= 6:
                d = self._rate(sc - 3)
            else:
                d = P["FALL_FLAT"]
            self.gh = max(gh - d, th)
        self.A = self.gh

    def _air(self, inp):
        P = PHYS
        self.gh = self.th
        self.A += self.vy                            # signed words in the guest
        self.vy -= P["GRAV_L"] if inp & INP_L else P["GRAV"]
        if self.A > P["A_MAX"]:
            self.A = P["A_MAX"]
            if self.vy > 0:
                self.vy = 0
        if self.vy < 0 and self.A <= self.gh:
            self._land()

    def _land(self):
        P = PHYS
        rel = self.pitch - slope_pitch(self.sc)
        self.A = self.gh
        mag = abs(rel)
        if mag >= 3:
            self._crash()
            return
        if mag == 2 and not self.bounced:
            self.vy = (-self.vy) >> 1
            self.bounced = 1
            self.speed -= self.speed >> 3
            self.events.append("bounce")
            return
        self.mode = M_RIDE
        self.vy = 0
        self.vg = 0
        self.sqt = P["SQUASH"]
        self.ptimer = P["PT_GROUND"]
        self.events.append("land")

    def _pose(self):
        m = self.mode
        if m == M_CRASH:
            q = (PHYS["CRASH_STEPS"] - self.mtimer) >> 3
            if q < 3:
                return 19 + q
            if q < 5:
                return 17 + (q - 3)
            return 22 + ((q - 5) & 1)
        if m == M_STALL:
            return 0
        if m == M_AIR:
            p = self.pitch
            if p >= 2:
                return 13
            if p <= -2:
                return 14
            if self.vy < 0 and self.A - self.gh < 0x0A00:
                return 15
            return 12
        if self.sqt:
            return 16
        ps = slope_pitch(self.sc)
        rel = self.pitch - ps
        if rel > 0:
            return 1 + rel
        if ps > 0:
            return 8 + ps
        if ps < 0:
            return 5 - ps
        return (self.stepn >> 2) & 1

    # ---- what the renderer is told -----------------------------------------------
    def bikes(self, cga=False):
        """The bike records the guest's frame loop writes into xb_bikes:
        [player, shadow, dust], each {"pose", "x", "y"} or None. On the CGA
        (cga=True) the rider alone is drawn: the shadow and dust are omitted, a
        deliberate speed trade (SPEC.md 102.3, apps/excitebike/sim.inc)."""
        P = PHYS
        px = self.pos >> 8
        extra = px - 8 * self.smax
        x = P["BIKE_X"] + (extra if extra > 0 else 0)
        x &= ~1
        feet = P["FEET0"] + self.lane
        y = feet - 24 - (self.A >> 8)
        out = [{"pose": self.pose, "x": x, "y": y}]
        if cga:
            return out + [None, None]
        sh = None
        # only when the shadow's box and the rider's cannot overlap (24 pixels up): the
        # CGA back end composes a box from the world alone, so an overlap would erase
        # the rider under it (SPEC.md 102.3)
        if self.mode in (M_RIDE, M_AIR) and self.A - self.gh >= P["SHADOW_MIN"]:
            sh = {"pose": 24, "x": x, "y": feet - 24 - (self.gh >> 8)}
        out.append(sh)
        du = None
        m = self.mode
        if (m == M_CRASH and (P["CRASH_STEPS"] - self.mtimer) < 40) or (m == M_RIDE and self.sqt):
            du = {"pose": 25 + ((self.stepn >> 2) % 3), "x": max(x - 24, 0), "y": feet - 24}
        out.append(du)
        return out

    def hud(self):
        """The 20-cell HUD string: TIME m:ss.cc, the lap, the engine gauge (or BURNING)."""
        mm, r = divmod(self.cs, 6000)
        ss, cc = divmod(r, 100)
        s = "TIME %d:%02d.%02d " % (min(mm, 9), ss, cc)
        if self.mode == M_STALL:
            s += "BURNING"
        else:
            full = self.gauge()
            s += "%d " % self.lap + "\\" * full + "]" * (5 - full)
        assert len(s) == HUD_LEN, (s, len(s))
        return s

    def gauge(self):
        """Filled gauge cells 0..5: the temperature's whole units above the idle
        8, in fives, rounded up."""
        return (((self.temp >> 8) - 8) + 4) // 5

    def rec(self):
        """The 16-byte per-step trace record the guest writes (sim.inc xb_trace)."""
        import struct
        return struct.pack("<IHhhHBbBB", self.pos, self.speed, self.A, self.vy,
                           self.temp, self.mode, self.pitch, self.lane, self.pose)



# ---- the reference driver: how a competent rider plays (run-track, the lap tests) ---
BAD = (C_ROUGH, C_MUD, C_HLO, C_HHI)


def bot_input(sim, look=10):
    """Turbo, with the engine kept under the overheat, steering around anything
    that would slow or crash the bike, and levelling the bike in the air."""
    inp = INP_A
    heat_hi = PHYS["TEMP_MAX"] - 3 * 256
    heat_lo = PHYS["TEMP_A"] + 256
    if sim.temp < heat_hi and (sim.temp < heat_lo or not getattr(sim, "_cool", 0)):
        inp |= INP_B
        sim._cool = 0
    else:
        sim._cool = 1
    if sim.mode == M_AIR:
        if sim.pitch > slope_pitch(0) + 0:
            inp |= INP_R
        elif sim.pitch < 0:
            inp |= INP_L
        return inp
    if sim.mode != M_RIDE:
        return inp
    # lane choice: the centre whose next `look` columns hold the least trouble
    def cost(lane):
        row = lane_row(lane)
        c = 0
        for k in range(1, look + 1):
            x = sim.col + k
            if x < sim.ncols and sim.ccls[sim.cid[x]][row] in BAD:
                c += 4 if sim.ccls[sim.cid[x]][row] == C_HHI else 2
        return c
    here = min(LANE_CENTRES, key=lambda L: abs(L - sim.lane))
    best = here
    bc = cost(here) - 1                 # a little stubbornness: do not dither
    for L in LANE_CENTRES:
        c = cost(L) + abs(L - sim.lane) // 24
        if c < bc:
            best, bc = L, c
    if sim.ldir and sim.lane not in LANE_CENTRES:
        # already moving: finish the move to the centre in that direction
        tgt = sim.lane + (1 if sim.ldir > 0 else -1)
        inp |= INP_D if sim.ldir > 0 else INP_U
        return inp
    if best < sim.lane:
        inp |= INP_U
    elif best > sim.lane:
        inp |= INP_D
    return inp


def run_track(course, limit=40000, art=None, verbose=False, flag=0):
    """Drive a course to the finish at turbo: -> (finished, steps, cs, crashes).  `cs` is
    the game clock at the finish line, in hundredths (it stops there)."""
    sim = Sim(art or X.Art(), course, flag)
    crashes = 0
    for i in range(limit):
        sim.step(bot_input(sim))
        if sim.events:
            crashes += sim.events.count("crash")
            sim.events.clear()
        if sim.fin and sim.speed == 0:
            return True, i + 1, sim.cs, crashes
    return False, limit, sim.cs, crashes


def par_of(cs):
    """A course's par: the best turbo time plus 8% (plan 5), in hundredths."""
    return (cs * 108 + 50) // 100


def fmt_cs(cs):
    return "%d:%02d.%02d" % (cs // 6000, cs // 100 % 60, cs % 100)


def run_tracks(names, art=None):
    """--run-track: every named course finishes at turbo on BOTH passes, and its committed
    pars are those times + 8% (a par is a RESULT of the reference rider, not a guess)."""
    art = art or X.Art()
    files = [t["file"][:-4] for t in art.tracks]
    for name in names:
        c = files.index(name)
        for flag, key in ((0, "par"), (1, "par2")):
            ok, steps, cs, crashes = run_track(c, art=art, flag=flag)
            par = art.tracks[c][key]
            print("%s pass %d: %s in %d steps, %d crashes, turbo time %s, %s %s (turbo + 8%% = %s)" % (
                name, flag + 1, "FINISHED" if ok else "DID NOT FINISH", steps, crashes, fmt_cs(cs),
                key, fmt_cs(par), fmt_cs(par_of(cs))))
            assert ok, "%s pass %d: the reference rider did not finish at turbo" % (name, flag + 1)
            assert par == par_of(cs), ("%s: the committed %s %s is not the turbo time + 8%% (%s): "
                                       "edit tracks/%s.trk" % (name, key, fmt_cs(par), fmt_cs(par_of(cs)), name))


def selfcheck():
    ref = Ref()
    cids = ref.course_cids(0)
    assert len(cids) > 200
    a = ref.frame(cids, 0, [{"pose": 0, "x": 88, "y": 96}], "TIME 0:00.00 LAP 1/2")
    b = ref.frame(cids, 0, [{"pose": 0, "x": 88, "y": 96}], "TIME 0:00.00 LAP 1/2")
    assert a == b
    assert len(a) == 200 and all(len(r) == 320 for r in a)
    # the world function is shear-free: moving the window by one column moves
    # every world pixel left by exactly 8
    s0 = ref.frame(cids, 10)
    s1 = ref.frame(cids, 11)
    assert all(s0[y][8:] == s1[y][:-8] for y in range(WORLD_LINES))
    # a pose's opaque pixel count is what the compiler recorded
    for i, st in enumerate(ref.art.pose_stats):
        n = sum(1 for r in ref.pose_rows(i) for ch in r if ch != ".")
        assert n == st["opaque"], (i, n, st["opaque"])
    # HUD glyphs land where they should
    h = ref.frame(cids, 0, (), "T")
    assert any(h[193 + y][HUD_COL * 8 + x] == 15 for y in range(7) for x in range(8))
    assert all(h[192][x] == 0 for x in range(320))
    assert all(h[y][x] == 0 for y in range(192, 200) for x in range(0, HUD_COL * 8))
    # the rider simulation: the reference rider finishes both courses at turbo and each
    # par is that time + 8%
    run_tracks([t["file"][:-4] for t in ref.art.tracks], ref.art)
    print("exbsim selfcheck: ok (%d columns in course 1)" % len(cids))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--run-track", metavar="NAME", action="append",
                    help="drive a course (t1, t2) to the finish at turbo and check its par")
    ap.add_argument("--render", metavar="PNG")
    ap.add_argument("--course", type=int, default=0)
    ap.add_argument("--col", type=int, default=100)
    ap.add_argument("--cga", action="store_true")
    o = ap.parse_args()
    if o.selfcheck:
        selfcheck()
    if o.run_track:
        run_tracks(o.run_track)
    if o.render:
        ref = Ref()
        cids = ref.course_cids(o.course)
        img = ref.frame(cids, o.col, [{"pose": 0, "x": 88, "y": 96}],
                        "TIME 0:00.00 LAP 1/2", cga=o.cga)
        X.write_png(o.render, W_PX, H_PX, png_rows(ref, img, cga=o.cga))
        print("wrote", o.render)


if __name__ == "__main__":
    main()


# ---------------------------------------------------------------------------------
# the sprite blobs the guest's loader (sprite.inc) must produce, computed here from
# the pose SOURCE text - the test compares the guest's claim with these bytes
# ---------------------------------------------------------------------------------
def vga_blobs(art):
    """-> (table words, blob bytes list) for VGA: per (pose, phase idx 0..3) four
    layers of (count, [(offset, mask)...])."""
    out = []
    for pi, (name, body) in enumerate(art.poses):
        for k in range(4):
            ph = 2 * k
            layers = [[[0] * 4 for _ in range(24)] for _ in range(4)]     # layer, row, byte
            for r, row in enumerate(body):
                vals = [0, 0, 0, 0]                                       # opq, i1, i2, i3
                for c, ch in enumerate(row):
                    if ch == ".":
                        continue
                    bit = 1 << (31 - (c + ph))
                    vals[0] |= bit
                    if ch in "123":
                        vals[int(ch)] |= bit
                vals[0] &= ~(vals[1] | vals[2] | vals[3])                 # layer 0 = outline
                for q in range(4):
                    for b in range(4):
                        layers[q][r][b] = (vals[q] >> (24 - 8 * b)) & 0xFF
            recs = []
            for q in range(4):
                rr = []
                for r in range(24):
                    for b in range(4):
                        m = layers[q][r][b]
                        if m:
                            rr.append((r * 40 + b, m))
                recs.append(rr)
            out.append(recs)
    return out


def vga_blob_bytes(art):
    """The whole claim image: the 96-word table then the blobs."""
    blobs = vga_blobs(art)
    table = []
    body = bytearray()
    base = len(blobs) * 2
    for recs in blobs:
        table.append(base + len(body))
        for rr in recs:
            body += len(rr).to_bytes(2, "little")
            for off, m in rr:
                body += off.to_bytes(2, "little") + bytes([m])
    img = bytearray()
    for t in table:
        img += t.to_bytes(2, "little")
    return bytes(img + body)


XS_HOT_V = (0, 1, 12)                          # sprite.inc's xs_hot_v: the VGA's compiled poses (80% of a lap)


def vga_compiled(art):
    """sprite.inc's xs_compile_v, from the VGA blob image: -> (ctab, code).  ctab[pose * 4 + phase / 2]
    is the code's offset in the sprite claim (0 = the record loop draws it).  A record is
    `mov al,MASK / out dx,al` (the first, and each time the mask changes), `mov al,COLOUR` (layer 1:
    `mov al,bl`, the rider's colour) and `xchg al,[es:di+off]`; a pose ends in `retf`."""
    blob = vga_blob_bytes(art)
    base = len(blob)
    ctab = [0] * (len(art.poses) * XB_PH_V)
    code = bytearray()
    for pose in XS_HOT_V:
        for k in range(XB_PH_V):
            idx = pose * XB_PH_V + k
            off = struct.unpack_from("<H", blob, idx * 2)[0]
            ctab[idx] = base + len(code)
            last = None
            for layer in range(4):
                cnt = struct.unpack_from("<H", blob, off)[0]
                off += 2
                for _ in range(cnt):
                    o = struct.unpack_from("<H", blob, off)[0]
                    m = blob[off + 2]
                    off += 3
                    if m != last:
                        code += bytes([0xB0, m, 0xEE])
                        last = m
                    code += b"\x88\xd8" if layer == 1 else bytes([0xB0, INK_SLOT[layer]])
                    code += (b"\x26\x86\x45" + bytes([o])) if o < 128 else (b"\x26\x86\x85" + struct.pack("<H", o))
            code += b"\xcb"
    return ctab, bytes(code)


XS_HOT = (0, 1, 12, 15, 16, 9, 2, 11, 10)      # sprite.inc's xs_hot: 94% of a lap's frames


def compiled_poses(art, cga_ink=None):
    """sprite.inc's xs_compile, from the blob image: -> (ctab, code).  ctab[pose * 2 + k] is the code's
    offset in the sprite claim (0 = interpreted) and `code` is appended to cga_blob_bytes()'s image.
    A byte that changes nothing (AND FF, OR 0) is dropped; two neighbours that do something are one
    word op; AND 0 is a `mov` of the OR; a row is `add di,dx`; a pose ends in `retf`."""
    blob = cga_blob_bytes(art, cga_ink)
    base = len(blob)
    ctab = [0] * (len(art.poses) * 2)
    code = bytearray()

    def mod(d):
        return b"\x05" if d == 0 else bytes([0x45, d])
    for pose in XS_HOT:
        for k in range(2):
            idx = pose * 2 + k
            off = struct.unpack_from("<H", blob, idx * 2)[0]
            ctab[idx] = base + len(code)
            for r in range(24):
                if r:
                    code += b"\x01\xd7"
                first, cnt = blob[off], blob[off + 1]
                off += 2
                pairs = [(blob[off + 2 * i], blob[off + 2 * i + 1]) for i in range(cnt)]
                off += 2 * cnt
                d, i = first, 0
                while i < cnt:
                    a, o = pairs[i]
                    if (a, o) == (0xFF, 0):
                        i += 1
                        d += 1
                        continue
                    if i + 1 < cnt and pairs[i + 1] != (0xFF, 0):
                        a1, o1 = pairs[i + 1]
                        if a == 0 and a1 == 0:
                            code += b"\x26\xc7" + mod(d) + bytes([o, o1])
                        else:
                            code += b"\x26\x8b" + mod(d) + b"\x25" + bytes([a, a1])
                            if o | o1:
                                code += b"\x0d" + bytes([o, o1])
                            code += b"\x26\x89" + mod(d)
                        i += 2
                        d += 2
                        continue
                    if a == 0:
                        code += b"\x26\xc6" + mod(d) + bytes([o])
                    else:
                        code += b"\x26\x8a" + mod(d)
                        if a != 0xFF:
                            code += b"\x24" + bytes([a])
                        if o:
                            code += b"\x0c" + bytes([o])
                        code += b"\x26\x88" + mod(d)
                    i += 1
                    d += 1
            code += b"\xcb"
    return ctab, bytes(code)


def cga_blob_bytes(art, cga_ink=None):
    """The CGA claim image (sprite.inc's xs_load_cga): a 2 x poses word table of (pose * 2
    + phase / 2) offsets, then per (pose, phase) 24 rows of (first byte, count)
    followed by `count` (AND, OR) byte pairs - the row trimmed to the bytes that
    hold an opaque pixel.  Computed pixel by pixel from the pose text."""
    m = cga_ink or art.palette["cga"]["p0_high"]
    inks = [m[s] for s in INK_SLOT]
    table = []
    body = bytearray()
    base = len(art.poses) * 2 * 2
    for pi, (name, rows) in enumerate(art.poses):
        for k in range(2):
            table.append(base + len(body))
            ph = 2 * k
            nb = 6 + k
            for row in rows:
                andb = [0xFF] * nb
                orb = [0] * nb
                for c, ch in enumerate(row):
                    if ch == ".":
                        continue
                    q = c + ph
                    b = q >> 2
                    sh = 6 - 2 * (q & 3)
                    andb[b] &= ~(3 << sh) & 0xFF
                    orb[b] |= inks[int(ch)] << sh
                op = [i for i in range(nb) if andb[i] != 0xFF]
                if not op:
                    body += bytes([0, 0])
                    continue
                f, l = op[0], op[-1]
                body += bytes([f, l - f + 1])
                for i in range(f, l + 1):
                    body += bytes([andb[i], orb[i]])
    img = bytearray()
    for t in table:
        img += t.to_bytes(2, "little")
    return bytes(img + body)
