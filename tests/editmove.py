#!/usr/bin/env python3
"""Compact the heap out from under a live app that is holding a big claim
(SPEC.md 66.5.7 / 66.5.8 / 66.5.9).

    make && make build/editmove360.img
    python3 tests/editmove.py --app notepad     # 66.5.7, two claims one proc
    python3 tests/editmove.py --app fractal     # 66.5.7, one word
    python3 tests/editmove.py --app artful      # 66.5.7, two claims one proc
    make build/mppmove360.img
    python3 tests/editmove.py --app modplug     # 66.5.8, 36 words
    make build/zmove360.img                     # needs `inform`
    python3 tests/editmove.py --app frotz       # 66.5.9, and a live PC

tests/paintmove.py proves the hardest relocation proc in the tree - 110 row
segments and a delta. These three prove the OTHER end of the range, and the
thing worth proving about them is not the arithmetic (one or two words each)
but that the claim MOVES AT ALL: every one is behind a worker, and a worker
that has not parked pins everything its instance owns.

So `claim moved` is the load-bearing check and this script says so rather than
passing: NO means the run measured nothing.

WHAT THE A/B FOUND (SPEC.md 66.5.7.2) is why --nolk is not an inverted
assertion. All three were written on 66.5.3's premise - a drawing worker sits
in the gfx lock exactly when a compaction wants it, so without
OSAPI_MEM_PARKSAFE the claims never move - and with HEAPPARKLK=0 they move
anyway. Their workers SLEEP, so they reach OSAPI_TASK_ALIVE inside INST_PARKW
on their own; Tracker's, which feeds a ring under the lock, does not. The
declarations are kept as a widening and the reasoning was corrected.
--expect-nomove (HEAPPARK=0, no park at all) is the build where they really
cannot move, and that one IS asserted.

The sequence is paintmove.py's, for its reason. Claims are first fit from the
BOTTOM, so an app launched into an empty arena sits on the floor with no hole
under it and compaction correctly leaves it alone. heapfrag goes first, the
app lands above it, heapfrag dies to open the floor, and heapfrag again forces
the compaction.

TWO CLAIMS, ONE PROC is the other thing under test here (66.5.7): Note Pad and
ArtfulType declare the same proc for a document and an undo arena, and the
kernel picks between them by the base in BX. A proc that fixed the wrong word
would leave one of the two stale, so both are checked by address AND by
content.
"""
import sys, os, time, hashlib, argparse, subprocess, tempfile
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp

MC_SIZE, MEM_MAX = 10, 32

# WHAT TO DOUBLE-CLICK, BY NAME. It was a row INDEX per app and a comment
# saying the listing is sorted (SPEC.md 19.4) - which is true and is exactly
# why an index rots: any file added to one of these disks renumbers every
# entry after it, and a stale index launches whatever sorted into that slot
# with no diagnostic anywhere. dispcp.open_named asks the guest.
PKG = {"artful": "ARTFUL.O88", "fractal": "FRACTAL.O88",
       "heapfrag": "HEAPFRAG.O88", "notepad": "NOTEPAD.O88"}
PKG_MPP = {"heapfrag": "HEAPFRAG.O88", "modplug": "MODPLUG.O88"}
# ...and for Frotz the app's file is THE STORY: it owns .Z5 (SPEC.md 54), so
# one double-click opens the app AND loads the file. Opening FROTZ.O88 itself
# gives a running app that holds no claim at all, which reads as "declared
# nothing movable" and is really "there is no story in it".
PKG_Z   = {"frotz": "ZOPS.Z5", "heapfrag": "HEAPFRAG.O88"}

# the Standard File dialog's geometry (SPEC.md 38, kernel/fdlg.inc)
FD_ROW0, FD_ROWH, FD_TEXTX = 22, 16, 28

