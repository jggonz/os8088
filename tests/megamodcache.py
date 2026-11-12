#!/usr/bin/env python3
"""SPEC.md 2.8.3.3: kern_mega KEEPS a dropped module - the pool pair above
1MB that mod_hi copied it to - and the next use re-adopts it with no disk
read; the pool's compactor moves it with its selectors; the pool's shed
takes it back whole. tests/modcache.py is the same question on kern_big.

    make && make mega && python3 tests/megamodcache.py

The owner's words (2026-11-12): "Mega does need parity; we keep adding
things, and then 'oops we don't do the work on mega'". Before this, every
drop on kern_mega FREED the image (mod_free_row's mega_hifreef), so the
Sticky Apps menu, the Control Panel and every other module read its file
again on every use.

HOST: tests/modkeep.py's host leg against kern_mega's OWN module files
(build/megak/*.drv, OS88_BUILD=build/megak): every kept image's tail is
zeros in its file and every store its source makes is in that tail or a
written-before-read cell. It found DISKOPS.DRV's kern_mega KSEG helpers
emitted AFTER its scratch tail - a kept image's drop would have zeroed code.

One Bochs boot of build/mega.img (16MB, protected mode), the Task Manager
opened FIRST so its region is the pool block above where STIKMENU.DRV's
pair lands:

  A  the first Sticky Apps press LOADS STIKMENU.DRV (mod_need.sized) and
     mod_hi puts it above 1MB; closed, it is KEPT: MODR_SEG still the pair's
     code selector, its first slot on mod_gone (COLD_SEG), and its mx_tab
     block MX_FPURGE with MX_AUX = MEM_P_MOD + 2 x the id;
  B  the second press re-adopts it (mod_need.held, no .sized) with NO int
     13h at all - the kept scan (SPEC.md 110.12.2) rides the kept image -
     and lists the disk's three Sticky Apps;
  C  the Task Manager closes (a hole above the kept block) and the pool's
     UPWARD pack (mx_packupf, mx_alloc's own) is run: the block MOVES, the
     selector in MODR_SEG is the same, its base is the new one, and it is
     still kept and purgeable;
  D  the third press re-adopts it where it went: no int 13h, three listed;
  E  mx_shedf - mx_alloc's shed, the cheapest cache first - is run until
     the kept image goes: MODR_SEG 0, its block out of mx_tab, BOTH halves
     of its pair free in the LDT (mem_pg_forget's .mod arm), no fault;
  F  the next press LOADS it again (mod_need.sized), lists three, and the
     drop keeps it again;
  G  the Control Panel opened and closed twice: the second open re-adopts
     CTRL.DRV (MOD_CTRL .held, no .sized) with no int 13h.

Every count is EVERY int 13h the trace caught: a breakpoint on the IDT's
13h gate, which every disk call the kernel makes in protected mode enters.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured red on
2026-11-12: mod_drop's kern_mega arm `jc .free` made `jmp .free` (the
pre-change behaviour, every drop freed) - A's kept check, B's held/0 int 13h,
C, D and G FAIL; mega_modkeepf's `or byte [es:si+MX_FL], MX_FPURGE` out (kept
but never purgeable) - A's and C's tag checks FAIL and E never sheds it;
mem_pg_forget's `.mod` arm out (the shed zeroes the word and leaks the pair)
- E's "both halves free" FAILs; compress.inc's KSEG helpers back after
cmz_ovl - HOST FAILs (DISKOPS.DRV's tail is not zeros).

It reads build/ (wants=) and writes only its own scratch directory.
"""
import os
import struct
import subprocess
import sys
import tempfile
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.environ["OS88_MEGA"] = "1"       # os88marty.launch answers a MegaBochs,
sys.path.insert(0, HERE)            # os88sym reads kern_mega's own map
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty   # noqa: E402
import os88ui      # noqa: E402
import os88sym     # noqa: E402
import modstate    # noqa: E402
import stikmenu as SM  # noqa: E402

T = 180
fails = []
EQ = os88sym.equates()
MOD_STIKM, MOD_CTRL = EQ["MOD_STIKM"], EQ["MOD_CTRL"]


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)
    return cond


def host():
    print("HOST: kern_mega's own module files (tests/modkeep.py --legs host)")
    env = dict(os.environ, OS88_MEGA="1",
               OS88_BUILD=os.path.join(ROOT, "build", "megak"))
    r = subprocess.run([sys.executable, os.path.join(HERE, "modkeep.py"),
                        "--legs", "host"], env=env, capture_output=True,
                       text=True, timeout=600)
    bad = [ln.strip() for ln in r.stdout.splitlines() if "FAIL" in ln]
    check(r.returncode == 0 and not bad,
          "every kern_mega module's tail is zeros and every store is in it "
          "or a WBR cell%s" % (": " + "; ".join(bad) if bad else ""))


def nint(tr):
    return sum(1 for h in tr.hits if h.get("type") == "int"
               or h["name"].startswith("?"))


def ids(tr, name):
    return [h["regs"]["di"] & 0xFF for h in tr.hits
            if h["name"] == name and h.get("regs")]


