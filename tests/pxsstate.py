#!/usr/bin/env python3
"""PIXELSTEIN 3D's seven states, in BOTH worlds, and the score file read back
after a restart (SPEC.md 97.13; wave 4).

    python3 tests/pxsstate.py [--machine os8088_5150_cga_gla] [--shots DIR]

The states are walked by the KEYS a player presses and by the world's own
clocks - a guard's shots, the DIE wash, READY's and OVER's timers - with the
bss read through pxslib, never inferred from the glass; only the things a
player cannot do quickly are poked (a guard put a tile off, the health at
1, the score high enough to earn a row, the lives at 0 for the last death).

IN A WINDOW:
  (a) the game opens on the ATTRACT page, its card drawn into the band;
  (a2) Game > Pause there refuses in words on the window's line;
  (b) C and a floor's code ("CELL", E1M3's) start a game ON THAT FLOOR -
      READY with px_floor 2 - and Esc (windowed) abandons it to ATTRACT;
  (b0) a WRONG code ("ZZZZ") says BAD CODE (px_codep 0xFE) and stays on
      the attract page;
  (c) Space: READY ("FLOOR 1") then PLAY on its clock or a second Space;
  (c2) SOUND: a shot is one OSAPI_SND_TONE effect (px_sfxn +1, PXSFX_SHOT);
  (c3) the Game menu's Sound and Mouse items, picked BY NAME through the
      kernel's own menu tables: Sound off silences the next shot, Mouse on
      sets px_mouse, and PXSTEIN.CFG on the floppy says both (sound 0,
      mouse 1) - read by the independent FAT12 walker - and both picked
      back, the file following again;
  (c3m) with Mouse on at Size 48, a pointer resting on the BAND's middle
      (px_bx + Size x 4) turns nothing, and one 64 dots right of it turns;
  (c4) THE STICKY AUTO-PAUSE: a click on the Disk window takes the focus,
      and the game pauses itself (px_pause 1, PAUSED on the bar's label
      row); the game's window clicked back, it STAYS paused until P;
  (d) a guard's shot at health 1: DIE (the wash), then READY and PLAY with
      a life fewer;
  (e) the last life: DIE, then GAME OVER, then - the score earning a row -
      ENTER, where "ABC" and Enter are typed through W_ONKEY: the commit is
      the UI TASK's (SPEC.md 20.6 rule 7) and the table's first row reads
      ABC; and PXSTEIN.HS is on the floppy in SYSTEM\\APPDATA, read off the
      live image by tools/os88flush.py's own FAT12 walker (never the
      kernel's): magic 'PX8',1, the row there;
  (e2) LEVELDONE IN A WINDOW: a new game, E1M1's switch thrown, the ratios
      card drawn through the window's own present path, the next floor's
      code its last line, and Space to READY on E1M2; Esc to ATTRACT;
EVERY CARD (READY, LEVELDONE) is checked DRAWN, not merely entered: the
band of the shadow zeroed at the state's flip and lit a frame later
(card_seen) - and that is when its screendump is taken;
IN THE BRACKET:
  (f) F from the attract page: A FINISHED GAME STARTS A NEW ONE at the
      bracket's entry (97.13) - READY, then PLAY, px_inbr 1, score 0;
  (g) the elevator switch: LEVELDONE with its ratios and its time; Space
      loads the next floor - READY, then PLAY on E1M2;
  (h) the last life again: DIE, GAME OVER, Space to move it on, ENTER, and
      "XYZ" typed through the bracket's own int 16h loop (the bracket IS the
      UI task) - the second row reads XYZ under ABC;
  (h2) THE EPISODE'S END: a new game, the switch thrown with px_floor
      poked to the eighth floor (7), and Space on its card: GAME OVER with
      px_victory set and "YOU ESCAPED" on the card;
  (i) T: the TIMEDEMO - DEMO, then ATTRACT with its numbers: frames = 81
      (the script's 80 steps and the start's forced frame), ticks > 0, fps
      = frames x 182 / ticks in tenths, rounded (review, wave 6 r2); the
      run PINNED to Textured Low res Size 64 whatever was in force
      (px_demorg), what was in force back after it, and the bar a new game's (health 100) not the last game's;
  (j) Esc leaves the bracket;
  (j2) T on a machine with NO full-screen mode (px_mode poked to 0): no
      bracket, and no timedemo request left standing for the next F;
AFTER A RESTART:
  (k) the floppy flushed, a second machine booted on it, the game launched
      again: its table, read by px_hs_load at entry, is the one the first
      machine committed (ABC then XYZ at the top).
"""
import argparse
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))     # LAST, so it wins (pxslib)
import os88marty                                                # noqa: E402
import pxslib                                                   # noqa: E402
import os88flush                                                # noqa: E402
import os88mouse                                                # noqa: E402
import os88ui                                                   # noqa: E402
import dispcp                                                   # noqa: E402