APPS = {
    # title prefix, source, includes, and the base words the proc must fix.
    # "waits" is how long the app needs after its row is double-clicked.
    "notepad": dict(title="Note Pad", src="apps/notepad/notepad.asm",
                    incs=("apps/",), words=["np_dseg", "np_useg"], wait=8),
    "fractal": dict(title="Fractal", src="apps/fractal/fractal.asm",
                    incs=("apps/",), words=["fr_cseg"], wait=14),
    "artful":  dict(title="ArtfulType", src="apps/artful/artful.asm",
                    incs=("apps/", "apps/artful/"),
                    words=["at_dseg", "at_aseg"], wait=8),
    "modplug": dict(title="ModPlug", src="apps/modplug/modplug.asm",
                    incs=("apps/", "apps/modplug/"),
                    words=["mpp_modseg"], wait=8,
                    img="build/mppmove360.img", rows=PKG_MPP),
    # ...and the window is titled after the STORY, not the app: Frotz calls
    # OSAPI_WM_TITLE when one is loaded (SPEC.md 11.92), so matching "Frotz"
    # finds nothing on exactly the runs that worked
    "frotz":   dict(title="ZOPS", src="apps/frotz/frotz.asm",
                    incs=("apps/", "apps/frotz/"),
                    words=["zf_sseg", "zf_stkseg"], wait=30,
                    extra=["zf_pcseg", "zf_sdelta", "zi_undoseg",
                           "zf_err", "zf_dead", "zx_badop"],
                    img="build/zmove360.img", rows=PKG_Z),
}


def pkg_syms(src, incs, probes=()):
    """A package's symbols, by re-assembling it - paintmove.py's trick.

    These offsets are `equ os88_image_end + N` behind macros, so they are
    neither greppable nor stable against an edit, and a wrong one reads a
    plausible word out of the middle of another variable - which reports the
    kernel as broken.

    `probes` is for Frotz, whose bss is `%define zf_x (os88_image_end + N)`
    rather than a label - a %define is a MACRO and nasm's map cannot see one,
    so each wanted name is pinned into the map with an equ that expands it.
    """
    extra = "".join("__p_%s equ %s\n" % (n, n) for n in probes)
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n" + extra
                            + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                nm = f[2]
                out[nm[4:] if nm.startswith("__p_") else nm] = int(f[0], 16)
        return out


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m, S):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def mine(cl, seg):
    return sorted([c for c in cl if c[2] == seg], key=lambda c: -c[1])


def uncovered(m, S, win, prefer_title=False):
    """A pixel of `win` that no window ABOVE it covers, or None.

    "Covers" is a Z-ORDER question and not a geometry one - the Disk window
    overlaps everything it launches and spends most of a session BELOW them.
    """
    zn = m.read(S("wm_zn"), 1)[0]
    zord = list(m.read(S("wm_zord"), zn))
    if win.i not in zord:
        return None
    wins = {w.i: w for w in os88geom.windows(m, S) if w.visible}
    above = [wins[i] for i in zord[zord.index(win.i) + 1:] if i in wins]
    ys = ([win.y + 1] if prefer_title
          else range(win.y + os88geom.TITLE_H, win.y + win.h - 2))
    for y in ys:
        for x in range(win.x + 4, win.x + win.w - 18):
            if not any(o.covers(x, y) for o in above):
                return x, y
    return None


