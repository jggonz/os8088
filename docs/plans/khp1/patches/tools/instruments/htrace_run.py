import sys, os, time, pickle
D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)
from htrace import *
import os88ui, os88geom as geom
import sucheck as su, subcheck as sc
machine, scen, out = sys.argv[1], sys.argv[2], sys.argv[3]
img = os.environ.get("IMG", "build/os8088-360.img"); apps = os.environ.get("APPS", "build/apps360.img")
os.chdir(ROOT)

def save(m, tab, tr, name):
    codes = {}
    for f in set(x[1] for x in tr):
        codes[f] = bytes(m.read(f, 6))
    pickle.dump({"tr": tr, "tab": tab.t, "codes": codes}, open(D + "/%s.pkl" % name, "wb"))
    print(profile(tr, tab, top=30))
    inc = inclusive(tr, tab)
    for k, v in sorted(inc.items(), key=lambda x: -sum(x[1]))[:40]:
        print("%-24s n=%4d sum=%8d mean=%7.0f min=%6d max=%6d" % (k, len(v), sum(v), sum(v)/len(v), min(v), max(v)))

with os88ui.boot(img, apps=apps, machine=machine, quick=True) as ui:
    m = ui.m; mo = ui.mo
    if scen == "menu":
        ui.open_drive("A"); os88marty.settle(m)
        tab = Table(m)
        title, x0, x1, items = ui.menus()[1]
        mo.to((x0 + x1)//2, geom.MBAR_H//2); os88marty.settle(m)
        arm_and_catch(m, "menu_drop", lambda: m.mouse(0, 0, l=True))
        tr = step_until_ret(m, stop=m.sym("menu_drop.poll"))
        save(m, tab, tr, out)
        m.run(); m.mouse(0, 0, l=False)
    elif scen == "lock":
        ui.open_drive("A"); os88marty.settle(m)
        tab = Table(m)
        res = []
        for sym in ("gfx_lock", "gfx_unlock"):
            for k in range(3):
                arm_and_catch(m, sym, lambda: m.key("ArrowRight"))
                tr = step_until_ret(m)
                res += tr
                save(m, tab, tr, out + "_" + sym + str(k))
                m.run(); os88marty.settle(m)
    elif scen == "notepad":
        w = ui.path("A:/APPS/NOTEPAD.O88"); os88marty.settle(m)
        mo.to(600, 150); os88marty.settle(m)
        for k in ("KeyA", "KeyB"):
            m.key(k); os88marty.settle(m)
        tab = Table(m)
        # trace one keystroke: from the first gfx_lock after the key until the
        # gfx_unlock that follows returns
        arm_and_catch(m, "gfx_lock", lambda: m.key("KeyC"))
        tr = []
        for part in range(6):
            t = step_until_ret(m)      # gfx_lock
            tr += t
            arm_and_catch(m, "gfx_unlock", lambda: None)
            # step from the lock's return to here is lost; so instead step the unlock
            t = step_until_ret(m)
            tr += t
            break
        save(m, tab, tr, out)
        m.run()
    elif scen == "notepad2":
        w = ui.path("A:/APPS/NOTEPAD.O88"); os88marty.settle(m)
        mo.to(600, 150); os88marty.settle(m)
        for k in ("KeyA", "KeyB"):
            m.key(k); os88marty.settle(m)
        tab = Table(m)
        arm_and_catch(m, "cursor_hide", lambda: m.key("KeyC"))
        r = m.regs(); ss, sp = r["ss"], r["sp"]
        raw = m.read(ss*16+sp, 64)
        ws = [raw[i] | raw[i+1] << 8 for i in range(0, 64, 2)]
        print("stack words:", " ".join("%04x" % w for w in ws))
        for w_ in ws:
            for seg in (0x60, os88sym.equates()["COLD_SEG"] if False else 0x60):
                print("  %04x -> %s" % (w_, tab.name(seg*16 + w_)))
        m.run()
    elif scen == "raise":
        mo.dblclick(*su.zone(m, 1)); su.idle(m)
        mo.dblclick(*su.zone(m, 0)); su.idle(m)
        w = [x for x in su.windows(m) if x.visible]
        z = [i for i in sc.zorder(m) if i in [x.i for x in w]]
        by = {x.i: x for x in w}
        back = by[z[0]]
        print("raising", back)
        mo.to(*su.tile(m, back)); os88marty.settle(m)
        tab = Table(m)
        arm_and_catch(m, "wm_front", lambda: m.mouse(0, 0, l=True))
        tr = step_until_ret(m, max_steps=2000000)
        save(m, tab, tr, out)
        m.run(); m.mouse(0, 0, l=False)