def press(m, menu, label):
    os88marty.guest_sleep(m, 2.0)
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                            "mod_need.sized", "mod_need.held", regs=True,
                            cap=4000) as tr:
        menu.drop()
        n = menu.b("stm_n")
        menu.release_off()
        menu.gone()
    r = dict(n13=nint(tr), n=n,
             sized=ids(tr, "mod_need.sized").count(MOD_STIKM),
             held=ids(tr, "mod_need.held").count(MOD_STIKM))
    print("  %s: %d int 13h, mod_need.sized %d, .held %d, %d listed"
          % (label, r["n13"], r["sized"], r["held"], n), flush=True)
    return r


def row_seg(m, mid):
    return modstate.seg(m, mid)


def kept_ok(m, mid, what):
    s = row_seg(m, mid)
    st = modstate.state(m, mid)
    tag = modstate.kept_owner(m, mid)
    lin = m.lin(s, 0) if s else 0
    return check(s != 0 and st == "kept" and lin >= 0x100000 and
                 tag == modstate.kept_tag(mid),
                 "%s: KEPT - MODR_SEG %#06x based %06X above 1MB, slots on "
                 "mod_gone (%s), its pool block purgeable as %s (want %#06x)"
                 % (what, s, lin, st, hex(tag) if tag is not None else None,
                    modstate.kept_tag(mid)))


# --- the kernel's own pool routines, run through a guest stub ---------------
def _call(seg, off):
    """`call far SEG:OFF` with DS = the kernel's data and [sch_lock] up -
    mx_alloc's own context for both routines - then a NOP to stop on"""
    lock = os88sym.syms()["sch_lock"]
    return bytes([0x1E, 0x50, 0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x58,  # push ds..
                  0xFE, 0x06, lock & 0xFF, lock >> 8,              # inc lock
                  0x9A, off & 0xFF, off >> 8, seg & 0xFF, seg >> 8,
                  0xFE, 0x0E, lock & 0xFF, lock >> 8,              # dec lock
                  0x1F, 0x90])


PACK_AT, SHED_AT = 0x60, 0x80


def _blob(m):
    """ONE stub image for every call, written whole each time: Bochs's trace
    cache keeps code it has run (docs/BOCHS.md), so a second, DIFFERENT stub
    at the same address re-ran the first one - the shed ran the pack again
    (measured: nothing shed in 40 calls after a pack, the first call shed
    the image with no pack before it). The port stubs' own bytes
    (os88megabochs' _STUBS) stay where they are; each routine here has an
    address of its own past them"""
    sy = os88sym.syms()
    pad = bytes(PACK_AT - len(type(m)._STUBS))
    pack = _call(EQ["MEGA_SEL_CODE"], sy["mx_packupf"])
    return (type(m)._STUBS + pad + pack + bytes(SHED_AT - PACK_AT - len(pack))
            + _call(EQ["COLD_SEG"], sy["mx_shedf"]))


def far_stub(m, at, regs):
    """Run the blob's routine at AT on the stopped guest, REGS set first,
    through os88megabochs' port stub machinery (_stub): the guest's state is
    put back after"""
    for _ in range(40):                 # protected mode at the stop, never a
        if m.protected():               # reflection's real-mode window
            break
        os88marty.guest_sleep(m, 0.05)
    m._STUBS = _blob(m)
    try:
        m._stub(at, regs, [at + len(_call(0, 0)) - 1])
    finally:
        del m._STUBS


def packup(m):
    flr = struct.unpack("<H", bytes(m.read(m.sym("mega_mxflr"), 2)))[0]
    far_stub(m, PACK_AT, {"eax": flr})


def shed_one(m):
    far_stub(m, SHED_AT, {"ebx": 0xFF00})


def block_at(m, lin):
    """kern_mega's mx_tab: is there a live block based at linear LIN?"""
    xp = EQ.get("MEGA_XPOOL", 0x100000)
    raw = bytes(m.read(m.tail("mx_tab"), modstate.MX_NBLK * modstate.MX_BLKSZ))
    for k in range(modstate.MX_NBLK):
        off, kb = struct.unpack_from("<HH", raw, k * modstate.MX_BLKSZ)
        if kb and xp + off * 1024 == lin:
            return True
    return False


def caches(m):
    """kern_mega's purgeable pool blocks, as 'TAG@KB' (mx_tab)"""
    raw = bytes(m.read(m.tail("mx_tab"), modstate.MX_NBLK * modstate.MX_BLKSZ))
    out = []
    for k in range(modstate.MX_NBLK):
        off, kb, own, fl, sel, aux = struct.unpack_from(
            "<HHBBHH", raw, k * modstate.MX_BLKSZ)
        if kb and fl & modstate.MX_FPURGE:
            out.append("%04X@%d" % (aux, off))
    return " ".join(sorted(out)) or "none"


def ldt_present(m, sel):
    base, limit = m.ldt_base()
    if (sel & ~7) + 7 > limit:
        return False
    return bool(m.read(base + (sel & ~7), 8)[5] & 0x80)


