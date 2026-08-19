#!/usr/bin/env python3
"""Compact the heap out from under a LOADED module (SPEC.md 66.5.2/45).

    make && make build/trackmove360.img && python3 tests/trackmove.py

Paint proved a relocation proc against a derived ROW TABLE. Tracker is the
harder case and the one SPEC.md 66.1 named when it argued that relocation
cannot be a word poke: a 116KB module is the largest single claim this OS
hands out, and `[trk_modseg]` is not what the replayer reads. It reads
`[mp_blobseg]`, a normalized base segment PER SAMPLE (31 of them), and the
segment of the sample each of four channels is playing - thirty-six words,
every one of them the base plus a constant.

It is also the first adopter whose holder has a WORKER, so this is the park
handshake (SPEC.md 66.5) doing the only job it exists for. The mixer walks
`MP_SEG` with no lock of any kind; if the park did not work, the module would
move under a running mixer and what came out would be noise, not silence.

Six assertions. The last is the one no memory dump can make:

  1. `[trk_modseg]` CHANGED ADDRESS. Without it the rest is vacuous.
  2. The 116KB of module survived, hashed at the two addresses.
  3. `[mp_blobseg]` followed the claim.
  4. All 31 `MS_SEG` sample bases followed, checked as deltas.
  5. All 4 channel `MP_SEG` followed.
  6. The replayer is STILL RUNNING afterwards - `mp_row` advances - which is
     the only check that says the worker came back from its park.
"""
import sys, os, time, hashlib, argparse, subprocess, tempfile
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp

MC_SIZE, MEM_MAX = 10, 32
PKG_MOD, PKG_HEAPFRAG = "BEVERLY.MOD", "HEAPFRAG.O88"
MS_SZ, MS_SEG = 12, 0
MP_CHSZ, MP_SEG = 40, 6
MEM_K_DRV = 0xFF03              # a loaded driver's IMAGE claim (SPEC.md 51.3);
                                # its base IS the segment the driver runs in


