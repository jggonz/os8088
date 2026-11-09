#!/usr/bin/env python3
"""optime - one window/menu operation priced EXACTLY: entry to return of the
routine that does it, guest cycles, plus every primitive call inside it
(logged breakpoints, which do not stop the guest) and an exclusive sampling
profile by routine.

    python3 optime.py ROOT [--machine M] [--step N] [--json out] [--ops a,b]

The scene is tests/deskbench.py's.
"""
import argparse, bisect, collections, json, os, sys, time

_OUT = os.getcwd()             # where shots and --json go (not the checkout)
ROOT = os.path.abspath(sys.argv[1])
sys.argv.pop(1)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty, os88mouse, os88sym          # noqa
import deskbench as db                        # noqa
import dispcp                                 # noqa
from os88geom import MBAR_H, TITLE_H          # noqa

HZ = 4772727.0
S = os88sym.linear

PRIMS = ["gfx_fill", "gfx_hline", "gfx_vline", "gfx_frame", "gfx_pixel",
         "gfx_fill_gray", "gfx_fill_pat", "gfx_xor_rect", "gfx_xor_fill",
         "gfx_blit1", "gfx_blit4", "gfx_save", "gfx_restore", "gfx_scroll",
         "font_run", "font_run_x", "font_str", "font_str_x", "font_char",
         "font_char_x", "font_width_x",
         "wm_draw_win", "wm_draw_title", "wm_draw_title_b", "wm_clip_set",
         "wm_dmg_gray", "wm_draw_shadow", "wm_su_try", "wm_su_bank",
         "wm_su_take", "wm_paint_dmg", "wm_dmg_wins", "menu_draw_bar",
         "dock_draw", "dock_pass", "wm_dock_under", "wm_pkgcall",
         "gfx_lock", "gfx_unlock", "ico_draw", "dsk_zone_draw",
         "wm_chrome_clip", "wm_grow_paint", "wm_grow_erase", "wm_tbox",
         "thm_desk", "gfx_rect_setup", "wm_clip_walk", "wm_clip_rows",
         "menu_hover", "menu_item_xor", "mem_claim", "mem_free"]


def kern_table(m):
    off = os88sym.syms()
    eq = os88sym.equates()
    cold = eq.get("COLD_SEG")
    slide = m.cold_slide()
    out = []
    for n in off:
        if "." in n:
            continue
        try:
            seg = os88sym.segment_of(n)
        except Exception:
            continue
        if seg is None:
            continue
        out.append((seg * 16 + off[n] - (slide if seg == cold else 0), n))
    out.sort()
    return out


