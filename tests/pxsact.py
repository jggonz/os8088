#!/usr/bin/env python3
"""PIXELSTEIN 3D's guards, doors and combat on the 5150 (SPEC.md 97.6, 97.8,
97.10; wave 3).

    python3 tests/pxsact.py [--machine os8088_5150_cga_gla] [--shots DIR]

Every leg reads the package's bss through pxslib and moves the world by
POKING it - an actor's position and facing, a door's state - the way the
sim would, then lets the sim run (px_simoff 0) and reads what it did:

  (a) A POKED GUARD SEES AND TURNS. E1M1's first guard, at (22,3), is faced
      NORTH and the eye put three tiles west of it in the hall, looking at
      it: its cell is crossed by the rays, the eye is not behind it on its
      axis, the line is clear, and within a few ticks it leaves STAND for
      ALERT and then CHASE (97.8's sight: the spotvis gate, the facing
      test, the tile walk); it TURNS to the eye - PXAC_ANG 2048, west,
      from 3072 - and chasing, it moves TOWARD the eye (its x falls).
  (b) IT SHOOTS. With the player vulnerable (px_god 0) and the guard a tile
      or two off, ~120 ticks of the sim land a shot: health under 100 (the
      hit chance at that distance is 208-240 of 256 a shot; "the ATTACK
      state seen" passed a px_act_shoot that landed nothing, review, wave
      3).
  (c) IT DIES. The player invulnerable again, the guard's hp poked to 2 and
      the eye turned to face it (its sprite covers the centre column, so
      px_aim names it), Ctrl fires the pistol - one shot a press - until
      its state is DIE or DEAD and its hp 0; a dead guard's cell has lost
      its PXC_ACTOR mark.
  (d) A BODY KEEPS A DOOR OPEN. A door poked OPEN with its timer at 0 and
      the corpse poked into its cell stays OPEN past PX_DOORHOLD (91 ticks,
      5 s: the close asks px_door_body and is refused); the corpse moved
      out, the same wait sees it CLOSING or SHUT with its position under
      256.
  (f) SPACE OPENS THE DOOR AHEAD, and Up walks the player into it, where
      the player's own body keeps it open.
  (g) A CHASING GUARD OPENS A SHUT DOOR AND COMES THROUGH IT - unpoked but
      for its state: E1M1's second guard put two tiles east of the hall's
      door with the eye two tiles west of it, CHASE; within 150 ticks the
      door has left SHUT and the guard's cell is at or past the door's,
      and THE DOOR CELL'S BYTE IS WHAT IT WAS (its material nibble is not
      a mark - the first cut read every steel door as PXC_PLAYER and no
      guard ever opened one, and a mark cleared there repainted the door;
      review, wave 3).
  (h) A LOCKED DOOR REFUSES WITHOUT THE KEY AND OPENS WITH IT: Space at the
      gold door (13,25) leaves it SHUT; px_keys poked to gold, Space opens
      it.
  (i) A PICKUP: the eye moved onto the ammo at (40,3) with its cell mark a
      tile behind, so the next step's px_pmark finds it - px_ammo rises by
      8 and the static's kind reads taken (0xFF).
  (m) A STILL EYE OWES NO FRAME FOR WHAT IT CANNOT SEE (review r1): with
      the eye still at the spawn, a guard poked PATROL into the W room
      behind the corridor's wall and a door out of view poked OPENING, 91
      ticks of the sim move both and px_frames does not move (px_act_dirty,
      px_door_dirty: a thinker owes a frame only for an actor the last cast
      could see or that stepped into its marks, a door only for a cell a
      ray crossed); the same patroller in the corridor ahead owes frames.
  (e) THE TWO-GUARDS-AT-MELEE FRAME, reported (97.6, PLAN 10's risk 8): in
      the bracket at the default rung, THE SIM RUNNING (the two chasers'
      walks and rolls in it; the first cut froze it), two guards poked a
      tile and two tiles in front of the spawn's eye, a full repaint's
      median of --frames cycle deltas - the frame the sprite cap exists
      for, expected ~6 fps before the cap. Reported, never gated. Beside it
      SEVEN CHASERS: every E1M1 guard poked CHASE at its spawn, scene A's
      finished frame - the sim tick with seven line-of-sight walks in it,
      priced against the finished frame with them standing (97.8). And
      (review, wave 6) ONE DOG and then TWO DOGS at the elbow, the same
      frame - the dog is the one new thing wave 6 draws in the world.
  (k) THE DIE WASH AND THE RESTART: health poked to 1, the player
      vulnerable, a guard a tile off in CHASE - the hit puts px_state at
      DYING, and PX_FADE ticks later the floor restarts: health 100, a life
      fewer, PLAY.
  (n) THE SECRET DOOR (wave 4's gate for wave 3's ungated SPECIAL-on-a-door
      cell, 97.8): E1M1's `s` - a door in the wall's own material - opens
      to Space like any door, its cell byte keeps the wall's material, and
      its first opening counts one of the floor's secrets (px_csec, 97.13).
  (p) THE KNIFE AT 0 AMMO: the rounds poked to 0 with the pistol chosen,
      a guard a tile ahead under the crosshair; Ctrl stabs - the rounds stay
      0, the frame drawn is the knife's (px_wnext 0..2) and the guard is
      hurt.
  (j) THE ELEVATOR SWITCH, inside the running bracket: Space at (2,22)'s
      switch ends the floor - the LEVELDONE card (97.13) - and THE CELL
      BYTE READS 13 IN BOTH LAYOUTS (material 14 -> 13, 0xD1 in px_map and
      px_mapT: wave 3 changed the byte and nothing read it back); Space on
      the card loads E1M2 (px_floor 1) between frames, READY, then PLAY, and
      frames go on being drawn on it.
  (q) THE DOG (wave 6): the fourth guard made a dog, four tiles down scene
      A's hall and chasing, the rest of the floor's actors out of the
      world for the leg - it closes FASTER than a guard runs (in the world's
      own steps, px_dtick - a window's frame caps them), it is drawn
      from the dog's frames alone (the bite among them), it BITES (the
      health falls), and ONE pistol round kills it for 200 points.
  (e2) A MELEE UNDER AUTO (review, wave 6 r2; reported): two guards at the
      elbow, the sim running, Auto at the 8086 bracket's start, 3 s of
      Delta-filled frames, and where Auto ends - the measurement the ladder
      question (SPEC.md 97.8, 97.15) waits on.
  (r) THE TAB MAP (wave 6): Tab in the window draws the seen cells from
      above - the player's cell the marker, a seen cell the floor's tone -
      and the world stops under it (px_dtick still); Tab takes it down and
      the world is drawn again; Game > Pause picked from the MENU over the
      map takes it down as P does and never runs the world behind it (the
      first cut toggled px_pause alone - review, wave 6); the generation
      wrapped by a poke, the marks FOLDED into px_seen keep the cell on the
      map. The player's marker is none of the backend's map tones (the
      first cut's 1bpp marker was every wall's lit tone - review, wave 6
      r2). (r3) A DOOR SEEN THROUGH ITS NEIGHBOURS IS ON THE MAP: door 0
      and then the secret door faced from their corridors, a frame cast,
      the door cell's own marks zeroed (a door walked past and never looked
      into) - its band bytes must be its material's lit tone, never unseen
      black: a black cell in a seen wall line gives a secret door away
      (review, wave 6 r2; the unfixed map drew it black).
  (o) THE SILVER LOCK, on E1M2: Space at its silver door with the GOLD key
      alone leaves it SHUT; with the silver key it opens. LAST, because the
      floor is a different one after (j).

--shots DIR writes the done-when screendumps off the final binary, on
whichever machine this runs on: door 0 OPEN with its jambs, the second
guard at four headings 1.5 tiles ahead (four distinct masters, one of them
mirrored), the ammo pickup a tile ahead, and the two-guard melee frame.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import os88mouse                                                # noqa: E402
import os88ui                                                   # noqa: E402
import pxslib                                                    # noqa: E402

STAND, PATROL, ALERT, CHASE, ATTACK, PAIN, DIE, DEAD = 1, 2, 3, 4, 5, 6, 7, 8
DOG = 1                         # PXAC_KIND (wave 6)
DOG_F0 = 31                     # PXS_D_WALK0: the dog's first frame (pxart.inc)
SHUT, OPENING, OPEN, CLOSING = 0, 1, 2, 3
DOORHOLD = 91
PXAF_ATTACK = 2
PXM_MAP = 4                     # pxhud.inc: the bar's "MAP - TAB TO PLAY"
FAIL = []


def check(ok, what):
    print("   %-66s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def ticks(g, n, limit=120.0):
    """Let the guest run n BIOS ticks."""
    t0 = g.ticks()
    os88marty.until(g.m, lambda mm: (g.ticks() - t0) & 0xFFFFFF >= n,
                    "%d ticks" % n, poll=0.1, limit=limit)


def watch(g, n, key, want, step=6, actor=0):
    """Run n ticks in steps, sampling an actor's `key`; True once it is in
    `want`."""
    seen = []
    for _ in range(0, n, step):
        ticks(g, step)
        v = g.actor(actor)[key]
        seen.append(v)
        if v in want:
            return True, seen
    return False, seen


def aim_seen(m, g, want, frames=600):
    """px_aim sampled EVERY VIDEO FRAME of guest time (review, wave 6 r2):
    a chasing guard and a leaping dog move in and out of the crosshair, and
    the first cut polled from the host - ~6 of the guest's frames between
    reads - so a target the crosshair held for a frame or two was missed
    (it failed twice in review r2's runs, and the shot after it killed the
    target at once). Returns `want` once seen, else the last value; the
    guest is left running."""
    m.pause()
    v = g.byte("px_aim")
    for _ in range(frames):
        if v == want:
            break
        m.advance(frames=1)
        v = g.byte("px_aim")
    m.run()
    return v


def tap(m, key, hold=3, g=None):
    m.key(key, down=True, up=False)
    ticks(g, hold)
    m.key(key, down=False, up=True)


def shot(m, a, name, wave=3):
    if not a.shots:
        return
    os.makedirs(a.shots, exist_ok=True)
    m.pause()
    w, h, pxl = m.fbuf(0)
    os88marty.write_png_rgb(os.path.join(a.shots, "wave%d-%s-%s.png" % (wave, a.machine, name)),
                            w, h, pxl)
    m.run()


def cell_byte(g, cell):
    return g.m.read(g.base + g.s["px_map"] + cell, 1)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--shots", help="write the done-when screendumps here")
    a = ap.parse_args()
    os.chdir(ROOT)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        g = pxslib.open_game(m)
        st = g.state()
        print("   PXSTEIN.O88: window %d, part 0 at %04x, tier %d, %d actors, %d doors, %d statics"
              % (g.win, g.seg, st["tier"], g.byte("px_nact"), g.byte("px_ndoors"),
                 g.byte("px_nstat")))
        g.sim(False)
        g.god(True)
        a0 = g.actor(0)
        check(a0["state"] == STAND and a0["cell"] == 3 * 64 + 22,
              "E1M1's first guard stands at (22,3) (state %d, cell %d)" % (a0["state"], a0["cell"]))

        # --- (a) it sees, it turns ------------------------------------------
        m.pause()
        g.actor_poke(0, dir=3, ang=3072)            # facing NORTH: the eye to
        g.eye_poke(19 * 256 + 128, 3 * 256 + 128, 0)    # the west is not behind
        g.pcell_poke(19 * 256 + 128, 3 * 256 + 128)     # it on that axis
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        g.sim(True)
        ok, seen = watch(g, 60, "state", (ALERT, CHASE, ATTACK))
        check(ok, "the guard SAW the player: STAND -> ALERT/CHASE within 60 ticks (%s)" % seen)
        a1 = g.actor(0)
        check(a1["ang"] == 2048 and a1["dir"] == 2,
              "...and TURNED to face it: ang 3072 -> %d, dir 3 -> %d" % (a1["ang"], a1["dir"]))
        ok, seen = watch(g, 60, "state", (CHASE, ATTACK))
        check(ok, "...and went to CHASE (%s)" % seen)
        x0 = a0["x"]
        for _ in range(12):                   # up to 72 ticks: an attack
            ticks(g, 6)                       # cycle (13 ticks) stands still,
            if g.actor(0)["x"] < x0:          # and the chase's roll may land
                break                         # one first (97.8: 64 / dist)
        a1 = g.actor(0)
        print("   the guard: x %d -> %d, y %d -> %d, ang %d, dir %d, state %d, hp %d"
              % (x0, a1["x"], a0["y"], a1["y"], a1["ang"], a1["dir"], a1["state"], a1["hp"]))
        check(a1["x"] < x0, "chasing, it moved TOWARD the eye (x %d -> %d)" % (x0, a1["x"]))
        check(a1["ang"] == 2048 and a1["dir"] == 2, "...facing west (ang %d, dir %d)"
              % (a1["ang"], a1["dir"]))

        # --- (b) it shoots ------------------------------------------------------
        # THE HEALTH AT 255 FIRST: a hit is up to 63 under two tiles, two can
        # land inside the six ticks a sample spans, and a death here restarts
        # the floor under every leg after it (wave 4's first run did exactly
        # that: 100 -> 16, then DYING, then guard 0 back at its spawn)
        m.pause()
        g.poke_byte("px_health", 255)
        m.run()
        g.god(False)
        h0 = g.player()["health"]
        attacked = False
        for _ in range(120):
            ticks(g, 1)
            if g.actor(0)["state"] == ATTACK:
                attacked = True
            if g.player()["health"] < h0:
                break
        g.god(True)
        h1 = g.player()["health"]
        print("   health %d -> %d; the ATTACK state %s" % (h0, h1, "seen" if attacked else "not seen"))
        check(h1 < h0, "the guard SHOT the player: health fell (%d -> %d)" % (h0, h1))
        check(g.player()["state"] == 0, "...and the player is still alive (state %d)"
              % g.player()["state"])
        m.pause()
        g.poke_byte("px_health", 100)
        m.run()

        # --- (c) it dies ------------------------------------------------------
        m.pause()
        a1 = g.actor(0)
        # the eye a tile and a half west of the guard, looking straight at it
        ex, ey = a1["x"] - 384, a1["y"]
        g.actor_poke(0, hp=2, state=CHASE)
        g.eye_poke(ex, ey, 0)
        g.pcell_poke(ex, ey)
        g.poke_byte("px_ammo", 99)
        g.poke_byte("px_weapon", 1)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        # A FRAME IN FLIGHT when the pokes landed completes first and counts
        # (its pose the old one, its aim 255); the poked pose's frame follows
        # it - the force is kept since wave 4 (97.13) - so the aim is waited
        # for rather than read at the first frame
        aim = aim_seen(m, g, 0)
        check(aim == 0, "the sprite pass aims at the guard under the crosshair (px_aim %d)" % aim)
        dead = False
        for k in range(10):
            tap(m, "ControlLeft", 3, g)
            ticks(g, 6)
            a2 = g.actor(0)
            if a2["state"] in (DIE, DEAD):
                dead = True
                break
        a2 = g.actor(0)
        print("   after %d shots: state %d, hp %d, ammo %d" % (k + 1, a2["state"], a2["hp"],
                                                                g.player()["ammo"]))
        check(dead, "the guard DIED under the pistol (state %d)" % a2["state"])
        check(a2["hp"] == 0, "...its hp is 0 (%d)" % a2["hp"])
        check(g.player()["ammo"] < 99, "...and rounds were spent (%d)" % g.player()["ammo"])
        mark = cell_byte(g, a2["cell"])
        check(not (mark & 0x20), "a body's cell has lost its PXC_ACTOR mark (%02x)" % mark)
        ok, seen = watch(g, 40, "state", (DEAD,))
        check(ok, "...and the die frames ended in DEAD (%s)" % seen)

        # --- (d) a body keeps a door open ------------------------------------------
        d0 = g.door(0)
        cell = d0["cell"]
        dbyte = cell_byte(g, cell)
        cx, cy = (cell & 63) * 256 + 128, (cell >> 6) * 256 + 128
        m.pause()
        g.actor_poke(0, x=cx, y=cy)                  # the corpse into the door
        g.door_poke(0, pos=256, state=OPEN, timer=0)
        m.run()
        ticks(g, DOORHOLD + 30)
        d1 = g.door(0)
        print("   door 0 at cell %d with the body in it after %d ticks: state %d, pos %d, timer %d"
              % (cell, DOORHOLD + 30, d1["state"], d1["pos"], d1["timer"]))
        check(d1["state"] == OPEN and d1["pos"] == 256,
              "a body in the door keeps it OPEN past PX_DOORHOLD (state %d, pos %d)"
              % (d1["state"], d1["pos"]))
        m.pause()
        g.actor_poke(0, x=cx + 512, y=cy)            # ...and two tiles away
        g.door_poke(0, timer=0)
        m.run()
        ticks(g, DOORHOLD + 30)
        d2 = g.door(0)
        print("   ...the body moved out: state %d, pos %d" % (d2["state"], d2["pos"]))
        check(d2["state"] in (CLOSING, SHUT) and d2["pos"] < 256,
              "the door closes once the body is out (state %d, pos %d)" % (d2["state"], d2["pos"]))

        # --- (f) SPACE OPENS THE DOOR AHEAD, and the player walks through -----------
        # the eye a tile west of door 0 (the hall's door at (13,3)), facing
        # east; Space through the keyboard; the door's record leaves SHUT and
        # slides to 256 (px_use -> px_door_of -> px_door_use, 97.8); Up held
        # then carries the eye INTO the door cell (px_pblk passes at 128).
        # The first cut's row table lagged a row and px_door_of found no
        # door from its own cell, which nothing before this leg could see
        m.pause()
        g.actor_poke(0, x=cx + 512, y=cy + 512)      # the corpse out of the way
        dx0, dy0 = (cell & 63) * 256 + 128 - 256, (cell >> 6) * 256 + 128
        g.eye_poke(dx0, dy0, 0)
        g.pcell_poke(dx0, dy0)
        g.door_poke(0, pos=0, state=SHUT, timer=0)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        tap(m, "Space", 3, g)
        opened = False
        for _ in range(10):
            ticks(g, 3)
            d = g.door(0)
            if d["state"] in (OPENING, OPEN):
                opened = True
                break
        check(opened, "Space in front of door 0 opens it (state %d, pos %d)" % (d["state"], d["pos"]))
        ticks(g, 20)
        d = g.door(0)
        check(d["state"] == OPEN and d["pos"] == 256, "...and it slid fully open (state %d, pos %d)"
              % (d["state"], d["pos"]))
        m.key("ArrowUp", down=True, up=False)       # Up until the eye is a
        os88marty.until(m, lambda mm: g.eye()[0] >= (cell & 63) * 256 + 32,
                        "the eye in the door cell", poll=0.02, limit=60.0)
        m.key("ArrowUp", down=False, up=True)       # few units into cell 13:
        ticks(g, 3)                                 # released within ~150
        ex, ey, eh = g.eye()                        # units, and the cell is 256
        check((ex >> 8) == (cell & 63), "Up held walks the player INTO the open door's cell "
              "(x %d -> %d, cell %d)" % (dx0, ex, cell & 63))
        ticks(g, DOORHOLD + 20)
        d = g.door(0)
        check(d["state"] == OPEN, "...where the player's body keeps it open past the hold (state %d)"
              % d["state"])
        check(cell_byte(g, cell) == dbyte, "the door cell's byte is what it was with the player "
              "in it (%02x, was %02x): no mark on a door cell" % (cell_byte(g, cell), dbyte))

        # --- (g) a chasing guard opens a shut door and comes through ---------------
        # the second guard (alive, unpoked but for its state) two tiles EAST
        # of door 0, the eye two tiles WEST of it and out of the doorway; the
        # door shut. Its chase walks it west, the shut door blocks it and it
        # OPENS it (px_act_cell's door arm), waits for PX_DOORPASS and comes
        # through - the first cut never reached that arm (97.8)
        m.pause()
        g.eye_poke(dx0 - 256, dy0, 0)
        g.pcell_poke(dx0 - 256, dy0)
        g.door_poke(0, pos=0, state=SHUT, timer=0)
        g.actor_poke(1, x=cx + 512, y=cy, state=CHASE, dir=2, ang=2048, hp=25, flags=PXAF_ATTACK)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        opened = False
        through = False
        for _ in range(25):
            ticks(g, 6)
            d = g.door(0)
            b = g.actor(1)
            if d["state"] != SHUT:
                opened = True
            if (b["cell"] & 63) <= (cell & 63):
                through = True
                break
        b = g.actor(1)
        d = g.door(0)
        print("   the chaser: x %d -> %d (door x %d), state %d; door 0 state %d, pos %d"
              % (cx + 512, b["x"], cell & 63, b["state"], d["state"], d["pos"]))
        check(opened, "a chasing guard OPENED the shut door in its way (state %d)" % d["state"])
        check(through, "...and came through it: its cell x %d <= the door's %d"
              % (b["cell"] & 63, cell & 63))
        check(cell_byte(g, cell) == dbyte, "the door cell's byte is what it was after the guard "
              "(%02x, was %02x)" % (cell_byte(g, cell), dbyte))
        m.pause()
        g.actor_poke(1, x=cx + 1024, y=cy + 512, state=STAND)     # out of the way
        m.run()

        # --- (h) a locked door refuses, then opens with the key --------------------
        gold = [i for i, d in enumerate(g.doors()) if d["lock"] == 1][0]
        dg = g.door(gold)
        gcell = dg["cell"]
        gx, gy = (gcell & 63) * 256 + 128 - 256, (gcell >> 6) * 256 + 128
        m.pause()
        g.eye_poke(gx, gy, 0)
        g.pcell_poke(gx, gy)
        g.poke_byte("px_keys", 0)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        tap(m, "Space", 3, g)
        ticks(g, 12)
        d = g.door(gold)
        check(d["state"] == SHUT and d["pos"] == 0, "Space at the gold door %d without the key "
              "leaves it SHUT (state %d, pos %d)" % (gold, d["state"], d["pos"]))
        m.pause()
        g.poke_byte("px_keys", 1)
        m.run()
        tap(m, "Space", 3, g)
        ticks(g, 12)
        d = g.door(gold)
        check(d["state"] in (OPENING, OPEN) and d["pos"] > 0, "...and with the gold key it opens "
              "(state %d, pos %d)" % (d["state"], d["pos"]))

        # --- (i) a pickup --------------------------------------------------------
        st0 = [i for i in range(g.byte("px_nstat"))
               if g.bytes_("px_stat", 4 * (i + 1))[4 * i + 2] == 0][0]     # the first ammo
        sc = pxslib.u16(g.bytes_("px_stat", 4 * (st0 + 1)), 4 * st0)
        sx, sy = (sc & 63) * 256 + 128, (sc >> 6) * 256 + 128
        m.pause()
        g.poke_byte("px_ammo", 8)
        g.eye_poke(sx, sy, 0)                       # ON the ammo...
        g.pcell_poke(sx - 256, sy)                  # ...with the mark a tile behind
        g.force_all_poke()
        m.run()
        ticks(g, 6)
        kind = g.bytes_("px_stat", 4 * (st0 + 1))[4 * st0 + 2]
        check(g.player()["ammo"] == 16, "stepping onto the ammo at cell %d took it: 8 -> %d rounds"
              % (sc, g.player()["ammo"]))
        check(kind == 0xFF, "...and the static reads taken (kind %02x)" % kind)

        # --- (n) THE SECRET DOOR (SPECIAL on a door cell, 97.8, 97.13) ---------
        sec = [i for i, d in enumerate(g.doors()) if d["flags"] & 8]
        check(len(sec) >= 1, "E1M1 has a secret door (%d)" % len(sec))
        if sec:
            ds = g.door(sec[0])
            scell = ds["cell"]
            sbyte = cell_byte(g, scell)
            if ds["flags"] & 4:                     # DOOR_EW: the passage runs N-S
                ex, ey, eh = (scell & 63) * 256 + 128, (scell >> 6) * 256 + 128 - 256, 1024
            else:
                ex, ey, eh = (scell & 63) * 256 + 128 - 256, (scell >> 6) * 256 + 128, 0
            if g.m.read(g.base + g.s["px_map"] + ((ey >> 8) << 6 | (ex >> 8)), 1)[0] & 1:
                ex, ey, eh = ((ex + 512, ey, 2048) if eh == 0 else (ex, ey + 512, 3072))
            m.pause()
            g.eye_poke(ex, ey, eh)
            g.pcell_poke(ex, ey)
            csec0 = g.byte("px_csec")
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            tap(m, "Space", 3, g)
            ticks(g, 12)
            d = g.door(sec[0])
            print("   the secret door %d at (%d,%d): byte %02x, state %d, pos %d, secrets %d -> %d of %d"
                  % (sec[0], scell & 63, scell >> 6, sbyte, d["state"], d["pos"], csec0,
                     g.byte("px_csec"), g.byte("px_nsec")))
            check(sbyte >> 4 not in (0, 12) and sbyte & 8,
                  "its cell is a door in the WALL'S material with SPECIAL (%02x)" % sbyte)
            check(d["state"] in (OPENING, OPEN) and d["pos"] > 0,
                  "Space opens the secret door (state %d, pos %d)" % (d["state"], d["pos"]))
            check(g.byte("px_csec") == csec0 + 1, "...and it counts one secret (%d -> %d)"
                  % (csec0, g.byte("px_csec")))
            check(cell_byte(g, scell) & 0xF0 == sbyte & 0xF0,
                  "...its material nibble unchanged (%02x)" % cell_byte(g, scell))

        # --- (p) THE KNIFE AT 0 AMMO (97.8: the knife is drawn and used) ------
        m.pause()
        px, py, head = pxslib.scene_at("a")
        g.eye_poke(px, py, head)
        g.pcell_poke(px, py)
        sp2 = g.actor(2)
        g.actor_poke(2, x=px + 256, y=py, state=STAND, hp=25, ang=2048, dir=2, flags=0)
        g.poke_byte("px_ammo", 0)
        g.poke_byte("px_weapon", 1)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        aim = g.byte("px_aim")
        check(aim == 2, "the guard a tile ahead is under the crosshair (px_aim %d)" % aim)
        wn = []
        for _ in range(8):
            tap(m, "ControlLeft", 3, g)
            ticks(g, 4)
            wn.append(g.byte("px_wnext"))
            if g.actor(2)["hp"] < 25:
                break
        a2 = g.actor(2)
        print("   the knife: hp 25 -> %d, ammo %d, weapon frames seen %s"
              % (a2["hp"], g.byte("px_ammo"), wn))
        check(g.byte("px_ammo") == 0, "stabbing spends no rounds (ammo %d)" % g.byte("px_ammo"))
        check(all(f < 3 for f in wn), "the frame drawn is the KNIFE's, not the pistol's (%s)" % wn)
        check(a2["hp"] < 25 or a2["state"] in (PAIN, DIE, DEAD),
              "...and the knife hurts the guard (hp %d, state %d)" % (a2["hp"], a2["state"]))
        m.pause()
        g.actor_poke(2, x=sp2["x"], y=sp2["y"], state=STAND, hp=25, dir=sp2["dir"], ang=sp2["ang"],
                     flags=0)
        g.poke_byte("px_ammo", 8)
        m.run()

        # --- (m) A STILL EYE OWES NO FRAME FOR WHAT IT CANNOT SEE (review r1) ----
        # the eye at the spawn looking east, nothing pressed; the third guard
        # poked into the W room (5,9) as a PATROLLER walking east - behind
        # the corridor's wall, no ray reaches its cells - and the door at
        # (11,21) poked OPENING: over 91 ticks of the running sim the
        # patroller walks and turns and the door slides fully open, and
        # px_frames does not move (the first cut cast a frame a tick for
        # either). Then the same patroller in the corridor ahead: frames
        m.pause()
        g._mark()
        px, py, head = pxslib.scene_at("a")
        g.eye_poke(px, py, head)
        g.pcell_poke(px, py)
        sp2 = g.actor(2)
        g.actor_poke(2, x=5 * 256 + 128, y=9 * 256 + 128, state=PATROL, dir=0, ang=0, hp=25,
                     flags=1)                                       # PXAF_PATROL
        hid = [i for i, d in enumerate(g.doors()) if d["cell"] == 21 * 64 + 11][0]
        g.door_poke(hid, pos=0, state=OPENING, timer=0)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        ticks(g, 3)                                 # the poke's frame drawn, its keys spent
        f0 = g.word("px_frames")
        dseen, walked = set(), False
        for _ in range(15):                         # 90 ticks in steps: the door's
            ticks(g, 6)                             # whole cycle is 16 + 91 + 16, and
            d = g.door(hid)                         # the host's polling runs long
            dseen.add((d["state"], d["pos"]))
            b = g.actor(2)
            walked = walked or (b["x"], b["y"]) != (5 * 256 + 128, 9 * 256 + 128)
        f1 = g.word("px_frames")
        print("   still eye, the patroller at (%d,%d) dir %d, the hidden door %d saw %s: "
              "px_frames %d -> %d over 90 ticks" % (b["x"] >> 8, b["y"] >> 8, b["dir"], hid,
                                                      sorted(dseen), f0, f1))
        check(walked, "the unseen patroller walked (x %d, y %d, dir %d)" % (b["x"], b["y"], b["dir"]))
        check((OPEN, 256) in dseen, "the unseen door slid open (saw %s)" % sorted(dseen))
        check(f1 == f0, "...and a still eye drew NO frame for either (px_frames %d -> %d)" % (f0, f1))
        m.pause()
        g.actor_poke(2, x=9 * 256 + 128, y=3 * 256 + 128, state=PATROL, dir=0, ang=0)   # in view
        m.run()
        ticks(g, 30)
        f2 = g.word("px_frames")
        check(f2 > f1, "...and the same patroller in the corridor ahead owes frames (%d -> %d)"
              % (f1, f2))
        m.pause()
        g.actor_poke(2, x=sp2["x"], y=sp2["y"], state=STAND, dir=sp2["dir"], ang=sp2["ang"], flags=0)
        m.run()

        # --- (q) THE DOG (wave 6, 97.8): fast, melee, one hit -------------------
        # the fourth guard made a DOG (kind 1, hp 1) four tiles ahead of the
        # eye down scene A's hall, CHASING; every other actor put out of the
        # world for the leg (PXAS_NONE, restored after) so nothing else can
        # hurt the player. It must CLOSE at its own speed - four tiles to the
        # elbow in fewer ticks than a guard's run (24) could - be DRAWN from
        # the dog's frames (PXS_D_*: 31..41, the bite 39), LEAP and BITE (the
        # health falls, the player vulnerable), and die to ONE pistol shot
        # (+200)
        import pxsart                                   # noqa: E402
        check(pxsart.D_WALK0 == DOG_F0, "the dog's frames start at %d (tools/pxsart.py's "
              "D_WALK0 %d, pxart.inc's PXS_D_WALK0)" % (DOG_F0, pxsart.D_WALK0))
        m.pause()
        g._mark()
        px, py, head = pxslib.scene_at("a")
        g.eye_poke(px, py, head)
        g.pcell_poke(px, py)
        keep = [g.actor(i) for i in range(g.byte("px_nact"))]
        for i in range(g.byte("px_nact")):
            if i != 3:
                g.actor_poke(i, state=0)
        g.actor_poke(3, x=px + 4 * 256, y=py, kind=DOG, state=CHASE, hp=1, ang=2048, dir=2,
                     flags=PXAF_ATTACK, frame=0, timer=0)
        g.poke_byte("px_health", 255)
        g.poke_byte("px_weapon", 1)
        g.poke_byte("px_ammo", 99)
        g.force_all_poke()
        g.poke_byte("px_god", 0)                    # vulnerable, and the clock
        dt0, x0 = g.word("px_dtick"), px + 4 * 256  # read PAUSED at the poke: the
                                                    # first cut read it after a
                                                    # host-timed frame wait, the
                                                    # dog already closing (it
                                                    # read 680 "a step")
        frames, bit, speed = set(), False, None
        # SAMPLED IN GUEST TIME (review, wave 6 r2): the first cut polled from
        # the host, whose gap is a different number of the world's steps on
        # every run (it read 40, 80 and 680 units a step), and one soak's
        # first sample already found the dog at melee - "no sample". Two
        # video frames a sample (~0.6 of a world step), paused between, and
        # the speed taken from the first sample the dog has MOVED in, while
        # it is still closing (x0 - x under the three tiles to the elbow)
        for _ in range(600):
            m.advance(frames=2)
            for fr, fl, act in g.candidates():
                if act == 3:
                    frames.add(fr)
            d = g.actor(3)
            dt = (g.word("px_dtick") - dt0) & 0xFFFF
            if speed is None and dt and x0 - 3 * 256 < d["x"] < x0:
                speed = (x0 - d["x"]) / float(dt)
            if g.player()["health"] < 255:
                bit = True
            if bit and DOG_F0 + 8 in frames:
                break
        m.run()
        g.god(True)
        d = g.actor(3)
        print("   the dog: %s units a step closing (a guard runs %d, the player walks %d); "
              "state %d at x %d; frames drawn %s; health 255 -> %d"
              % ("%.1f" % speed if speed else "-", 24, 24, d["state"], d["x"], sorted(frames),
                 g.player()["health"]))
        check(speed is not None and speed > 24, "the dog is FAST: it closes at more than a "
              "guard's run a step (%s)" % ("%.1f" % speed if speed else "no sample"))
        check(frames and all(DOG_F0 <= f <= DOG_F0 + 10 for f in frames),
              "...drawn from the dog's frames alone (%s)" % sorted(frames))
        check(bit, "...and it BIT the player: health 255 -> %d" % g.player()["health"])
        check(DOG_F0 + 8 in frames, "...the leap's bite frame was drawn (%d)" % (DOG_F0 + 8))
        shot(m, a, "dog", wave=6)
        m.pause()
        g.poke_byte("px_health", 100)
        s0 = g.word("px_score")
        m.run()
        aim = aim_seen(m, g, 3)
        check(aim == 3, "the dog at the elbow is under the crosshair (px_aim %d)" % aim)
        am0 = g.byte("px_ammo")
        tap(m, "ControlLeft", 3, g)
        ticks(g, 6)
        d = g.actor(3)
        print("   one shot (ammo %d -> %d): the dog's state %d, hp %d, score %d -> %d"
              % (am0, g.byte("px_ammo"), d["state"], d["hp"], s0, g.word("px_score")))
        check(g.byte("px_ammo") == am0 - 1 and d["state"] in (DIE, DEAD),
              "ONE pistol round kills the dog (state %d, ammo %d -> %d)"
              % (d["state"], am0, g.byte("px_ammo")))
        check(g.word("px_score") == s0 + 200, "...for 200 points (%d -> %d)"
              % (s0, g.word("px_score")))
        ok, seen = watch(g, 30, "state", (DEAD,), actor=3)
        check(ok and DOG_F0 + 10 in [c[0] for c in g.candidates() if c[2] == 3],
              "...and it falls to its corpse frame (%s; candidates %s)" % (seen, g.candidates()))
        m.pause()
        for i, k in enumerate(keep):
            g.actor_poke(i, x=k["x"], y=k["y"], kind=k["kind"], state=k["state"], hp=k["hp"],
                         dir=k["dir"], ang=k["ang"], flags=k["flags"], frame=0, timer=0)
        m.run()

        # --- (r) THE TAB MAP (wave 6, 97.6): the seen cells from above --------
        # the window at scene A, the eye looking round once so the hall is
        # SEEN; Tab: the map is drawn once, the world stops under it (no
        # sim tick is spent), the player's cell carries the marker and the
        # cell ahead of it the floor's tone; Tab again takes it down and a
        # frame re-lays the world. Then THE FOLD: px_gen poked to 255, so
        # the next cast wraps the generation and the marks are cleared -
        # the cell ahead survives in px_seen's bits and the map shows it
        m.pause()
        g._mark()
        px, py, head = pxslib.scene_at("a")
        g.eye_poke(px, py, head)
        g.pcell_poke(px, py)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)

        def map_open():
            f0 = g.word("px_frames")
            m.type_text("\t")
            os88marty.until(m, lambda mm: g.byte("px_mapon") == 1 and g.byte("px_mapd") == 0
                            and g.word("px_frames") != f0, "the map", poll=0.1, limit=60.0)
            ticks(g, 3)

        def map_cells():
            sh = g.shadow()
            ox = g.byte("px_mox")
            ox = ox - 256 if ox > 127 else ox
            oy, x0 = g.byte("px_moy"), g.byte("px_x0")
            k, b = (py >> 8) - oy, (px >> 8) - ox
            at = lambda kk, bb: (sh[2 * kk * 80 + x0 + bb], sh[(2 * kk + 1) * 80 + x0 + bb])
            return at(k, b), at(k, b + 1), g.word("px_mmark"), g.word("px_inkf")

        map_open()
        me, ahead, mark, floor = map_cells()
        dt0 = g.word("px_dtick")
        ticks(g, 20)
        print("   the map: the player's cell %s (marker %04x), the cell ahead %s (floor %04x); "
              "px_dtick %d -> %d over 20 ticks" % (me, mark, ahead, floor, dt0, g.word("px_dtick")))
        check(g.byte("px_pause") == 1 and g.word("px_dtick") == dt0,
              "Tab: the MAP is up and the world stops under it (px_dtick still)")
        check(me == (mark & 255, mark >> 8), "...the player's cell carries the marker %s" % (me,))
        check(ahead == (floor & 255, floor >> 8), "...and the SEEN cell ahead the floor's tone %s"
              % (ahead,))
        f0 = g.word("px_frames")
        m.type_text("\t")
        os88marty.until(m, lambda mm: g.byte("px_mapon") == 0 and g.word("px_frames") != f0,
                        "the world again", poll=0.1, limit=60.0)
        check(g.byte("px_pause") == 0, "Tab again: the map is down, the world runs, a frame drawn")
        # (r2) GAME > PAUSE OVER THE MAP (review, wave 6): the menu's Pause
        # takes the map down as P does. The first cut toggled px_pause alone,
        # so the world ran (guards shooting) behind a frozen map whose bar
        # still read MAP - TAB TO PLAY
        map_open()
        ui = os88ui.UI(m, mouse=os88mouse.Mouse(marty=m), verbose=False)
        ui.menu_pick("Game", "Pause")
        ticks(g, 6)
        mo, pz, hm = g.byte("px_mapon"), g.byte("px_pause"), g.byte("px_hmsg")
        print("   Game > Pause over the map: px_mapon %d, px_pause %d, px_hmsg %d" % (mo, pz, hm))
        check(mo == 1 and pz == 1 or mo == 0,
              "Game > Pause over the map never runs the world behind it (px_mapon %d, px_pause %d)"
              % (mo, pz))
        check(mo == 0 and pz == 0 and hm != PXM_MAP,
              "...it takes the map down, as P does (px_mapon %d, px_pause %d, px_hmsg %d)"
              % (mo, pz, hm))
        os88marty.until(m, lambda mm: g.byte("px_mapfa") == 0, "the map's force taken",
                        poll=0.1, limit=60.0)
        m.pause()
        g._mark()
        g.poke_byte("px_gen", 255)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        c = (py >> 8) * 64 + (px >> 8) + 1
        bit = g.bytes_("px_seen", 512)[c >> 3] & (0x80 >> (c & 7))
        check(g.byte("px_gen") < 8 and bit and g.bytes_("px_spot", 4096)[c] in (0, g.byte("px_gen")),
              "THE FOLD: the generation wrapped (px_gen %d) and the cell ahead is in px_seen's bits"
              % g.byte("px_gen"))
        map_open()
        me, ahead, mark, floor = map_cells()
        check(ahead == (floor & 255, floor >> 8), "...so the map still shows it after the wrap %s"
              % (ahead,))
        inkt = g.bytes_("px_inkt", 64)
        tones = set([floor]) | set(inkt[4 * i] | inkt[4 * i + 1] << 8 for i in range(16))
        check(mark not in tones, "...and the marker %04x is none of this backend's map tones %s"
              % (mark, sorted("%04x" % t for t in tones)))
        shot(m, a, "map", wave=6)
        m.type_text("\t")
        os88marty.until(m, lambda mm: g.byte("px_mapon") == 0, "the map down", poll=0.1, limit=60.0)

        # (r3) A DOOR SEEN ONLY THROUGH ITS NEIGHBOURS IS ON THE MAP (review,
        # wave 6 r2). The review's premise - "a ray that strikes a shut slab
        # never marks the door" - is FALSE here, and the first cut of this
        # leg proved it: the slab stands at the cell's middle, so the ray
        # ENTERS the door cell and its transposed walker marks it (px_spotT
        # read 3 and 5 on the two doors below, and the unfixed map drew both
        # in their tone - w6r2/pxsact-negctl-oldcell.log). What IS a hole is
        # a door whose OWN marks are 0 while its corridor is seen - a secret
        # door in a side wall walked past: every wall around it drawn by the
        # neighbour rule and the door cell black, which gives it away (97.6).
        # So the leg makes exactly that state: the door faced, a frame cast,
        # then the door cell's three marks ZEROED (the sim frozen, so no
        # frame re-marks it before Tab) and a neighbour's asserted set
        def door_on_map(i, what):
            dd = g.door(i)
            dc = dd["cell"]
            # the eye TWO tiles out, so the cell between it and the slab is
            # one the walkers pass (the eye's own cell is not marked)
            cx_, cy_ = (dc & 63) * 256 + 128, (dc >> 6) * 256 + 128
            mp = g.bytes_("px_map", 4096)
            if dd["flags"] & 4:                     # DOOR_EW: the passage runs N-S
                sides = ((0, -1, 1024), (0, 1, 3072))
            else:
                sides = ((-1, 0, 0), (1, 0, 2048))
            for sx, sy, eh in sides:
                c1 = ((cy_ >> 8) + sy) * 64 + (cx_ >> 8) + sx
                c2 = ((cy_ >> 8) + 2 * sy) * 64 + (cx_ >> 8) + 2 * sx
                if not (mp[c1] & 3) and not (mp[c2] & 3):
                    break
            ex, ey = cx_ + 512 * sx, cy_ + 512 * sy
            g.sim(False)
            m.pause()
            for nm, n in (("px_spot", 4096), ("px_spotT", 4096), ("px_seen", 512)):
                g.m.write(g.base + g.s[nm], bytes(n))
            g.door_poke(i, pos=0, state=SHUT, timer=0)
            g._mark()
            g.eye_poke(ex, ey, eh)
            g.pcell_poke(ex, ey)
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            m.pause()
            tx = (dc & 63) * 64 + (dc >> 6)
            spot, spotT = g.bytes_("px_spot", 4096), g.bytes_("px_spotT", 4096)
            own = (spot[dc], spotT[tx])
            ecell = c1                              # the cell between
            etx = (ecell & 63) * 64 + (ecell >> 6)
            nb = spot[ecell] or spotT[etx]
            g.m.write(g.base + g.s["px_spot"] + dc, b"\0")
            g.m.write(g.base + g.s["px_spotT"] + tx, b"\0")
            sb = g.bytes_("px_seen", 512)[dc >> 3] & ~(0x80 >> (dc & 7)) & 255
            g.m.write(g.base + g.s["px_seen"] + (dc >> 3), bytes([sb]))
            m.run()
            map_open()
            sh = g.shadow()
            ox = g.byte("px_mox")
            ox = ox - 256 if ox > 127 else ox
            oy, x0 = g.byte("px_moy"), g.byte("px_x0")
            k, b = (dc >> 6) - oy, (dc & 63) - ox
            got = (sh[2 * k * 80 + x0 + b], sh[(2 * k + 1) * 80 + x0 + b])
            mat = cell_byte(g, dc) >> 4
            it = g.bytes_("px_inkt", 64)
            want = (it[4 * mat], it[4 * mat + 1])
            st = g.door(i)["state"]
            print("   %s %d at (%d,%d), SHUT and faced: its own marks after the cast %s "
                  "(then zeroed), the cell between %s, material %d, map cell %s (lit tone %s), "
                  "state %d" % (what, i, dc & 63, dc >> 6, own, nb, mat, got, want, st))
            check(nb != 0, "...the corridor cell beside the %s is SEEN (%d)" % (what, nb))
            check(st == SHUT and got == want and got != (0, 0),
                  "Tab draws the %s whose own marks are 0 in its material's lit tone %s, "
                  "not unseen black (%s)" % (what, want, got))
            m.type_text("\t")
            os88marty.until(m, lambda mm: g.byte("px_mapon") == 0, "the map down", poll=0.1,
                            limit=60.0)
            g.sim(True)

        door_on_map(0, "door")
        if sec:
            door_on_map(sec[0], "SECRET door")

        # (r4) THE MAP ACROSS THE BRACKET'S EXIT (review, wave 6's close): Tab
        # in the bracket, then Esc. The map stays up in the window (px_mapon,
        # the world paused) and so must its bar line - the first cut's exit
        # kept only PXM_PAUSED and blanked MAP - TAB TO PLAY over a map that
        # was still showing
        g.enter_fsx()
        map_open()
        g.leave_fsx()
        ticks(g, 6)
        mo, pz, hm = g.byte("px_mapon"), g.byte("px_pause"), g.byte("px_hmsg")
        print("   Tab in the bracket, then Esc: px_mapon %d, px_pause %d, px_hmsg %d"
              % (mo, pz, hm))
        check(mo == 1 and pz == 1 and hm == PXM_MAP,
              "(r4) the map survives the bracket's exit WITH its bar line (px_mapon %d, "
              "px_pause %d, px_hmsg %d, want %d)" % (mo, pz, hm, PXM_MAP))
        m.type_text("\t")
        os88marty.until(m, lambda mm: g.byte("px_mapon") == 0, "the map down", poll=0.1,
                        limit=60.0)

        # --- (e) the frames, reported: melee (the sim RUNNING), seven chasers -----
        g.enter_fsx()
        g.pin(rung="tex", lowres=True, size=64)
        px, py, head = g.scene("a")
        g.wait_frames(1)
        m.pause()
        g.actor_poke(0, x=px + 256, y=py, state=CHASE, hp=25, ang=2048, dir=2, flags=PXAF_ATTACK)
        g.actor_poke(1, x=px + 512, y=py + 64, state=CHASE, hp=25, ang=2048, dir=2,
                     flags=PXAF_ATTACK)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        nsc = 0                                     # (px_nsc is rewritten by
        for _ in range(30):                         # every gather: the largest
            nsc = max(nsc, g.byte("px_nsc"))        # of thirty reads)
        check(nsc >= 2, "two guards a tile and two tiles ahead are candidates (%d)" % nsc)
        times = g.frame_times(a.frames, mode="sim")
        cyc = pxslib.median([t[0] for t in times])
        ms = pxslib.ms(cyc)
        fps = 1000.0 / ms if ms else 0
        qp = (g.word("px_qp") - pxslib.layout()["Q"]) // pxslib.PXG_QSZ
        print("   TWO GUARDS AT MELEE, full repaint, the sim running, the default rung: %6.1f ms "
              "= %5.2f fps (%d posts queued, %d sprites) - reported" % (ms, fps, qp, nsc))
        shot(m, a, "melee")
        # ...and THE DOG AT MELEE (review, wave 6): the one new thing drawn in
        # the world had no frame price. One dog at the elbow (the second
        # guard out of the world), then two - the melee rule admits two
        # actors at spawn, and a dog closes at 40 (god on: they bite, the
        # health stays)
        for nd in (1, 2):
            m.pause()
            g.actor_poke(0, x=px + 256, y=py, kind=DOG, state=CHASE, hp=1, ang=2048, dir=2,
                         flags=PXAF_ATTACK, frame=0, timer=0)
            if nd == 2:
                g.actor_poke(1, x=px + 512, y=py + 64, kind=DOG, state=CHASE, hp=1, ang=2048,
                             dir=2, flags=PXAF_ATTACK, frame=0, timer=0)
            else:
                g.actor_poke(1, state=0)
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            times = g.frame_times(a.frames, mode="sim")
            msd = pxslib.ms(pxslib.median([t[0] for t in times]))
            print("   %s AT MELEE, full repaint, the sim running, the default rung: %6.1f ms "
                  "= %5.2f fps (against two guards %.1f) - reported"
                  % ("ONE DOG" if nd == 1 else "TWO DOGS", msd, 1000.0 / msd if msd else 0, ms))
            if nd == 2:
                shot(m, a, "melee-dogs", wave=6)
        # ...and (e2) A MELEE UNDER AUTO (review, wave 6 r2) - REPORTED: two
        # guards at the elbow, the sim running, Detail Auto at the bracket's
        # 8086 start (position 1, Textured Low res) and NOTHING forced after
        # the first frame - the frames are the Delta-filled ones a fight
        # draws - for 180 video frames (3 s), then where Auto stands. Eight
        # consecutive frames over PX_BUDGET step it down - on an 8086 straight
        # to Flat Low res (3), past Flat Full - and the step back up is
        # tests/pxsauto.py's leg (f): the 10 s hold-down outlasts this leg,
        # so a path here ends where it fell (SPEC.md 97.8, wave 6's close)
        m.pause()
        g.actor_poke(0, x=px + 256, y=py, kind=0, state=CHASE, hp=25, ang=2048, dir=2,
                     flags=PXAF_ATTACK, frame=0, timer=0)
        g.actor_poke(1, x=px + 512, y=py + 64, kind=0, state=CHASE, hp=25, ang=2048, dir=2,
                     flags=PXAF_ATTACK, frame=0, timer=0)
        g._mark()
        g.poke_byte("px_detail", pxslib.PXD["auto"])
        g.poke_byte("px_apos", 1)
        g.poke_byte("px_astart", 1)
        g.poke_byte("px_amiss", 0)
        g.poke_byte("px_ahit", 0)
        g.poke_byte("px_pend", 1)
        m.run()
        g.wait_frames(1)
        fa0, ap0 = g.word("px_frames"), 1           # the poked start: the first
        path = [ap0]                                # frame may already be a miss
        if g.byte("px_apos") != ap0:
            path.append(g.byte("px_apos"))
        for _ in range(12):
            m.advance(frames=15)
            m.run()
            ap = g.byte("px_apos")
            if ap != path[-1]:
                path.append(ap)
        fa1, ap1 = g.word("px_frames"), g.byte("px_apos")
        print("   A MELEE UNDER AUTO, 3 s on the 8086 bracket: Auto position %d -> %d (path %s; "
              "0 Tex Full, 1 Tex Low, 2 Flat Full, 3 Flat Low), %d frames drawn - reported"
              % (ap0, ap1, path, (fa1 - fa0) & 0xFFFF))
        g.pin(rung="tex", lowres=True, size=64)
        g.wait_frames(1)
        m.pause()
        g.actor_poke(0, x=22 * 256 + 128, y=3 * 256 + 128, kind=0, state=CHASE, hp=25,
                     flags=PXAF_ATTACK)
        g.actor_poke(1, x=31 * 256 + 128, y=13 * 256 + 128, kind=0, state=CHASE, hp=25,
                     flags=PXAF_ATTACK)
        for i in range(2, g.byte("px_nact")):
            g.actor_poke(i, state=CHASE, hp=25, flags=PXAF_ATTACK)
        m.run()
        g.scene("a")
        g.wait_frames(1)
        times = g.frame_times(a.frames, mode="sim")
        ms7 = pxslib.ms(pxslib.median([t[0] for t in times]))
        v7 = [pxslib.ms(t[0]) for t in times]       # BIMODAL (review r2): a
        # chaser step is ~10.3 ms and a ~135 ms frame carries two steps or
        # three by where the tick edges fall, so the frames sit near 132 and
        # near 143 and the MEDIAN picks a mode - wave 3's 133.2 and wave 4's
        # 143.3 are the same two modes (docs/reports/PXS-FRAME-2026-09-23.md)
        for i in range(g.byte("px_nact")):
            g.m.pause()
            g.actor_poke(i, state=STAND)
            g.m.run()
        g.scene("a")
        g.wait_frames(1)
        times = g.frame_times(a.frames, mode="sim")
        ms0 = pxslib.ms(pxslib.median([t[0] for t in times]))
        print("   SEVEN CHASERS, scene A finished (the sim running): %6.1f ms = %5.2f fps against "
              "%6.1f ms = %5.2f fps with every guard standing (+%.1f ms: seven LOS walks a tick)"
              % (ms7, 1000.0 / ms7, ms0, 1000.0 / ms0, ms7 - ms0))
        print("   ...the seven-chaser frames: mean %.1f ms, %.1f..%.1f (%s)"
              % (sum(v7) / len(v7), min(v7), max(v7), " ".join("%.1f" % x for x in v7)))

        # --- the done-when screendumps (--shots), the sim frozen ------------------
        if a.shots:
            g.sim(False)                            # (the sim off, a frame is owed
            m.pause()                               # by the poke alone: _mark before
            g._mark()                               # it, or the wait counts from a
            g.eye_poke(dx0, dy0, 0)                 # frame already drawn)
            g.pcell_poke(dx0, dy0)
            g.door_poke(0, pos=256, state=OPEN, timer=0)
            g.actor_poke(1, x=cx + 1024, y=cy + 512, state=STAND)
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            shot(m, a, "door-open")
            for ang in (0, 1024, 2048, 3072):
                m.pause()
                g._mark()
                g.actor_poke(1, x=dx0 + 384, y=dy0, state=STAND, ang=ang, dir=ang >> 10)
                g.force_all_poke()
                m.run()
                g.wait_frames(1)
                shot(m, a, "guard-ang%d" % ang)
            m.pause()
            g._mark()
            g.actor_poke(1, x=cx + 1024, y=cy + 512, state=STAND)
            g.eye_poke(sx - 768, sy - 96, 0)        # three tiles west of the ammo
            g.pcell_poke(sx - 768, sy - 96)         # and off its line, so it is
                                                    # right of the pistol (which
                                                    # has no depth test; review r1)
            g.m.write(g.base + g.s["px_stat"] + 4 * st0 + 2, bytes([0]))   # put back
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            shot(m, a, "pickup")
            g.sim(True)

        # --- (k) the DIE wash and the restart --------------------------------------
        # (wave 4: the wash ends in READY - "FLOOR 1", 27 ticks - and then
        # PLAY; the row reads PLAY with 100 health, which is after both)
        m.pause()
        g._mark()
        g.eye_poke(px, py, head)
        g.pcell_poke(px, py)
        g.actor_poke(1, x=px + 256, y=py, state=CHASE, hp=25, ang=2048, dir=2, flags=PXAF_ATTACK)
        g.poke_byte("px_health", 1)
        g.poke_byte("px_god", 0)
        lives0 = g.byte("px_lives")
        g.force_all_poke()
        m.run()
        dying = passed = False
        for _ in range(40):
            ticks(g, 3)
            if g.player()["state"] == 1:
                dying = True
                break
            if g.byte("px_lives") != lives0:        # THE WASH PASSED BETWEEN TWO
                passed = True                       # POLLS (review, wave 6 r2: a
                break                               # loaded host's poll took
                                                    # longer than the wash and
                                                    # READY together; a life is
                                                    # taken only on the way out
                                                    # of DYING, so it is proof)
        check(dying or passed, "a hit at health 1 puts the player in the DIE wash (state %d, "
              "health %d%s)" % (g.player()["state"], g.player()["health"],
                                ", seen by the life it took" if passed else ""))
        restarted = False
        for _ in range(30):
            ticks(g, 3)
            pl = g.player()
            if pl["state"] == 0 and pl["health"] == 100:
                restarted = True
                break
        pl = g.player()
        print("   after the wash: state %d, health %d, lives %d -> %d, floor %d"
              % (pl["state"], pl["health"], lives0, pl["lives"], pl["floor"]))
        check(restarted and pl["lives"] == lives0 - 1 and pl["floor"] == 0,
              "...and PX_FADE ticks later the floor restarted: health 100, a life fewer")
        g.god(True)

        # --- (j) the elevator switch: the next floor loads inside the bracket -------
        m.pause()
        g._mark()                                   # (everything stands after
        g.eye_poke(3 * 256 + 128, 22 * 256 + 128, 2048)   # the restart: the poke's
                                                    # frame is the only one)
        g.pcell_poke(3 * 256 + 128, 22 * 256 + 128)
        g.force_all_poke()
        m.run()
        g.wait_frames(1)
        f0 = g.word("px_frames")
        tap(m, "Space", 3, g)
        g.wait_state("done", limit=30.0)            # the LEVELDONE card (97.13)
        sw = 22 * 64 + 2
        b1 = cell_byte(g, sw)
        b2 = g.m.read(g.base + g.s["px_mapT"] + 2 * 64 + 22, 1)[0]
        check(b1 == 0xD1 and b2 == 0xD1, "the switch's cell reads material 13 (0xD1) in BOTH "
              "layouts after the throw (px_map %02x, px_mapT %02x)" % (b1, b2))
        check(g.player()["floor"] == 0, "...and the floor stands under the card (px_floor %d)"
              % g.player()["floor"])
        shot(m, a, "leveldone")
        # THE CARD'S HOLD IS COUNTED IN WORLD STEPS, not BIOS ticks
        # (px_timers, 97.13): the first cut waited 12 ticks for a 9-step
        # hold, and in one run of review r2 the steps lagged the ticks and
        # the Space landed inside the hold - eaten, as the hold means it to
        # be. So the wait is on px_cardhold itself - AND THE HOLD RE-ARMS at
        # 1 every step while OSAPI_KEY_DOWN reads Space or Enter held
        # (px_timers' .still), so a Space break code the guest never saw
        # held it for ever: wave 6's verification soak waited 180 guest
        # seconds here. pxslib.release_held reads the kernel's key map and
        # releases again, NAMING the key when it fires, and is asked again
        # every ten polls while the hold stands (wave 6's close)
        hk = [0, 0]

        def hold_clear(mm):
            if g.byte("px_cardhold") == 0:
                return True
            hk[0] += 1
            if hk[0] % 10 == 1:
                hk[1] += len(pxslib.release_held(m, ("Space", "Enter"), "the card's hold"))
            return False
        os88marty.until(m, hold_clear, "the card's hold", poll=0.1, limit=60.0)
        print("   the LEVELDONE card's hold cleared (%d polls; %d lost break code(s) "
              "released again)" % (hk[0], hk[1]))
        ticks(g, 2)
        m.key("Space")                              # the card moves on: E1M2
        loaded = False
        for _ in range(20):
            ticks(g, 3)
            if g.player()["floor"] == 1:
                loaded = True
                break
        check(loaded, "Space on the LEVELDONE card loaded the next floor (px_floor %d)"
              % g.player()["floor"])
        g.wait_state("play", limit=30.0)            # READY's own clock
        g.force()
        g.wait_frames(1)
        f1 = g.word("px_frames")
        check(f1 > f0, "...and frames are drawn on E1M2 in the same bracket (%d -> %d)" % (f0, f1))
        check(g.byte("px_nact") == 5 and g.byte("px_ndoors") == 17,
              "E1M2's tables are in force (%d actors, %d doors)" % (g.byte("px_nact"),
                                                                    g.byte("px_ndoors")))

        # --- (o) THE SILVER LOCK (E1M2's) ---------------------------------------
        sil = [i for i, d in enumerate(g.doors()) if d["lock"] == 2]
        check(len(sil) >= 1, "E1M2 has a silver door (%d)" % len(sil))
        if sil:
            d0 = g.door(sil[0])
            c = d0["cell"]
            if d0["flags"] & 4:
                cand = [((c & 63) * 256 + 128, (c >> 6) * 256 + 128 - 256, 1024),
                        ((c & 63) * 256 + 128, (c >> 6) * 256 + 128 + 256, 3072)]
            else:
                cand = [((c & 63) * 256 + 128 - 256, (c >> 6) * 256 + 128, 0),
                        ((c & 63) * 256 + 128 + 256, (c >> 6) * 256 + 128, 2048)]
            ex, ey, eh = [p for p in cand
                          if not g.m.read(g.base + g.s["px_map"] + ((p[1] >> 8) << 6 | (p[0] >> 8)), 1)[0] & 3][0]
            m.pause()
            g._mark()
            g.eye_poke(ex, ey, eh)
            g.pcell_poke(ex, ey)
            g.poke_byte("px_keys", 1)               # the GOLD key alone
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            # IN PLAY FIRST (review, wave 6 r2): E1M2 has just loaded, and a
            # Space pressed while READY stands is READY's own (it starts the
            # floor) - the gold check would pass on a Space that never
            # reached the door, and the silver one failed once in a soak
            # with the door never asked (state 0, pos 0)
            os88marty.until(m, lambda mm: g.byte("px_state") == pxslib.PXST["play"],
                            "PLAY on E1M2", poll=0.1, limit=60.0)
            ticks(g, 12)                            # past the card's hold
            tap(m, "Space", 3, g)
            ticks(g, 12)
            d = g.door(sil[0])
            check(d["state"] == SHUT and d["pos"] == 0, "Space at the silver door %d with the GOLD "
                  "key alone leaves it SHUT (state %d, pos %d)" % (sil[0], d["state"], d["pos"]))
            m.pause()
            g.poke_byte("px_keys", 2)
            m.run()
            ticks(g, 6)                             # the first tap's release seen
            tap(m, "Space", 3, g)
            ticks(g, 12)
            d = g.door(sil[0])
            check(d["state"] in (OPENING, OPEN) and d["pos"] > 0, "...and with the silver key it "
                  "opens (state %d, pos %d)" % (d["state"], d["pos"]))
        g.leave_fsx()
        check(g.byte("px_inbr") == 0, "Esc left the bracket")
    if FAIL:
        print("pxsact: FAIL (%d)" % len(FAIL))
        for f in FAIL:
            print("  -", f)
        return 1
    print("pxsact: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