FAIL = []
CHASE = 4
PXAF_ATTACK = 2
ST = pxslib.PXST


def check(ok, what):
    print("   %-70s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAIL.append(what)


def ticks(g, n, limit=120.0):
    t0 = g.ticks()
    os88marty.until(g.m, lambda mm: (g.ticks() - t0) & 0xFFFFFF >= n,
                    "%d ticks" % n, poll=0.1, limit=limit)


def shot(a, m, name):
    if not a.shots:
        return
    os.makedirs(a.shots, exist_ok=True)
    m.pause()
    w, h, pxl = m.fbuf(0)
    os88marty.write_png_rgb(os.path.join(a.shots, "wave4-%s-%s.png" % (a.machine, name)), w, h, pxl)
    m.run()


def band_lit(g):
    """Lit bytes in the band's 80 rows of the shadow (a card is text there)."""
    seg = g.word("px_shseg")
    b = g.m.read(seg << 4, 80 * 80)
    return sum(1 for v in b if v)


def card_seen(a, g, name, shotname=None, limit=60.0):
    """THE CARD IS DRAWN, not merely owed (review r2: the READY shot was taken
    the instant px_state flipped and was a black frame, and nothing checked a
    card at all past ATTRACT). Caught at the state's flip, paused: the band
    of the shadow is ZEROED while the card is still owed (px_cardd is
    decremented BEFORE the draw, so a non-zero byte means no row of it is
    down yet), so whatever is lit afterwards is this card's and not the last
    state's. Then a frame past the flip, paused again: still in the state,
    the band lit - and only then the screendump. Returns (owed, lit)."""
    m = g.m
    want = ST[name]
    os88marty.until(m, lambda mm: g.byte("px_state") == want,
                    "the %s state" % name.upper(), poll=0.005, limit=limit)
    m.pause()
    owed = g.byte("px_cardd")
    reowed = False
    seg = g.word("px_shseg")
    b0 = None
    if not owed:
        # DRAWN BEFORE THE PAUSE CAUGHT IT - or not owed YET (the flip and
        # the owe are a few instructions apart, and a pause between them
        # reads 0 with nothing drawn). MartyPC runs ~7x real and a card is
        # under a tick, so give the state three ticks to draw its own card,
        # then keep the band as it stands and owe the card again over a
        # zeroed band: the redraw must give the same bytes - a state that
        # drew no card of its own still shows the LAST one's
        m.run()
        t0 = g.ticks()
        os88marty.until(m, lambda mm: (g.ticks() - t0) & 0xFFFFFF >= 3, "3 ticks",
                        poll=0.005, limit=limit)
        m.pause()
        b0 = bytes(g.m.read(seg << 4, 80 * 80))
        g.poke_byte("px_cardd", 1)
        reowed = True
    f0 = g.word("px_frames")
    m.write(seg << 4, bytes(80 * 80))
    m.run()
    os88marty.until(m, lambda mm: (g.word("px_frames") - f0) & 0xFFFF >= 1
                    and g.byte("px_cardd") == 0, "the %s card drawn" % name.upper(),
                    poll=0.005, limit=limit)
    m.pause()
    st = g.gstate()
    lit = band_lit(g)
    b1 = bytes(g.m.read(seg << 4, 80 * 80))
    if shotname and a.shots:
        os.makedirs(a.shots, exist_ok=True)
        w, h, pxl = m.fbuf(0)
        os88marty.write_png_rgb(os.path.join(a.shots, "wave4-%s-%s.png" % (a.machine, shotname)),
                                w, h, pxl)
    m.run()
    check(st == want and 50 < lit < 80 * 80 // 2 and (not reowed or b0 == b1),
          "the %s card is DRAWN over a zeroed band (%d lit bytes; %s)"
          % (name.upper(), lit, "owed at the flip" if not reowed else
             "drawn before the pause, and the band then %s the card redrawn"
             % ("IS" if b0 == b1 else "is NOT")))
    return owed, lit


def hs_rows(g):
    sc = g.bytes_("px_hs", 12)
    nm = g.bytes_("px_hsn", 18)
    return [(struct.unpack_from("<H", sc, 2 * i)[0], nm[3 * i:3 * i + 3].decode("ascii", "replace"))
            for i in range(6)]


def die(g, lives, score=None, limit=240.0):
    """A guard a tile ahead in CHASE, the health at 1 and the player
    vulnerable: its shot starts the DIE wash."""
    m = g.m
    m.pause()
    px, py, head = pxslib.scene_at("a") if g.byte("px_floor") == 0 else g.eye()
    g.eye_poke(px, py, 0)
    g.pcell_poke(px, py)
    g.actor_poke(0, x=px + 256, y=py, state=CHASE, hp=25, ang=2048, dir=2, flags=PXAF_ATTACK)
    g.poke_byte("px_health", 1)
    g.poke_byte("px_lives", lives)
    if score is not None:
        g.poke_word("px_score", score)
    g.poke_byte("px_god", 0)
    g.poke_byte("px_simoff", 0)
    g.force_all_poke()
    m.run()
    g.wait_state("dying", limit=limit)


def cfg_bytes(m):
    """PXSTEIN.CFG off the live floppy (B:) by os88flush's own walker."""
    try:
        return os88flush.Flush(marty=m).volume(1).read("SYSTEM/APPDATA/PXSTEIN.CFG")
    except os88flush.FlushError as e:
        print("   (%s)" % e)
        return b""


def away_point(m, g):
    """A point to click that takes the focus from the game: another window's
    title bar where the game's window does not cover it, else the bare
    desktop (which hands the menu bar to Locator - px_focus_ck's .away)."""
    S = m.sym
    gr = dispcp.win_rect(m, S, g.win)
    rects = [dispcp.win_rect(m, S, i) for i in dispcp.win_list(m, S)]

    def inside(r, x, y):
        return r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]
    for i in dispcp.win_list(m, S):
        if i == g.win:
            continue
        x, y, w, _h = dispcp.win_rect(m, S, i)
        for xx in range(x + 8, x + w - 8, 8):
            if not inside(gr, xx, y + 5):
                return xx, y + 5
    sw = struct.unpack("<H", m.read(S("vid_w"), 2))[0]
    sh = struct.unpack("<H", m.read(S("vid_h"), 2))[0]
    for yy in range(24, sh - 40, 8):
        for xx in range(8, sw - 8, 8):
            if not any(inside(r, xx, yy) for r in rects):
                return xx, yy
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/games360.img")
    ap.add_argument("--shots", help="write the cards' screendumps here")
    a = ap.parse_args()
    os.chdir(ROOT)
    fd, saved = tempfile.mkstemp(prefix="pxsstate", suffix=".img")
    os.close(fd)
    try:
        with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
            g = pxslib.open_game(m, play=False)
            # --- (a) ATTRACT ----------------------------------------------------
            ticks(g, 6)
            check(g.gstate() == ST["attract"], "the game opens on the ATTRACT page (state %d)"
                  % g.gstate())
            lit = band_lit(g)
            check(lit > 50 and g.byte("px_cardd") == 0,
                  "...its card drawn into the band (%d lit bytes, px_cardd %d)"
                  % (lit, g.byte("px_cardd")))
            # (a2) Game > Pause outside PLAY says so on the window's line
            # (SPEC.md 47; review r2) rather than changing nothing
            mo = os88mouse.Mouse(marty=m)
            ui = os88ui.UI(m, mouse=mo, verbose=False)
            ui.menu_pick("Game", "Pause")
            ticks(g, 6)
            lb = bytes(g.bytes_("px_lbuf", 96)).split(b"\0")[0]
            check(b"Pause is for play" in lb and g.byte("px_pause") == 0
                  and g.gstate() == ST["attract"],
                  "(a2) Game > Pause on the ATTRACT page refuses in words (%r)" % lb)
            # --- (b) a floor's code ---------------------------------------------
            ticks(g, 12)                            # (every card's hold, 97.13)
            m.type_text("c")
            ticks(g, 4)
            check(g.byte("px_codep") == 0, "C starts taking a code (px_codep %d)" % g.byte("px_codep"))
            m.type_text("zzzz")
            ticks(g, 4)
            check(g.byte("px_codep") == 0xFE and g.gstate() == ST["attract"],
                  "(b0) a wrong code says BAD CODE and stays on ATTRACT (px_codep %02x)"
                  % g.byte("px_codep"))
            m.type_text("c")
            ticks(g, 4)
            m.type_text("cell")
            card_seen(a, g, "ready")
            check(g.byte("px_floor") == 2, "the code CELL starts a game on E1M3 (px_floor %d)"
                  % g.byte("px_floor"))
            m.key("Escape")
            g.wait_state("attract", limit=30.0)
            check(True, "windowed Esc abandons the game to the ATTRACT page")
            # --- (c) READY, PLAY ------------------------------------------------
            ticks(g, 12)
            m.key("Space")
            card_seen(a, g, "ready", "win-ready")
            check(g.byte("px_floor") == 0 and g.word("px_score") == 0,
                  "Space: a new game, READY on the first floor, score 0")
            g.wait_state("play", limit=60.0)
            check(True, "READY gives way to PLAY on its own clock")
            # --- (c2) a shot is a sound ------------------------------------------
            # (a tap is not always a shot under the harness - pxsact's own
            # loop taps up to ten times for the same reason: px_firek, set by
            # px_play_begin so a held key is not a press, clears only on a
            # step that sees Ctrl UP with the focus - so tap until a round is
            # spent, and assert on THAT tap)
            fired = None
            for _k in range(6):
                n0, a0 = g.word("px_sfxn"), g.byte("px_ammo")
                m.key("ControlLeft", down=True, up=False)
                ticks(g, 3)
                m.key("ControlLeft", down=False, up=True)
                ticks(g, 6)
                if g.byte("px_ammo") != a0:
                    fired = (n0, a0)
                    break
            if fired is None:
                check(False, "(c2) a tap of Ctrl spent a round (six taps)")
            else:
                n0, a0 = fired
                check((g.word("px_sfxn") - n0) & 0xFFFF == 1 and g.byte("px_sfxl") == 0
                      and g.byte("px_ammo") == a0 - 1,
                      "(c2) a shot (a round spent, %d -> %d) plays ONE effect through "
                      "OSAPI_SND_TONE (px_sfxn +%d, last %d)"
                      % (a0, g.byte("px_ammo"), (g.word("px_sfxn") - n0) & 0xFFFF,
                         g.byte("px_sfxl")))
            # --- (c3) the Sound and Mouse items, and PXSTEIN.CFG ------------------
            ui.menu_pick("Game", "Sound")
            ticks(g, 6)
            ui.menu_pick("Game", "Mouse")
            ticks(g, 6)
            mo.to(4, 4)
            check(g.byte("px_sound") == 0 and g.byte("px_mouse") == 1,
                  "(c3) Game > Sound turns the effects off, Game > Mouse the steering on "
                  "(sound %d, mouse %d)" % (g.byte("px_sound"), g.byte("px_mouse")))
            # (c3m) THE MOUSE'S MIDDLE IS THE BAND'S (review r2): at Size 48
            # the band is 384 dots from px_bx, so a pointer resting on its
            # middle turns nothing - the first cut took px_bx + 256 and spun
            # the view 16 a tick - and one 64 dots right of it does turn
            # ONE PRESS AT A TIME, each waited for by its own effect: windowed V
            # is also px_set_save's PXSTEIN.CFG write on the UI task, which can
            # outlast a fixed four ticks - and a second V typed before the first
            # landed steps past 0 and leaves the wait below on a Size it never
            # reaches (a soak failure, 0 of 5 alone)
            for _k in range(4):
                s0 = g.byte("px_sizeix")
                if s0 == 0:
                    break
                m.type_text("v")
                os88marty.until(m, lambda mm: g.byte("px_sizeix") != s0,
                                "V steps the Size row", poll=0.05, limit=30.0)
            os88marty.until(m, lambda mm: g.byte("px_size") == 48, "Size 48", poll=0.05,
                            limit=30.0)
            cx = g.word("px_bx") + g.byte("px_size") * 4
            cy = g.word("px_by") + 40
            mo.to(cx, cy)
            ticks(g, 4)
            h0 = g.word("px_head")
            ticks(g, 10)
            h1 = g.word("px_head")
            check(g.byte("px_mouse") == 1 and h1 == h0,
                  "(c3m) Size 48, the pointer on the band's middle (%d,%d): no turn "
                  "(heading %d -> %d)" % (cx, cy, h0, h1))
            mo.to(cx + 64, cy)
            ticks(g, 4)
            h2 = g.word("px_head")
            ticks(g, 6)
            h3 = g.word("px_head")
            check(h3 != h2, "...and 64 dots right of it turns the view (heading %d -> %d)"
                  % (h2, h3))
            mo.to(cx, cy)
            m.pause()                               # (Size 64 back as the
            g.poke_byte("px_sizeix", 2)             # timedemo leg restores it)
            g.poke_byte("px_pend", 1)
            m.run()
            os88marty.until(m, lambda mm: g.byte("px_size") == 64, "Size 64", poll=0.05,
                            limit=30.0)
            g.poke_byte("px_mouse", 0)              # (no steering by the parked
            n0 = g.word("px_sfxn")                  # pointer while we shoot)
            a0 = g.byte("px_ammo")
            for _k in range(6):
                m.key("ControlLeft", down=True, up=False)
                ticks(g, 3)
                m.key("ControlLeft", down=False, up=True)
                ticks(g, 6)
                if g.byte("px_ammo") != a0:
                    break
            check(g.byte("px_ammo") == a0 - 1 and g.word("px_sfxn") == n0,
                  "...and a shot with Sound off (a round spent, %d -> %d) plays nothing"
                  % (a0, g.byte("px_ammo")))
            cfg = cfg_bytes(m)
            col = 1 if g.byte("px_tier") >= 1 else 0    # Colour defaults on
            check(len(cfg) == 12 and cfg[:4] == b"PXC\x02" and cfg[9] == 0 and cfg[10] == 1
                  and cfg[11] == col,                     # from the 286 (97.14)
                  "PXSTEIN.CFG on the floppy says sound 0, mouse 1, colour %d (%r) - eight "
                  "bytes since wave 5 (SPEC.md 97.14)" % (col, cfg))
            g.poke_byte("px_mouse", 1)              # (as the file has it)
            ui.menu_pick("Game", "Sound")
            ticks(g, 6)
            ui.menu_pick("Game", "Mouse")
            ticks(g, 6)
            mo.to(4, 4)
            cfg = cfg_bytes(m)
            check(g.byte("px_sound") == 1 and g.byte("px_mouse") == 0 and len(cfg) == 12
                  and cfg[9] == 1 and cfg[10] == 0,
                  "both picked back: the game and the file follow (%r)" % cfg)
            # --- (c4) the sticky auto-pause ----------------------------------------
            pt = away_point(m, g)
            if pt is None:
                check(False, "(c4) a visible point off the game's window to click")
            else:
                mo.click(*pt)
                ticks(g, 10)
                check(g.byte("px_pause") == 1 and g.byte("px_hmsg") == 1,
                      "(c4) the focus lost: the game pauses itself, PAUSED on the bar "
                      "(px_pause %d, px_hmsg %d)" % (g.byte("px_pause"), g.byte("px_hmsg")))
                gx, gy = dispcp.win_rect(m, m.sym, g.win)[:2]
                mo.click(gx + 60, gy + 5)
                ticks(g, 10)
                mo.to(4, 4)
                check(g.byte("px_pause") == 1, "...and it STAYS paused when the window is "
                      "raised again (sticky)")
                m.type_text("p")
                ticks(g, 6)
                check(g.byte("px_pause") == 0, "...until P (px_pause %d)" % g.byte("px_pause"))
            # --- (d) a death with a life left ------------------------------------
            die(g, 3)
            card_seen(a, g, "ready")
            check(g.byte("px_lives") == 2 and g.byte("px_health") == 100,
                  "after the wash: READY, a life fewer (lives %d), health %d"
                  % (g.byte("px_lives"), g.byte("px_health")))
            g.wait_state("play", limit=60.0)
            # --- (e) the last life: OVER, ENTER, the file ------------------------
            die(g, 0, score=30000)
            g.wait_state("over", limit=60.0)
            check(True, "the last life's wash ends in GAME OVER")
            g.wait_state("enter", limit=60.0)
            check(True, "...and a score of 30000 earns a row: ENTER on OVER's clock")
            ticks(g, 12)
            m.type_text("abc")
            ticks(g, 4)
            check(g.bytes_("px_ini", 3) == b"ABC" and g.byte("px_inip") == 3,
                  "the initials typed through W_ONKEY (%r)" % g.bytes_("px_ini", 3))
            m.key("Enter")
            g.wait_state("attract", limit=30.0)
            rows = hs_rows(g)
            print("   the table: %s" % rows)
            check(rows[0] == (30000, "ABC"), "Enter commits: the first row is ABC 30000 (%r)"
                  % (rows[0],))
            f = os88flush.Flush(marty=m)
            try:
                hsf = f.volume(1).read("SYSTEM/APPDATA/PXSTEIN.HS")
            except os88flush.FlushError as e:
                hsf = b""
                print("   (%s)" % e)
            ok = (len(hsf) == 34 and hsf[:4] == b"PX8\x01"
                  and struct.unpack_from("<H", hsf, 4)[0] == 30000 and hsf[16:19] == b"ABC")
            check(ok, "PXSTEIN.HS is on the floppy in SYSTEM\\APPDATA, read by an "
                  "independent FAT12 walker: %d bytes, %r" % (len(hsf), hsf[:20]))
            # --- (e2) LEVELDONE IN A WINDOW (review r2): the ratios card through
            # px_gput_1 and the window's one OSAPI_GFX_BLIT1 union, a present
            # path the bracket's legs never take --------------------------------
            ticks(g, 12)
            m.key("Space")
            g.wait_state("play", limit=90.0)
            g.god(True)
            m.pause()
            g._mark()
            g.eye_poke(3 * 256 + 128, 22 * 256 + 128, 2048)
            g.pcell_poke(3 * 256 + 128, 22 * 256 + 128)
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            m.key("Space", down=True, up=False)
            ticks(g, 3)
            m.key("Space", down=False, up=True)
            card_seen(a, g, "done", "win-leveldone")
            code = bytes(g.bytes_("px_cbuf", 8 * 26)[7 * 26:7 * 26 + 9])
            check(g.byte("px_inbr") == 0 and code == b"CODE WARD",
                  "(e2) windowed: the switch gives the LEVELDONE card, its last line "
                  "the next floor's code (%r)" % code)
            ticks(g, 12)
            m.key("Space")
            card_seen(a, g, "ready")
            check(g.byte("px_floor") == 1, "...and Space on it: READY on E1M2 (px_floor %d)"
                  % g.byte("px_floor"))
            m.key("Escape")
            g.wait_state("attract", limit=30.0)
            ticks(g, 12)
            # --- (f) the bracket: a finished game starts a new one --------------
            m.type_text("f")
            card_seen(a, g, "ready", "ready")
            check(g.byte("px_inbr") == 1 and g.word("px_score") == 0 and g.byte("px_floor") == 0,
                  "F from ATTRACT: the bracket's entry starts a NEW game (READY, score 0)")
            g.wait_state("play", limit=60.0)
            # --- (g) LEVELDONE ---------------------------------------------------
            # (THE SIM RUNS: Use is a key the world's tick reads, px_keys_tick,
            # and px_simoff freezes it with the doors and the guards)
            g.god(True)
            m.pause()
            g._mark()
            g.eye_poke(3 * 256 + 128, 22 * 256 + 128, 2048)
            g.pcell_poke(3 * 256 + 128, 22 * 256 + 128)
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            m.key("Space", down=True, up=False)
            ticks(g, 3)
            m.key("Space", down=False, up=True)
            card_seen(a, g, "done", "leveldone", limit=30.0)
            print("   LEVELDONE: kills %d/%d, secrets %d/%d, loot %d/%d, %d ticks"
                  % (g.byte("px_ckill"), g.byte("px_nkill"), g.byte("px_csec"), g.byte("px_nsec"),
                     g.byte("px_ctreas"), g.byte("px_ntreas"), g.word("px_ltime")))
            check(g.byte("px_nkill") == 7 and g.word("px_ltime") > 0,
                  "the card's totals are E1M1's (7 guards) and its time is counted")
            code = bytes(g.bytes_("px_cbuf", 8 * 26)[7 * 26:7 * 26 + 9])
            check(code == b"CODE WARD", "...and its last line is the NEXT floor's code (%r)" % code)
            ticks(g, 12)
            check(g.gstate() == ST["done"], "the card still stands before a key moves it on "
                  "(state %d): no held key's repeat skipped it" % g.gstate())
            m.key("Space")
            os88marty.until(m, lambda mm: g.byte("px_floor") == 1, "E1M2", poll=0.05, limit=60.0)
            check(g.byte("px_floor") == 1, "Space on the card: E1M2 loads (px_floor %d)"
                  % g.byte("px_floor"))
            g.wait_state("play", limit=60.0)
            # --- (h) the last life in the bracket --------------------------------
            die(g, 0, score=25000)
            g.wait_state("over", limit=60.0)
            ticks(g, 12)
            shot(a, m, "over")
            m.key("Space")
            g.wait_state("enter", limit=30.0)
            ticks(g, 12)
            m.type_text("xyz")
            ticks(g, 6)
            shot(a, m, "enter")
            m.key("Enter")
            g.wait_state("attract", limit=30.0)
            rows = hs_rows(g)
            print("   the table: %s" % rows)
            check(rows[0] == (30000, "ABC") and rows[1] == (25000, "XYZ"),
                  "in the bracket's own key loop: XYZ 25000 under ABC 30000")
            check(g.byte("px_inbr") == 1, "...and the bracket stood throughout")
            # --- (h2) the eighth floor's elevator: the episode won ----------------
            ticks(g, 12)
            m.key("Space")
            g.wait_state("play", limit=90.0)
            g.god(True)
            m.pause()
            g._mark()
            g.eye_poke(3 * 256 + 128, 22 * 256 + 128, 2048)
            g.pcell_poke(3 * 256 + 128, 22 * 256 + 128)
            g.force_all_poke()
            m.run()
            g.wait_frames(1)
            m.key("Space", down=True, up=False)
            ticks(g, 3)
            m.key("Space", down=False, up=True)
            g.wait_state("done", limit=30.0)
            m.pause()
            g.poke_byte("px_floor", 7)              # E1M8's card, as if walked
            g.poke_word("px_score", 0)              # (no row: OVER goes to ATTRACT)
            m.run()
            ticks(g, 12)
            m.key("Space")
            g.wait_state("over", limit=30.0)
            ticks(g, 4)
            card = bytes(g.bytes_("px_cbuf", 8 * 26))
            check(g.byte("px_victory") == 1 and b"YOU ESCAPED" in card,
                  "(h2) the eighth floor's elevator ends the episode: OVER, px_victory %d, "
                  "'YOU ESCAPED' on the card" % g.byte("px_victory"))
            shot(a, m, "escaped")
            ticks(g, 12)
            m.key("Space")
            g.wait_state("attract", limit=30.0)
            # --- (i) the timedemo ------------------------------------------------
            ticks(g, 12)
            det0, size0 = g.byte("px_detail"), g.byte("px_sizeix")
            m.pause()
            g.poke_byte("px_health", 0)             # the last game's bar...
            g.poke_byte("px_detail", 0)             # ...and Auto in force
            g.poke_byte("px_sizeix", 0)             # ...at Size 48
            m.run()
            m.type_text("t")
            g.wait_state("demo", limit=30.0)
            ticks(g, 2)
            check(g.byte("px_health") == 100 and g.byte("px_lives") == 3,
                  "(i) the timedemo's bar is a new game's (health %d, lives %d)"
                  % (g.byte("px_health"), g.byte("px_lives")))
            g.wait_state("attract", limit=300.0)
            ticks(g, 4)
            shot(a, m, "timedemo")
            fr, tk, fps = g.word("px_demof"), g.word("px_demot"), g.word("px_demofps")
            rg = g.bytes_("px_demorg", 3)
            print("   TIMEDEMO: %d frames in %d ticks = %d.%d fps at rung %d lowres %d size %d"
                  % (fr, tk, fps // 10, fps % 10, rg[0], rg[1], rg[2]))
            check(g.byte("px_demodone") == 1 and fr == 81 and tk > 0
                  and fps == (fr * 182 + tk // 2) // tk,
                  "the timedemo ran its script (81 frames) and the card's numbers are "
                  "frames x 182 / ticks, rounded")
            check(tuple(rg) == (2, 1, 64), "...PINNED to Textured Low res Size 64 though Auto "
                  "and Size 48 were in force (%r)" % (tuple(rg),))
            check(g.byte("px_detail") == 0 and g.byte("px_sizeix") == 0,
                  "...and what was in force is back after it (detail %d, size %d)"
                  % (g.byte("px_detail"), g.byte("px_sizeix")))
            card = bytes(g.bytes_("px_cbuf", 8 * 26))
            check(b"Textured  Low res 64" in card, "...and the card names the rung and Size")
            m.pause()
            g.poke_byte("px_detail", det0)
            g.poke_byte("px_sizeix", size0)
            g.poke_byte("px_pend", 1)
            m.run()
            # --- (j) out of the bracket ------------------------------------------
            g.leave_fsx()
            check(g.byte("px_inbr") == 0, "Esc left the bracket")
            # --- (j2) T with no full-screen mode ------------------------------------
            mode0 = g.byte("px_mode")
            ticks(g, 12)
            m.pause()
            g.poke_byte("px_mode", 0)
            m.run()
            m.type_text("t")
            ticks(g, 8)
            check(g.byte("px_inbr") == 0 and g.byte("px_demoreq") == 0
                  and g.gstate() == ST["attract"],
                  "(j2) T with no mode: no bracket, and no timedemo request left for the "
                  "next F (px_demoreq %d)" % g.byte("px_demoreq"))
            m.pause()
            g.poke_byte("px_mode", mode0)
            m.run()
            f.save(1, saved)
        # --- (k) after a restart ----------------------------------------------
        with os88marty.launch(a.image, apps=saved, machine=a.machine) as m:
            g = pxslib.open_game(m, play=False)
            rows = hs_rows(g)
            print("   after a restart: %s" % rows[:3])
            check(rows[0] == (30000, "ABC") and rows[1] == (25000, "XYZ"),
                  "a second machine booted on that floppy reads the same table at entry")
    finally:
        try:
            os.unlink(saved)
        except OSError:
            pass
    if FAIL:
        print("pxsstate: FAIL (%d)" % len(FAIL))
        for x in FAIL:
            print("  -", x)
        return 1
    print("pxsstate: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