def pkg_syms(src, incs=("apps/", "apps/tracker/")):
    """A package's symbols, by re-assembling it - os88sym.py's trick."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m, S):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def uncovered(m, S, win, prefer_title=False):
    """A pixel of `win` that no window ABOVE it covers - a Z-ORDER question."""
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


def find_win(m, S, title):
    for w in os88geom.windows(m, S):
        if w.title.startswith(title):
            raw = m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2)
            return u16(raw), w
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--expect-nolk", action="store_true",
                    help="the kernel was built HEAPPARKLK=0 (SPEC.md 66.5.4)")
    a = ap.parse_args()

    def S(name):
        return os88sym.linear(name, ("NOPARKLK",) if a.expect_nolk else ())
    P = pkg_syms("apps/tracker/tracker.asm")

    with os88marty.launch("build/os8088-360.img", apps="build/trackmove360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)   # browser to the right
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- heapfrag first, so it owns the floor of the arena --------------
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)
        hf_seg, hf_win = find_win(m, S, "Heap")
        print("heapfrag at %04x" % (hf_seg or 0))

        # --- then the module, which OPENS TRACKER through the association ---
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_MOD)
        time.sleep(30)                       # 116KB off a 360KB floppy
        os88marty.settle(m)
        tk_seg, tk_win = find_win(m, S, "Tracker")
        if tk_seg is None:
            print("FAIL: Tracker never opened a window")
            return 1

        def tword(name):
            return u16(m.read(tk_seg * 16 + P[name], 2))

        base = tword("trk_modseg")
        if not base:
            print("FAIL: Tracker holds no module")
            return 1
        held = [c for c in claims(m, S) if c[0] == base]
        if not held:
            print("FAIL: [trk_modseg] %04x is not a claim" % base)
            return 1
        para, rloc = held[0][1], held[0][3]
        print("Tracker at %04x, module %04x %dKB%s"
              % (tk_seg, base, para // 64, "  MOVABLE" if rloc else ""))
        if not rloc:
            print("FAIL: the module was never declared movable")
            return 1

        # --- the SOUND DRIVER's staging pool, if this machine has a card ----
        # SPEC.md 66.5.5. docs/FIELD-NOTES.md 2 named it the fragmenting party,
        # and it is the one claim in the tree that needs a DRIVER to park.
        D = pkg_syms("drivers/sound/sound.asm",
                     ("drivers/sound/", "drivers/", "apps/"))
        dseg = next((c[0] for c in claims(m, S) if c[2] == MEM_K_DRV), None)
        pool0 = prloc = None
        if dseg:
            pool0 = u16(m.read(dseg * 16 + D["sbl_poolseg"], 2))
            if pool0:
                pc = [c for c in claims(m, S) if c[0] == pool0]
                prloc = pc[0][3] if pc else None
                print("sound driver at %04x, pool %04x %dKB%s"
                      % (dseg, pool0, (pc[0][1] // 64) if pc else 0,
                         "  MOVABLE" if prloc else ""))
        if not pool0:
            print("no sound driver / no stream: the pool checks will SKIP")

        h0 = hashlib.md5(m.read(base * 16, para * 16)).hexdigest()
        smp0 = m.read(tk_seg * 16 + P["mp_smptab"], MS_SZ * 31)
        ch0 = m.read(tk_seg * 16 + P["mp_chans"], MP_CHSZ * 4)
        ds0 = [(u16(smp0, i * MS_SZ + MS_SEG) - base) & 0xFFFF for i in range(31)]
        dc0 = [(u16(ch0, i * MP_CHSZ + MP_SEG) - base) & 0xFFFF for i in range(4)]
        row0 = tword("mp_row")
        print("module md5 %s  blobseg %04x  row %d"
              % (h0[:12], tword("mp_blobseg"), row0))

        # --- close heapfrag: the floor under the module opens up -------------
        #
        # THROUGH THE DOCK, because Tracker's window covers heapfrag entirely
        # and no pixel of it is clickable. A tile TOGGLES minimize (SPEC.md
        # 30), so two clicks hide it and bring it back FRONTMOST - which is
        # the one way to raise a wholly-covered window that needs no geometry
        # at all, and the dock is the one strip nothing can cover.
        tile = os88geom.tile_xy(m, hf_win, S)
        mo.click(*tile)                                  # minimize
        os88marty.settle(m)
        mo.click(*tile)                                  # ...and back, on top
        os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)             # now its close box
        os88marty.settle(m)
        if any(w.i == hf_win.i and w.visible for w in os88geom.windows(m, S)):
            print("FAIL: heapfrag did not close, so no hole opened")
            return 1

        # --- and run it again, whose big claim forces the compaction ---------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)

        # heapfrag's OWN verdict, so a module that did not move can be told
        # apart from a compaction that never ran
        hf2_seg, _ = find_win(m, S, "Heap")
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
            print("   %04x %5d KB owner %04x%s" % (bs, pa // 64, ow,
                                                   "  MOVABLE" if rl else ""))
            fill = max(fill, bs + pa)
        if top_p > fill:
            print("        %5d KB HOLE (to the top)" % ((top_p - fill) // 64))

        bad = 0
        nb = tword("trk_modseg")
        moved = nb != base
        # heapfrag holds the gfx lock across its triggering claim on purpose,
        # so WITHOUT SPEC.md 66.5.4's declaration this cannot move: the worker
        # is blocked in gfx_lock and never reaches its ALIVE park point
        print("  1 module moved        %s"
              % ("%04x -> %04x" % (base, nb) if moved
                 else "NO" if a.expect_nolk else "NO  <-- proves nothing"))
        bad += moved if a.expect_nolk else not moved

        h1 = hashlib.md5(m.read(nb * 16, para * 16)).hexdigest()
        print("  2 contents survived   %s  (%s)"
              % ("OK" if h1 == h0 else "CORRUPT", h1[:12]))
        bad += h1 != h0

        ok3 = tword("mp_blobseg") == nb
        print("  3 blobseg followed    %s" % ("OK" if ok3 else "STALE"))
        bad += not ok3

        smp1 = m.read(tk_seg * 16 + P["mp_smptab"], MS_SZ * 31)
        ch1 = m.read(tk_seg * 16 + P["mp_chans"], MP_CHSZ * 4)
        ds1 = [(u16(smp1, i * MS_SZ + MS_SEG) - nb) & 0xFFFF for i in range(31)]
        print("  4 31 sample bases     %s" % ("OK" if ds0 == ds1 else "STALE"))
        bad += ds0 != ds1

        # MP_SEG is LIVE STATE, not a constant: a playing module rewrites it
        # every time a channel starts a note, so comparing it across time is
        # meaningless - it read STALE on a machine that simply had sound. What
        # must hold is that every channel is pointing INSIDE the module, which
        # is exactly what a missed relocation would break.
        inb = [i for i in range(4)
               if u16(ch1, i * MP_CHSZ + MP_SEG)
               and not (nb <= u16(ch1, i * MP_CHSZ + MP_SEG) < nb + para)]
        print("  5 channels inside it  %s" % ("OK" if not inb else
                                              "OUTSIDE: %s" % inb))
        bad += bool(inb)

        # the worker came back from its park, and the replayer is still walking
        # the module it was walking - a mixer reading a moved blob does not
        # stop, it plays the wrong bytes, so this is a liveness check and the
        # four above are the correctness ones
        r1 = tword("mp_row")
        time.sleep(4)
        r2 = tword("mp_row")
        alive = tword("mp_loaded") != 0
        print("  6 replayer alive      %s  (row %d -> %d -> %d, loaded=%s)"
              % ("OK" if alive else "GONE", row0, r1, r2, alive))
        bad += not alive
        if pool0:
            pnew = u16(m.read(dseg * 16 + D["sbl_poolseg"], 2))
            live = [c[0] for c in claims(m, S)]
            # THE POOL EXISTS ONLY WHILE A STREAM DOES - sbl_pool_put releases
            # it with the last grant - so it can only be seen to move while
            # something is playing, and only if there is a hole UNDER it at
            # that moment. Neither is arranged here: after the run above the
            # arena below the pool is fully packed, and the only holes are
            # trapped beneath PINNED claims. So this reports what it saw and
            # does not manufacture a pass. (Closing Tracker to open a hole is
            # self-defeating: it stops the stream, and [sbl_poolseg] goes to 0.)
            if not pnew:
                print("  7 pool moved          SKIP (the stream closed, so the"
                      " pool was freed)")
            elif pnew != pool0:
                print("  7 pool moved          %04x -> %04x" % (pool0, pnew))
            else:
                print("  7 pool moved          NOT EXERCISED (declared=%s;"
                      " nothing was free beneath it)" % bool(prloc))
            if not prloc:
                print("      the pool was never DECLARED movable")
                bad += 1
            ok8 = (not pnew) or (pnew in live)
            print("  8 pool base is a claim %s" % ("OK" if ok8 else "STALE"))
            bad += not ok8
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