def main():
    host()
    tmp = tempfile.mkdtemp(prefix="megamodcache-")
    try:
        with os88marty.launch(os88marty.system_disk(
                os.path.join(ROOT, "build", "os8088-360.img")),
                apps=os88marty.apps_disk(os.path.join(
                    ROOT, "build", "apps360.img")), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=300.0)
            os88marty.no_saver(m)
            menu = SM.Menu(m, ui)

            ui.menu_pick("Apple", "Task Manager")
            tm = ui.wait_window("Task Manager", limit=T)
            SM_desktop(ui)

            print("A: the first press loads STIKMENU.DRV above 1MB")
            a = press(m, menu, "first")
            check(a["sized"] == 1 and a["n13"] > 0,
                  "mod_need read the file (%d int 13h)" % a["n13"])
            kept_ok(m, MOD_STIKM, "closed")
            s0 = row_seg(m, MOD_STIKM)
            lin0 = m.lin(s0, 0) if s0 else 0

            print("B: the second press comes from RAM")
            b = press(m, menu, "second")
            check(b["sized"] == 0 and b["held"] == 1,
                  "re-adopted, no load (sized %d, held %d)"
                  % (b["sized"], b["held"]))
            check(b["n13"] == 0 and b["n"] == 3,
                  "NO int 13h (%d) and three listed (%d)"
                  % (b["n13"], b["n"]))
            check(row_seg(m, MOD_STIKM) == s0, "the same pair %#06x" % s0)

            print("C: the Task Manager goes; the pool's pack moves the image")
            ui.close(tm)
            SM_desktop(ui)
            os88marty.guest_sleep(m, 1.0)
            packup(m)
            s1 = row_seg(m, MOD_STIKM)
            lin1 = m.lin(s1, 0) if s1 else 0
            check(s1 == s0, "MODR_SEG is the same selector (%#06x)" % s1)
            check(lin1 != lin0, "...and the block MOVED: %06X -> %06X"
                  % (lin0, lin1))
            kept_ok(m, MOD_STIKM, "after the pack")

            print("D: the third press re-adopts it where it went")
            d = press(m, menu, "third")
            check(d["sized"] == 0 and d["held"] == 1 and d["n13"] == 0
                  and d["n"] == 3,
                  "re-adopted from the moved block: %d int 13h, %d listed "
                  "(sized %d, held %d)" % (d["n13"], d["n"], d["sized"],
                                           d["held"]))

            print("E: the pool's shed takes it back")
            code = s1 & ~7
            kept_ok(m, MOD_STIKM, "before the shed")
            print("  the pool's caches: %s" % caches(m), flush=True)
            n = 0
            while row_seg(m, MOD_STIKM) and n < 40:
                shed_one(m)         # the cheapest cache first: a TRIV raise
                n += 1              # cache would go before it (FBh < FCh)
                print("  shed %d: %s" % (n, caches(m)), flush=True)
            check(row_seg(m, MOD_STIKM) == 0,
                  "MODR_SEG 0 after %d shed(s)" % n)
            check(not block_at(m, lin1),
                  "its block at %06X is out of mx_tab" % lin1)
            gone = not ldt_present(m, code) and not ldt_present(m, code + 8)
            check(gone, "both halves of the pair %#06x/%#06x are free in "
                  "the LDT" % (code, code + 8))
            faults = [ln for ln in m.box.said()
                      if ln.startswith("kern_mega FAULT")] \
                if hasattr(m, "box") and hasattr(m.box, "said") else []
            check(not faults, "no fault (%r)" % faults[:2])

            print("F: the next press loads it again, and keeps it again")
            f = press(m, menu, "after the shed")
            check(f["sized"] == 1 and f["n"] == 3,
                  "loaded from the disk again, three listed (sized %d)"
                  % f["sized"])
            kept_ok(m, MOD_STIKM, "dropped again")

            print("G: the Control Panel twice")
            for k in (1, 2):
                os88marty.guest_sleep(m, 2.0)
                with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                                        "mod_need.sized", "mod_need.held",
                                        regs=True, cap=4000) as tr:
                    ui.menu_pick("Apple", "Control Panel")
                    w = ui.wait_window("Control Panel", limit=T)
                    ui.settle()
                n13 = nint(tr)
                sz = ids(tr, "mod_need.sized").count(MOD_CTRL)
                hd = ids(tr, "mod_need.held").count(MOD_CTRL)
                print("  open %d: %d int 13h, CTRL.DRV sized %d held %d"
                      % (k, n13, sz, hd), flush=True)
                ui.close(w)
                ui._wait(lambda: modstate.state(m, MOD_CTRL) != "live",
                         "CTRL.DRV dropped", T)
                if k == 1:
                    check(sz == 1, "the first open loaded CTRL.DRV")
                else:
                    check(sz == 0 and hd >= 1 and n13 == 0,
                          "the second open re-adopted it with NO int 13h "
                          "(%d, sized %d, held %d)" % (n13, sz, hd))
                kept_ok(m, MOD_CTRL, "the panel closed")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megamodcache: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


def SM_desktop(ui):
    import modcache
    modcache.desktop(ui)


if __name__ == "__main__":
    sys.exit(main())
