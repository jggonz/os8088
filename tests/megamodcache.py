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

  H  RAM IS RAM (the owner, 2026-11-12): an image the pool REFUSED stays in
     conventional memory, and in protected mode it is kept there too.
     CTRL.DRV's pool pair is shed (mx_shedf), every free KB of the pool
     covered with pinned blocks and every pool cache made unpurgeable, so
     mod_hi's copy is refused. With the Task Manager open, the Control Panel
     loads CTRL.DRV LOW (MODR_SEG an LDT selector based below 1MB); closed,
     it is KEPT: the slots on mod_gone, its mem_tab record re-tagged
     MEM_P_MOD + 2 x the id and MOVABLE (MC_RLOC != 0). The second open
     re-adopts it with NO int 13h, still low. Closed again, the Task Manager
     goes and a conventional compaction (posted at rank 0FFh) MOVES the
     record: the same selector, based at the new place, still kept; the
     third open re-adopts it there with no int 13h. Then mem_shed_one - the
     conventional shed - is run until it goes: MODR_SEG 0, its record free,
     BOTH halves of its pair free in the LDT, no fault. The pool is given
     back and the next open loads it again, above 1MB.

Every count is EVERY int 13h the trace caught: a breakpoint on the IDT's
13h gate, which every disk call the kernel makes in protected mode enters.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1), each measured red on
2026-11-12: mod_drop's kern_mega arm `jc .free` made `jmp .free` (the
pre-change behaviour, every drop freed) - A's kept check, B's held/0 int 13h,
C, D and G FAIL; mega_modkeepf's `or byte [es:si+MX_FL], MX_FPURGE` out (kept
but never purgeable) - A's and C's tag checks FAIL and E never sheds it;
mem_pg_forget's `.mod` arm out (the shed zeroes the word and leaks the pair)
- E's "both halves free" FAILs; compress.inc's KSEG helpers back after
cmz_ovl - HOST FAILs (DISKOPS.DRV's tail is not zeros). H, the same way:
mod_drop's kern_mega arm freeing a low image again (`call mem_ishidx / jc
.free` before the scratch is zeroed, the pre-2026-11-12 behaviour) - H's
kept checks, the second open's held/0 int 13h, the compaction and the
third open FAIL.

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
import os88geom as geom  # noqa: E402
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


PACK_AT, SHED_AT, CSHED_AT = 0x60, 0x80, 0xA0


def _ncall(seg, off, gadget):
    """`_call`'s frame round a NEAR routine in another segment: push a far
    return, then the offset of a `retf` in SEG (GADGET: mod_gone + 1, whose
    `stc / retf` is `.cold`'s), and far-JUMP in - the routine's `ret` lands
    on the `retf`, which comes home. (`push imm16` is a 186 opcode: Bochs's
    CPU is a 386, and this stub is the harness's, never the kernel's)"""
    lock = os88sym.syms()["sch_lock"]
    return bytes([0x1E, 0x50, 0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x58,  # push ds..
                  0xFE, 0x06, lock & 0xFF, lock >> 8,              # inc lock
                  0x0E,                                            # push cs
                  0xE8, 0x02, 0x00,                                # call +2
                  0xEB, 0x08,                                      # jmp home
                  0x68, gadget & 0xFF, gadget >> 8,                # push R
                  0xEA, off & 0xFF, off >> 8, seg & 0xFF, seg >> 8,
                  0xFE, 0x0E, lock & 0xFF, lock >> 8,              # dec lock
                  0x1F, 0x90])


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
    shed = _call(EQ["COLD_SEG"], sy["mx_shedf"])
    return (type(m)._STUBS + pad + pack + bytes(SHED_AT - PACK_AT - len(pack))
            + shed + bytes(CSHED_AT - SHED_AT - len(shed))
            + _ncall(EQ["COLD_SEG"], sy["mem_shed_one"], sy["mod_gone"] + 1))


def far_stub(m, at, regs):
    """Run the blob's routine at AT on the stopped guest, REGS set first,
    through os88megabochs' port stub machinery (_stub): the guest's state is
    put back after"""
    for _ in range(40):                 # protected mode at the stop, never a
        if m.protected():               # reflection's real-mode window
            break
        os88marty.guest_sleep(m, 0.05)
    m._STUBS = _blob(m)
    n = len(_ncall(0, 0, 0) if at == CSHED_AT else _call(0, 0))
    try:
        m._stub(at, regs, [at + n - 1])
    finally:
        del m._STUBS


def packup(m):
    flr = struct.unpack("<H", bytes(m.read(m.sym("mega_mxflr"), 2)))[0]
    far_stub(m, PACK_AT, {"eax": flr})


def shed_one(m):
    far_stub(m, SHED_AT, {"ebx": 0xFF00})


def cshed_one(m):
    """mem_shed_one for a claimant of the top rank: the conventional heap's
    cheapest purgeable record goes"""
    far_stub(m, CSHED_AT, {"ebx": 0xFF00})


# --- the conventional heap and the pool, read and covered -------------------
MC_SIZE = 10


def mem_rec(m, lin):
    """mem_tab's live record based at linear LIN, as (index, para, owner,
    rloc), or None"""
    n = EQ.get("MEM_MAX", 32)
    raw = bytes(m.read(m.sym("mem_tab"), n * MC_SIZE))
    for k in range(n):
        sg, pa, ow, _dma, rl = struct.unpack_from("<HHHHH", raw, k * MC_SIZE)
        if sg and sg * 16 == lin:
            return k, pa, ow, rl
    return None


def pool_rows(m):
    n, z = modstate.MX_NBLK, modstate.MX_BLKSZ
    raw = bytes(m.read(m.tail("mx_tab"), n * z))
    return [(k, raw[k * z:(k + 1) * z]) for k in range(n)]


def cover_pool(m):
    """megashed's cover: every free KB of the pool under a pinned block
    (owner FEh, MX_FPIN), and every purgeable pool block's MX_FPURGE taken
    off, so no mx_alloc - mod_hi's copy - can succeed or shed. Answers what
    uncover_pool undoes"""
    xkb = struct.unpack("<H", bytes(m.read(m.sym("xm_kb"), 2)))[0]
    rows = pool_rows(m)
    mxa = m.tail("mx_tab")
    used = sorted((struct.unpack_from("<H", e, 0)[0],
                   sum(struct.unpack_from("<HH", e, 0)))
                  for _, e in rows if struct.unpack_from("<H", e, 2)[0])
    gaps, at = [], 0
    for a, b in used:
        if a > at:
            gaps.append((at, a - at))
        at = max(at, b)
    if at < xkb:
        gaps.append((at, xkb - at))
    free = [k for k, e in rows if not struct.unpack_from("<H", e, 2)[0]]
    dummies, unpurged = [], []
    for (o, kb), k in zip(gaps, free):
        a = mxa + k * modstate.MX_BLKSZ
        m.write(a, struct.pack("<H", o))
        m.write(a + 4, struct.pack("<HHH", 0x08FE, 0, 0))
        m.write(a + 2, struct.pack("<H", kb))
        dummies.append((a, o, kb))
    for k, e in rows:
        if struct.unpack_from("<H", e, 2)[0] and e[5] & modstate.MX_FPURGE:
            a = mxa + k * modstate.MX_BLKSZ
            m.write(a + 5, bytes([e[5] & ~modstate.MX_FPURGE & 0xFF]))
            unpurged.append((a, bytes(e)))
    print("  the pool covered: %d free run(s), %d KB; %d cache(s) made "
          "unpurgeable" % (len(dummies), sum(d[2] for d in dummies),
                           len(unpurged)), flush=True)
    return dummies, unpurged


def uncover_pool(m, undo):
    """Take the cover off: each pinned block written that still reads as
    written, and each cache's MX_FPURGE where the block is still the same"""
    dummies, unpurged = undo
    for a, o, kb in dummies:
        if bytes(m.read(a, 6)) == struct.pack("<HHH", o, kb, 0x08FE):
            m.write(a + 2, struct.pack("<H", 0))
    for a, e in unpurged:
        now = bytes(m.read(a, modstate.MX_BLKSZ))
        if now[:5] == e[:5] and now[6:] == e[6:]:
            m.write(a + 5, bytes([now[5] | modstate.MX_FPURGE]))


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

            low_leg(m, ui)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megamodcache: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


def cp_open(m, ui, label):
    """Open the Control Panel under the int 13h trace: the window, and what
    the trace and CTRL.DRV's row say"""
    os88marty.guest_sleep(m, 2.0)
    with os88marty.bp_trace(m, {"type": "int", "addr": 0x13},
                            "mod_need.sized", "mod_need.held",
                            regs=True, cap=4000) as tr:
        ui.menu_pick("Apple", "Control Panel")
        w = ui.wait_window("Control Panel", limit=T)
        ui.settle()
    r = dict(n13=nint(tr), sized=ids(tr, "mod_need.sized").count(MOD_CTRL),
             held=ids(tr, "mod_need.held").count(MOD_CTRL),
             seg=row_seg(m, MOD_CTRL))
    r["lin"] = m.lin(r["seg"], 0) if r["seg"] else 0
    print("  %s: %d int 13h, CTRL.DRV sized %d held %d, MODR_SEG %#06x "
          "based %06X" % (label, r["n13"], r["sized"], r["held"], r["seg"],
                          r["lin"]), flush=True)
    return w, r


def cp_close(m, ui, w):
    ui.close(w)
    ui._wait(lambda: modstate.state(m, MOD_CTRL) != "live",
             "CTRL.DRV dropped", T)


def kept_low(m, what):
    """KEPT BELOW 1MB: MODR_SEG an LDT selector based in conventional
    memory, its slots on mod_gone, and the mem_tab record at that base
    re-tagged MEM_P_MOD + 2 x the id and declared MOVABLE"""
    s = row_seg(m, MOD_CTRL)
    st = modstate.state(m, MOD_CTRL)
    lin = m.lin(s, 0) if s else 0
    rec = mem_rec(m, lin) if s else None
    want = modstate.kept_tag(MOD_CTRL)
    ok = bool(s and s & 4 and st == "kept" and 0 < lin < 0x100000
              and rec is not None and rec[2] == want and rec[3] != 0)
    check(ok, "%s: KEPT LOW - MODR_SEG %#06x based %06X, slots on mod_gone "
          "(%s), its mem_tab record %s (want owner %#06x, MC_RLOC != 0)"
          % (what, s, lin, st,
             "owner %#06x MC_RLOC %#06x" % (rec[2], rec[3]) if rec else None,
             want))
    return s, lin, rec


def low_leg(m, ui):
    print("H: a module the pool refused is kept BELOW 1MB (RAM is RAM)")
    n = 0
    while row_seg(m, MOD_CTRL) and n < 40:
        shed_one(m)                 # CTRL.DRV's kept pool pair goes
        n += 1
    check(row_seg(m, MOD_CTRL) == 0,
          "CTRL.DRV's kept pool pair shed (%d shed(s))" % n)
    undo = cover_pool(m)
    try:
        ui.menu_pick("Apple", "Task Manager")
        tm = ui.wait_window("Task Manager", limit=T)
        SM_desktop(ui)
        w, r = cp_open(m, ui, "open 1, the pool full")
        check(r["sized"] == 1 and 0 < r["lin"] < 0x100000,
              "CTRL.DRV loaded and LEFT LOW: mod_hi's copy refused (sized "
              "%d, based %06X)" % (r["sized"], r["lin"]))
        cp_close(m, ui, w)
        s0, lin0, _ = kept_low(m, "the panel closed")

        w, r = cp_open(m, ui, "open 2")
        check(r["sized"] == 0 and r["held"] >= 1 and r["n13"] == 0,
              "the second open re-adopted the LOW image with NO int 13h "
              "(%d, sized %d, held %d)" % (r["n13"], r["sized"], r["held"]))
        check(r["seg"] == s0 and r["lin"] == lin0,
              "...the same selector %#06x at %06X" % (s0, lin0))
        cp_close(m, ui, w)
        kept_low(m, "closed again")

        print("  the Task Manager goes; a conventional compaction must MOVE "
              "the kept record", flush=True)
        ui.close(tm)
        SM_desktop(ui)
        dw = ui.open_drive("A")         # the window the pass wakes
        geom.post_compact(m, dw, 0xFF, ui._S)
        ui._wait(lambda: ui._word("mem_cpq") == 0, "the posted compaction "
                 "to run", T)
        ui.settle()
        s1, lin1, _ = kept_low(m, "after the compaction")
        check(s1 == s0, "MODR_SEG is the same selector (%#06x)" % s1)
        check(lin1 != lin0, "...and the record MOVED: %06X -> %06X"
              % (lin0, lin1))
        ui.close(dw)
        SM_desktop(ui)

        w, r = cp_open(m, ui, "open 3, after the move")
        check(r["sized"] == 0 and r["held"] >= 1 and r["n13"] == 0
              and r["seg"] == s1 and r["lin"] == lin1,
              "re-adopted where the compaction put it: %d int 13h, %#06x at "
              "%06X (sized %d, held %d)" % (r["n13"], r["seg"], r["lin"],
                                           r["sized"], r["held"]))
        cp_close(m, ui, w)
        s2, lin2, rec2 = kept_low(m, "closed a third time")

        print("  mem_shed_one takes it back", flush=True)
        code = s2 & ~7
        n = 0
        while row_seg(m, MOD_CTRL) and n < 40:
            cshed_one(m)
            n += 1
        check(row_seg(m, MOD_CTRL) == 0, "MODR_SEG 0 after %d shed(s)" % n)
        check(rec2 is not None and mem_rec(m, lin2) is None,
              "its mem_tab record at %06X is free" % lin2)
        check(code != 0 and not ldt_present(m, code)
              and not ldt_present(m, code + 8),
              "both halves of the pair %#06x/%#06x are free in the LDT"
              % (code, code + 8))
        faults = [ln for ln in m.box.said()
                  if ln.startswith("kern_mega FAULT")] \
            if hasattr(m, "box") and hasattr(m.box, "said") else []
        check(not faults, "no fault (%r)" % faults[:2])

        w, r = cp_open(m, ui, "open 4, the pool still full")
        check(r["sized"] == 1 and 0 < r["lin"] < 0x100000,
              "loaded from the disk again, low (sized %d, %06X)"
              % (r["sized"], r["lin"]))
        cp_close(m, ui, w)
        kept_low(m, "closed")
    finally:
        uncover_pool(m, undo)
    # the re-adopt of a LOW image runs mod_hi, which now has room: it copies
    # the image's MOD_H_IMG bytes up (mod_need's .held hands it BP, where a
    # load's read did) - a copy of nothing would fault the panel's first call
    w, r = cp_open(m, ui, "open 5, the pool given back")
    check(r["sized"] == 0 and r["held"] >= 1 and r["n13"] == 0
          and r["lin"] >= 0x100000,
          "re-adopted with NO int 13h and copied above 1MB (%d int 13h, "
          "sized %d, held %d, %06X)" % (r["n13"], r["sized"], r["held"],
                                        r["lin"]))
    cp_close(m, ui, w)
    kept_ok(m, MOD_CTRL, "the panel closed")
    w, r = cp_open(m, ui, "open 6")
    check(r["sized"] == 0 and r["n13"] == 0 and r["lin"] >= 0x100000,
          "...and re-adopted from the pool after that (%d int 13h)"
          % r["n13"])
    cp_close(m, ui, w)
    faults = [ln for ln in m.box.said()
              if ln.startswith("kern_mega FAULT")] \
        if hasattr(m, "box") and hasattr(m.box, "said") else []
    check(not faults, "no fault after the copy up (%r)" % faults[:2])


def SM_desktop(ui):
    import modcache
    modcache.desktop(ui)


if __name__ == "__main__":
    sys.exit(main())