def pkg_seg(m, S, title):
    for w in os88geom.windows(m, S):
        if w.title.startswith(title):
            raw = m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2)
            return u16(raw), w
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True, choices=sorted(APPS))
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--expect-nomove", action="store_true",
                    help="the A/B: built with HEAPPARK=0, so no worker parks "
                         "at all and NOTHING these three own may move")
    ap.add_argument("--nolk", action="store_true",
                    help="symbols for a HEAPPARKLK=0 build. NOT an inverted "
                         "assertion: SPEC.md 66.5.7.2 measured these three "
                         "moving with the gfx-lock park removed, because "
                         "their workers sleep and reach ALIVE anyway")
    a = ap.parse_args()
    cfg = APPS[a.app]

    def S(name):
        d = []
        if a.nolk:
            d.append("NOPARKLK")
        if a.expect_nomove:
            d.append("NOPARK")
        return os88sym.linear(name, tuple(d))

    rows = cfg.get("rows", PKG)
    with os88marty.launch("build/os8088-360.img",
                          apps=cfg.get("img", "build/editmove360.img"),
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)

        # SHOVE IT RIGHT, once - paintmove.py's reason: a Disk window is 320
        # wide at x=103 and a package opens near x=60..150, so the browser
        # otherwise sits squarely on top of everything it launches and every
        # later click meant for an app lands in the listing.
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        disk = (wx, wy)
        print("disk window moved to (%d,%d)" % disk)

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- heapfrag first, so it owns the floor of the arena --------------
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, rows["heapfrag"])
        time.sleep(22)
        os88marty.settle(m)
        hf_seg, hf_win = pkg_seg(m, S, "Heap")
        print("heapfrag at %04x" % (hf_seg or 0))

        # --- then the app, which lands ABOVE it -----------------------------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=rows[a.app])
        time.sleep(cfg["wait"])
        os88marty.settle(m)
        seg, win = pkg_seg(m, S, cfg["title"])
        if seg is None:
            print("FAIL: %s never opened a window; on screen: %s"
                  % (cfg["title"], [(w.i, w.title, w.visible)
                                    for w in os88geom.windows(m, S)]))
            return 1

        # --- give it something to hold --------------------------------------
        cx0, cy0, cx1, cy1 = win.content
        if a.app == "notepad":
            mo.click(cx0 + 20, cy0 + 12)
            os88marty.settle(m)
            m.type_text("the quick brown fox jumps over the lazy dog")
            time.sleep(3)
            # ...and DELETE some of it, which is what claims the undo arena.
            # An insert records a count and no bytes; only a deletion has a
            # blob to store, so an insert-only session leaves [np_useg] at 0
            # and the two-claims-one-proc half of this test vacuous.
            for _ in range(12):
                m.key("Backspace")
            time.sleep(3)
            os88marty.settle(m)
        elif a.app == "fractal":
            time.sleep(25)          # let the cache fill with computed rows
            os88marty.settle(m)
        elif a.app == "frotz":
            # the story is already loaded: Frotz owns .Z5 (SPEC.md 54), so the
            # double-click that opened the app also loaded it. Give the VM a
            # few seconds to boot and run to its first prompt, so [zf_pcseg]
            # is a live PC INTO the claim rather than 0 - which is the word
            # 66.5.9 is actually about
            time.sleep(12)
            os88marty.settle(m)
        elif a.app == "modplug":
            # 'l' is ModPlug's Open (SPEC.md 56), and it has to be the DIALOG
            # rather than a double-click on the row: 56.13 leaves the .MOD
            # extension pointed at Tracker on purpose, so the association
            # would open the wrong player.
            mo.click(cx0 + 20, cy0 + 20)     # focus the player first
            os88marty.settle(m)
            m.key("KeyL")
            time.sleep(3)
            os88marty.settle(m)
            # MATCH ON THE TITLE, not the size: wm_fit clamps the template's
            # 170 rows to 155 on a 640x200 screen (SPEC.md 39.7), so a size
            # test reports "the dialog never opened" about one that did
            fd = [w for w in os88geom.windows(m, S)
                  if w.visible and w.title.startswith("Open")]
            if not fd:
                print("FAIL: the file dialog never opened")
                return 1
            fd = fd[-1]
            fx0, fy0, _, _ = fd.content
            # row 0 is BEVERLY.MOD - the listing is sorted by name and the
            # dialog opens on this instance's own folder (SPEC.md 38.10),
            # which is B:'s root because MEDIA does not exist on this image
            mo.dblclick(fx0 + FD_TEXTX + 20, fy0 + FD_ROW0 + FD_ROWH // 2)
            time.sleep(20)          # 116KB off a 360KB floppy
            os88marty.settle(m)
        else:
            # ArtfulType is the SPLASH windowed (SPEC.md 46); 'w' takes the
            # document into the fullscreen editor, and that is the transition
            # that both spawns the caret worker and claims the undo/redo/clip
            # arena. Windowed it has neither, so a windowed run would move the
            # document without ever testing OSAPI_MEM_PARKSAFE - passing for
            # the wrong reason, which is the failure this file exists to avoid
            m.key("KeyW")
            time.sleep(4)
            os88marty.settle(m)
            m.type_text("hello from the compactor")
            time.sleep(3)
            os88marty.settle(m)
            # ...and Esc back to the splash. The fullscreen surface covers the
            # dock, so heapfrag's tile - the only handle on a window nothing
            # else can reach - is unclickable while it is up. Neither claim is
            # freed by leaving, and the caret worker keeps running, so this
            # costs the test nothing it was measuring
            m.key("Escape")
            time.sleep(2)
            os88marty.settle(m)

        P = pkg_syms(cfg["src"], cfg["incs"],
                     cfg["words"] + cfg.get("extra", []))

        def pword(name):
            return u16(m.read(seg * 16 + P[name], 2))

        before = mine(claims(m, S), seg)
        print("%s at %04x holds %s"
              % (cfg["title"], seg,
                 ["%04x/%dKB%s" % (b, p // 64, "*" if r else "")
                  for b, p, _, r in before]))
        if not any(r for _, _, _, r in before):
            print("FAIL: %s declared no claim movable" % cfg["title"])
            return 1

        # bank each declared base word and the bytes at it
        sizes = {c[0]: c[1] for c in before}
        bank = {}
        for w in cfg["words"]:
            base = pword(w)
            if base == 0:
                print("  (%s is 0 - not claimed in this run)" % w)
                continue
            if base not in sizes:
                print("FAIL: [%s] %04x is not a claim %s holds"
                      % (w, base, cfg["title"]))
                return 1
            bank[w] = (base, sizes[base],
                       hashlib.md5(m.read(base * 16,
                                          sizes[base] * 16)).hexdigest())
            print("  [%s] = %04x  %dKB  md5 %s"
                  % (w, base, sizes[base] // 64, bank[w][2][:12]))
        if not bank:
            print("FAIL: no declared base word held a claim")
            return 1
        shot0 = m.vram("cga")

        # --- close heapfrag: the floor under the app opens up ---------------
        #
        # THROUGH THE DOCK, trackmove.py's route: all three of these windows
        # open at x=60..150 and heapfrag is under them, sometimes entirely, so
        # there is no pixel of it to click. A tile TOGGLES minimize (SPEC.md
        # 30), so two clicks hide it and bring it back FRONTMOST - the one way
        # to raise a wholly-covered window that needs no geometry at all, and
        # the dock is the one strip nothing can cover.
        tile = os88geom.tile_xy(m, hf_win, S)

        def hf_state():
            for w in os88geom.windows(m, S):
                if w.i == hf_win.i:
                    zn = m.read(S("wm_zn"), 1)[0]
                    z = list(m.read(S("wm_zord"), zn))
                    return w.visible, (z and z[-1] == w.i)
            return False, False

        # DRIVE THE TILE TO A STATE, do not count clicks. A tile click on a
        # window that is not frontmost FRONTS it; on one that is, it
        # minimizes - so "two clicks" lands somewhere different depending on
        # what the app opened last, and the first version of this closed
        # nothing while reporting success.
        for _ in range(5):
            vis, top = hf_state()
            if vis and top:
                break
            mo.click(*tile)
            os88marty.settle(m)
        vis, top = hf_state()
        if not (vis and top):
            print("FAIL: could not bring heapfrag to the front")
            return 1
        mo.click(hf_win.x + 8, hf_win.y + 9)          # now its close box
        os88marty.settle(m)

        # THE CLAIM MAP IS THE PROOF, not the window. A window that closed
        # having freed nothing leaves the arena exactly as packed as it was,
        # and then every later check passes vacuously - which is what the
        # first version of this script did, reporting "still, no hole beneath
        # it" about a heap that had never opened one.
        if any(c[2] == hf_seg for c in claims(m, S)):
            print("FAIL: heapfrag closed but still holds claims - no hole")
            return 1
        under = [c for c in claims(m, S)
                 if c[0] < min(b for b, _, _ in bank.values())]
        print("heapfrag freed; %d claims left below the app" % len(under))

        # --- and run it again, whose big claim forces the compaction --------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk,
                          name=rows["heapfrag"])
        time.sleep(22)
        os88marty.settle(m)

        # heapfrag's OWN verdict plus the whole map, so a claim that did not
        # move can be told apart from a compaction that never ran
        hf2_seg, _ = pkg_seg(m, S, "Heap")
        if hf2_seg:
            img = u16(m.read(hf2_seg * 16 + 8, 2))
            hb = m.read(hf2_seg * 16 + img, 128)
            print("heapfrag#2: %d checks, %s  L1=%dK want=%dK moved=%d"
                  % (u16(hb, 0),
                     "".join("." if b == 0 else "X"
                             for b in hb[40:40 + u16(hb, 0)]),
                     u16(hb, 8), u16(hb, 10), u16(hb, 20)))
        base_p = u16(m.read(S("mem_base"), 2))
        top_p = u16(m.read(S("mem_top"), 2))
        fill = base_p
        for bs, pa, ow, rl in sorted(claims(m, S)):
            if bs > fill:
                print("        %5d KB HOLE" % ((bs - fill) // 64))
            print("   %04x %5d KB owner %04x%s"
                  % (bs, pa // 64, ow, "  MOVABLE" if rl else ""))
            fill = max(fill, bs + pa)
        if top_p > fill:
            print("        %5d KB HOLE (to the top)" % ((top_p - fill) // 64))

        after = mine(claims(m, S), seg)
        held = [c[0] for c in after]
        print("%s now holds %s"
              % (cfg["title"], ["%04x/%dKB" % (b, p // 64)
                                for b, p, _, r in after]))

        bad, n = 0, 0
        anymoved = False
        for w, (base, para, h0) in bank.items():
            n += 1
            new = pword(w)
            moved = new != base
            anymoved |= moved
            print("  %d [%s] %-9s %s"
                  % (n, w, "moved" if moved else "still",
                     "%04x -> %04x" % (base, new) if moved
                     else "%04x (no hole beneath it)" % base))
            if new not in held:
                print("      ...and it names no claim %s holds"
                      % cfg["title"])
                bad += 1
            elif moved:
                n += 1
                h1 = hashlib.md5(m.read(new * 16, para * 16)).hexdigest()
                ok = h1 == h0
                print("  %d [%s] contents  %s  (%s)"
                      % (n, w, "OK" if ok else "CORRUPT", h1[:12]))
                bad += not ok

        # FROTZ'S TWO EXTRA WORDS are the whole of SPEC.md 66.5.9: the story
        # base is easy and the PROGRAM COUNTER is not. [zf_pcseg] points into
        # the middle of the claim, so it must be SHIFTED by the delta and
        # never set to the new base - and [zf_sdelta] is what zi_yield reads
        # to repair an ES pushed before the move.
        if a.app == "frotz" and "zf_sseg" in bank:
            oldb = bank["zf_sseg"][0]
            newb = pword("zf_sseg")
            d = (newb - oldb) & 0xFFFF
            n += 1
            pc = pword("zf_pcseg")
            okpc = pc != 0 and newb <= pc < newb + (bank["zf_sseg"][1] >> 0)
            print("  %d [zf_pcseg] in story %s  (%04x, story %04x..%04x)"
                  % (n, "OK" if okpc else "OUTSIDE", pc,
                     newb, newb + bank["zf_sseg"][1]))
            bad += not okpc
            n += 1
            sd = pword("zf_sdelta")
            oksd = sd == d
            print("  %d [zf_sdelta] = delta %s  (%04x vs %04x)"
                  % (n, "OK" if oksd else "WRONG", sd, d))
            bad += not oksd

        n += 1
        print("  %d claim moved        %s"
              % (n, "OK" if anymoved
                 else "NO (expected)" if a.expect_nomove
                 else "NO  <-- the run proves nothing"))
        bad += anymoved if a.expect_nomove else not anymoved

        # --- raise it: the repaint is what reads through the moved base -----
        pt = uncovered(m, S, win, prefer_title=True)
        if pt is not None:
            mo.click(*pt)
        os88marty.settle(m)
        shot1 = m.vram("cga")

        def band(v):
            _, _, rows = v
            return b"".join(bytes(rows[y][cx0 // 8:cx1 // 8])
                            for y in range(cy0, min(cy1, len(rows))))

        if a.app == "frotz":
            # A REPAINT COMPARISON IS THE WRONG QUESTION for a running
            # interpreter - the Z-machine is still executing, so its window
            # legitimately differs from one moment to the next. What matters
            # is that it is still executing CORRECTLY through a base that
            # moved under it, which is what a stale PC would end.
            n += 1
            err = m.read(seg * 16 + P["zf_err"], 1)[0]
            dead = m.read(seg * 16 + P["zf_dead"], 1)[0]
            op = m.read(seg * 16 + P["zx_badop"], 1)[0]
            okz = err == 0 and dead == 0 and op == 0
            print("  %d VM did not fault   %s  (err %02x dead %02x badop %02x)"
                  % (n, "OK" if okz else "HALTED", err, dead, op))
            bad += not okz
            # ...and it must still RUN: type at the prompt and require the
            # window's pixels to change. A stale [zf_pcseg] executes whatever
            # lies at the same offset in some other segment, which is a fault
            # OR a silent wrong answer - this catches the second.
            n += 1
            pre = band(m.vram("cga"))
            m.type_text("look")
            m.key("Enter")
            time.sleep(6)
            os88marty.settle(m)
            ran = band(m.vram("cga")) != pre
            print("  %d VM still running   %s" % (n, "OK" if ran else "STUCK"))
            bad += not ran
        else:
            n += 1
            same = band(shot0) == band(shot1)
            print("  %d repaint identical   %s"
                  % (n, "OK" if same else "DIFFERENT"))
            bad += not same
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