class Timer:
    def __init__(self, m, step):
        self.m, self.step = m, step
        self.tab = kern_table(m)
        self.addrs = [a for a, _ in self.tab]
        self.byname = {}
        for a, n in self.tab:
            self.byname.setdefault(n, a)
        self.prims = [(p, self.byname[p]) for p in PRIMS if p in self.byname]
        self.paddr = dict((a, p) for p, a in self.prims)
        self.results = []

    def name(self, lin):
        i = bisect.bisect_right(self.addrs, lin) - 1
        if i < 0:
            return "?"
        a, n = self.tab[i]
        return n if lin - a < 0x3000 else "?%05x" % lin

    def time(self, label, routine, trigger, nth=1, limit=20.0, sample=True, after=None):
        """Arm a stop at ROUTINE, fire TRIGGER, catch the nth entry, time it
        to its return."""
        m = self.m
        m.pause()
        ent = self.byname[routine]
        m.breakpoints([{"type": "exec", "addr": ent}])
        trigger()
        got = 0
        while got < nth:
            m.run()
            st = m.wait_stop(limit=limit)
            if st != "breakpoint":
                m.breakpoints([])
                m.run()
                print("### %s: %s never reached (%s)" % (label, routine, st))
                return None
            got += 1
        r = m.regs()
        ss, sp = r["ss"], r["sp"]
        stk = bytes(m.read(ss * 16 + sp, 4))
        ra_off = stk[0] | stk[1] << 8
        cs = r["cs"]
        # far call? (routine is entered by far call when cs != 0x60 caller)
        ra = cs * 16 + ra_off
        c0 = int(m.status()["cycles"])
        bps = [{"type": "exec", "addr": ra}]
        for p, a in self.prims:
            if a != ent:
                bps.append({"type": "exec", "addr": a, "log": True})
        m.breakpoints(bps)
        try:
            m.cmd(cmd="bplog")
        except Exception:
            pass
        if after:
            after()
        excl = collections.Counter()
        logs = []
        n = 0
        c1 = None
        while True:
            if sample:
                st = m.advance(cycles=self.step)
                s2 = m.status()
            else:
                m.run()
                m.wait_stop(limit=limit)
                s2 = m.status()
            if s2.get("state") == "breakpoint":
                rr = m.regs()
                if rr["cs"] * 16 + rr["ip"] == ra and rr["sp"] > sp:
                    c1 = int(s2["cycles"])
                    break
                if not sample:
                    continue
                m.run()     # a recursive hit of the return site; step past
                m.pause()
                continue
            rr = m.regs()
            excl[self.name(rr["cs"] * 16 + rr["ip"])] += 1
            n += 1
            if (int(s2["cycles"]) - c0) / HZ > limit:
                break
        r = m.cmd(cmd="bplog")
        hits = r.get("hits", [])
        m.breakpoints([])
        m.run()
        calls = collections.Counter(self.paddr.get(a, "?%05x" % a)
                                    for a, c in hits)
        cyc = (c1 or int(m.status()["cycles"])) - c0
        res = dict(row=label, routine=routine, cycles=cyc,
                   ms=1000.0 * cyc / HZ, calls=dict(calls),
                   excl=dict(excl), complete=c1 is not None,
                   hits=[(self.paddr.get(a, "?"), c - c0) for a, c in hits])
        self.results.append(res)
        print("\n### %s  [%s]  %d cycles = %.1f ms%s" %
              (label, routine, cyc, res["ms"],
               "" if c1 else " (INCOMPLETE)"))
        print("   calls: " + ", ".join("%s %d" % kv for kv in
                                       sorted(calls.items(),
                                              key=lambda x: -x[1])))
        tot = max(n, 1)
        for k, v in excl.most_common(18):
            print("   excl %5.1f%% %6.1f ms  %s" % (100.0 * v / tot,
                  1000.0 * v * self.step / HZ, k))
        sys.stdout.flush()
        return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--step", type=int, default=800)
    ap.add_argument("--json")
    ap.add_argument("--nosample", action="store_true")
    a = ap.parse_args()
    settle = os88marty.settle
    say = lambda s: print("  " + s)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          timeout=1200) as m:
        mo = os88mouse.Mouse(marty=m)
        sc = db.build(m, mo, settle, 0, say)
        db.check(m, sc, say)
        T = Timer(m, a.step)
        smp = not a.nosample
        rects = lambda: db.rects(m)

        def grab(slot):
            zs = rects()
            i = [j for j, (sl, _) in enumerate(zs) if sl == slot][0]
            return db.exposed_title(zs, i)

        def release():
            m.mouse(0, 0, l=False)

        import os88ui
        U = os88ui.UI(m, mouse=mo, verbose=False)

        SH = os.path.join(_OUT, "optime-shots",
                          (a.json or "x").replace(".json", ""))
        import shutil; shutil.rmtree(SH, ignore_errors=True); os.makedirs(SH, exist_ok=True)
        nshot = [0]

        def snap(label):
            m.pause()
            fw, fh, fb = m.fbuf()
            m.run()
            nshot[0] += 1
            fn = os.path.join(SH, "%02d-%s.png" % (nshot[0], label.replace(" ", "_").replace("(", "").replace(")", "")))
            os88marty.write_png_rgb(fn, fw, fh, fb)
            if os.environ.get("NOFULL"):
                return
            # incremental vs a forced FULL repaint (tests/dispcorner.py's
            # method): WF_SAVEU off so caches cannot answer, [cp_dirty]
            saved = []
            for w in dispcp.win_list(m, S):
                ad = S("wm_wins") + w * dispcp.WIN_SIZE + dispcp.W_FLAGS
                f = m.read(ad, 2)
                saved.append((ad, f))
                m.write(ad, bytes([f[0] & ~dispcp.WF_SAVEU & 0xFF, f[1]]))
            m.write(S("cp_dirty"), b"\x01")
            settle(m)
            for ad, f in saved:
                m.write(ad, f)
            m.pause()
            fw2, fh2, fb2 = m.fbuf()
            m.run()
            base = MBAR_H * fw * 3
            nd = 0
            box = [9999, 9999, -1, -1]
            for i in range(base, len(fb), 3):
                if fb[i:i+3] != fb2[i:i+3]:
                    nd += 1
                    px = (i // 3) % fw; py = (i // 3) // fw
                    box = [min(box[0], px), min(box[1], py), max(box[2], px), max(box[3], py)]
            print("    INCvsFULL %-34s %d px %s" % (label, nd, box if nd else ""))
            sys.stdout.flush()
            if nd:
                os88marty.write_png_rgb(fn.replace(".png", "-FULL.png"), fw2, fh2, fb2)

        def front(slot):
            U.raise_window(slot)
            os88marty.ui_done(m)
            settle(m)

        ONLY = os.environ.get("ONLY", "")
        # --- the scene's own z-order (CP, Disk, Note, Paint): move Paint --
        for label, dx, dy in (("scene: move Paint away", 48, 12),
                              ("scene: move Paint back", -48, -12)):
            if ONLY: break
            frm = grab(sc["paint"])
            db.drag_hold(m, mo, frm, (frm[0] + dx, frm[1] + dy))
            T.time(label, "wm_paint_dmg", release, sample=smp)
            m.run()
            settle(m)
            snap(label)
        # --- raises: the press runs wm_front -> wm_raise ------------------
        for label, slot in (("raise Note Pad", sc["note"]),
                            ("raise Disk window", sc["disk"]),
                            ("raise Control Panel", sc["cp"]),
                            ("raise Paint", sc["paint"])):
            if ONLY: break
            if grab(slot) is None:
                print("### %s: title buried, raised through the dock" % label)
                front(slot)
                continue
            mo.to(*grab(slot))
            settle(m)
            T.time(label, "wm_front_b", lambda: m.mouse(0, 0, l=True),
                   sample=smp)
            release()
            m.run()
            settle(m)
            snap(label)
        # --- now Paint is in front again. Move Paint ---------------------
        for label, slot, dx, dy in (("move Paint away", sc["paint"], 48, 12),
                                    ("move Paint back", sc["paint"], -48, -12),
                                    ("move Paint (front) by 8", sc["paint"], 8, 0),
                                    ("move Paint (front) back 8", sc["paint"], -8, 0)):
            if ONLY: break
            frm = grab(slot)
            db.drag_hold(m, mo, frm, (frm[0] + dx, frm[1] + dy))
            T.time(label, "wm_paint_dmg", release, sample=smp)
            m.run()
            settle(m)
            snap(label)
        # --- the bottom window raised then moved (CP) ---------------------
        if not ONLY: front(sc["cp"])
        for label, dx in (("move Control Panel (front)", 24),
                          ("move it back", -24)):
            if ONLY: break
            frm = grab(sc["cp"])
            db.drag_hold(m, mo, frm, (frm[0] + dx, frm[1]))
            T.time(label, "wm_paint_dmg", release, sample=smp)
            m.run()
            settle(m)
            snap(label)
        front(sc["paint"])
        # --- menu: drop and dismiss --------------------------------------
        mx, my = 8 + 20, MBAR_H // 2
        mo.to(mx, my)
        settle(m)
        if not ONLY: T.time("menu drop+hold+dismiss", "menu_drop",
               lambda: m.mouse(0, 0, l=True), sample=False, limit=8.0,
               after=release)
        m.run()
        settle(m)
        snap("menu")
        # --- fullscreen exit: wm_paint_all ---------------------------------
        m.type_text("f")
        settle(m)
        T.time("fullscreen exit (blk_relit)", "blk_relit",
               lambda: m.type_text("f"), sample=smp)
        settle(m)
        snap("fsexit")
    if a.json:
        json.dump(T.results, open(os.path.join(_OUT, a.json), "w"), indent=1)


main()
