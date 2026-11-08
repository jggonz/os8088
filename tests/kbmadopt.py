#!/usr/bin/env python3
"""The keyboard mouse's pointer half: ADOPTED on a machine no pointer has
spoken on, live out of its claim, and DROPPED when one first does (SPEC.md
9.6.7, 2.8.8; docs/plans/BOOT3-PLAN.md wave 5).

    python3 tests/kbmadopt.py [--break key|drop]

Registered as kbmadopt in soak. Every MartyPC machine here has a serial
mouse on COM1, but a mouse that is never MOVED never sends a packet - so
booting with it still is a mouseless machine as far as the kernel can tell:
at kmain_o's end [mou_ptr] is 0 and kbm_adopt claims the image. Asserted on
guest memory, never on pixels:

  1. ADOPTED: the row (mod_r_kbm) names a claim; mem_tab holds it as ONE
     512-byte granule owned MEM_K_MOD (0xFF08); the claim starts with the
     module header (magic 'O8', MOD_VER 5, the row's own id) and its first
     bytes are `.boot3m`'s as kernel.bin carries them; all three KBFP slots
     name it.
  2. LIVE: ArrowRight moves the pointer by KBM_MIN (3) - the step code
     running out of the claim - and a letter key is NOT taken: ui_task
     reaches `ui_task.key_done` for it (a breakpoint, counted).
  3. A MENU off the keypad: Space over the first menu's title drops it,
     ArrowDown steps menu_sel by ITEM (9.6.3), Space picks and it closes.
  4. DROPPED: the real mouse moves, [mou_ptr] goes to 1, and within the UI
     passes after it the row is 0, the claim is gone from mem_tab and every
     slot names mod_gone again.
  5. AFTER THE DROP a letter key still reaches ui_task's dispatch - an empty
     slot eating every key is BOOT3-PLAN R15, the one hazard of the design.

Red arms, each of which must turn the row red (WRITING-TESTS 1):
  --break key   flip ui_task's `jnc .events` after the slot call (the label
                `ui_task.kbmk`) to `jc`: the image's inverted carry read the
                old way. Leg 2's letter is then eaten (red), and so would
                every key be after the drop.
  --break drop  NOP mou_hotplug's drop (`mou_hotplug.kbmdrop`, a 5-byte far
                call): leg 4's row never goes to 0 (red).

What it cannot see: a worker preempted inside the image at the drop (R16,
answered by the task_yield before it) - no MartyPC script can place a
preemption that precisely, and that is said here rather than implied.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]
import os88marty as M                                       # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88layout                                           # noqa: E402

MEM_K_MOD = 0xFF08
MC_SIZE = geom.MC_SIZE
MEM_MAX = 32
KBM_MIN = 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--break", dest="brk", choices=("key", "drop"))
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    eq = os88sym.equates()
    kbm_id = eq["MOD_KBM"]
    kb = open(os.path.join(ROOT, "build", "kernel.bin"), "rb").read()
    # `.boot3m` in the FILE: its runtime segment B3M_SEG (where kz_all puts
    # it) through os88layout, which knows the blob in front of `.text` - and
    # which must agree with the section's own file offset, or one of the two
    # is lying about where the image is
    at = os88layout.seg_at(eq["B3M_SEG"])
    assert at == eq["B3M_START"], ("B3M_SEG and B3M_START disagree", at,
                                   eq["B3M_START"])
    img0 = kb[at:at + 32]

    with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                     apps=os.path.join(ROOT, "build", "apps360.img"),
                     machine=a.machine, quick=True) as ui:
        m = ui.m
        M.ui_passed(m)

        def w(name, off=0):
            return struct.unpack("<H", m.read(m.sym(name) + off, 2))[0]

        def claims():
            raw = m.read(m.sym("mem_tab"), MEM_MAX * MC_SIZE)
            out = []
            for k in range(MEM_MAX):
                seg, para, own = struct.unpack_from("<HHH", raw, k * MC_SIZE)
                if para:
                    out.append((seg, para, own))
            return out

        def slots():
            return [struct.unpack("<HH", m.read(m.sym("KBFP") + 4 * k, 4))
                    for k in range(3)]

        if a.brk:
            m.pause()
            if a.brk == "key":
                at = m.sym("ui_task.kbmk")
                op = m.read(at, 1)[0]
                assert op == 0x73, ("ui_task.kbmk is not a jnc", hex(op))
                m.write(at, b"\x72")
            else:
                at = m.sym("mou_hotplug.kbmdrop")
                op = m.read(at, 1)[0]
                assert op == 0x9A, ("mou_hotplug.kbmdrop is not a far call",
                                    hex(op))
                m.write(at, b"\x90" * 5)
            m.run()

        # 1. adopted
        assert m.read(m.sym("mou_ptr"), 1) == b"\0", \
            "a pointer spoke during the boot - the row is void"
        seg = w("mod_r_kbm")
        assert seg, "no adoption: mod_r_kbm is 0 on a machine nothing pointed on"
        mine = [c for c in claims() if c[0] == seg]
        assert mine == [(seg, 32, MEM_K_MOD)], ("the claim", mine)
        hdr = m.read(seg * 16, 32)
        assert hdr[0:2] == b"O8" and hdr[2] == 5 and hdr[3] == kbm_id, \
            ("the claim's header", hdr[:4])
        assert hdr == img0, "the claim is not `.boot3m`'s bytes"
        for off, s in slots():
            assert s == seg, ("a KBFP slot does not name the claim", s, seg)

        # 2. live: the step, and a letter passed through
        m.pause()
        m.write(m.sym("mouse_x"), struct.pack("<HH", 100, 100))
        m.run()
        M.ui_passed(m)
        m.key("ArrowRight")
        M.until(m, lambda _: w("mouse_x") != 100, "the keypad to move the "
                "pointer out of the claim", guest=5)
        assert w("mouse_x") == 100 + KBM_MIN, ("one tap", w("mouse_x"))
        n = M.bp_count(m, "ui_task.key_done", lambda: m.key("KeyA"))
        assert n >= 1, "a letter was eaten with the pointer half adopted"

        # 3. a menu off the keypad
        cells = ui.menus()
        name, x0, x1, items = cells[0]
        m.pause()
        m.write(m.sym("mouse_x"), struct.pack("<HH", (x0 + x1) // 2,
                                              geom.MBAR_H // 2))
        m.run()
        m.key("Space")
        M.until(m, lambda _: ui._byte("menu_dropd"), "the %s menu to drop "
                "off the keyboard mouse" % name, guest=5)
        sel0 = ui._byte("menu_sel")
        m.key("ArrowDown")
        M.until(m, lambda _: ui._byte("menu_sel") != sel0, "ArrowDown to step "
                "the dropped menu by item", guest=5)
        m.key("Space")
        M.until(m, lambda _: not ui._byte("menu_dropd"), "Space to pick and "
                "close the menu", guest=10)
        M.ui_passed(m)

        # 4. the real mouse speaks: the drop
        # Bare packets, paced: the mouse ITSELF is the subject here, and until
        # the contest settles its packets move nothing (SPEC.md 9.5), so the
        # absolute driver's closed loop would have nothing to read back
        for _ in range(50):
            if m.read(m.sym("mou_ptr"), 1) == b"\1":
                break
            m.mouse(4, 0)
            M.pace(m, 0.1)
        assert m.read(m.sym("mou_ptr"), 1) == b"\1", \
            "the serial mouse never settled - the drop leg cannot run"
        M.until(m, lambda _: w("mod_r_kbm") == 0, "the drop at mou_hotplug's "
                ".have", guest=5)
        assert not [c for c in claims() if c[0] == seg], \
            "the row is 0 and the claim is still held"
        gone = (os88sym.syms()["mod_gone"], w("api_coldseg"))
        assert slots() == [gone] * 3, ("every slot back on mod_gone, where "
                                       "`.cold` runs", slots(), gone)

        # 5. after the drop a key still reaches ui_task's dispatch
        n = M.bp_count(m, "ui_task.key_done", lambda: m.key("KeyA"))
        assert n >= 1, "a key was eaten after the drop (BOOT3-PLAN R15)"
    print("kbmadopt: adopted as one granule, drove the pointer and a menu "
          "from it, and dropped it when the mouse spoke%s"
          % (" - WITH THE RED ARM %r APPLIED, WHICH SHOULD HAVE FAILED IT"
             % a.brk if a.brk else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
