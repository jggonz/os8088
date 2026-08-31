#!/usr/bin/env python3
"""WHO WRITES ui_rebootq? (docs/DUAL-DISPLAY-VGA.md 8(11))

    make && python3 tests/dispreboot.py

The field's report is a machine that RESETS on a click, three times in a row,
and every capture so far has been from the aftermath: wild execution at
0xDBE8, garbage handed to drv_call, a reboot already under way. The chain ends
at one byte - `ui_rebootq` (SPEC.md 20.10) - which ui_task spends at the top
of every pass, and which nothing but `ui_reboot_post` is supposed to write.
`ui_reboot_post` has been shown not to run. So the byte is being written by
something that is not aiming at it, and THIS is the instrument that catches
that write in the act rather than reasoning backwards from its consequence.

A MartyPC `mem` breakpoint is a bus-access breakpoint - read AND write, at the
flat address - so ui_task's own `cmp byte [ui_rebootq], 0` trips it every pass.
Two things make that affordable. The filter: THE FLAG'S OWN VALUE. ui_task
only ever reads it or writes a zero, so a stop that finds the byte still 0 is
ui_task looking at its own flag and is resumed, and a stop that finds it
non-zero is the write we are after, whoever did it. **CS:IP is NOT the filter
and must not be made one**: the breakpoint trips on the bus cycle and the CPU
runs on to the next instruction boundary, which on this machine means an
interrupt can be taken first - the first catch this instrument produced
reported sch_isr's entry with `8EC4 0060 F246` on the stack, an interrupt
frame whose return address is ui_task+5. A value test has no such window.

And the ARMING WINDOW: the breakpoint goes on immediately before the click and
comes off as soon as something is caught, so the idle poll is only paying for
the handful of passes between the packet arriving and the dispatch starting.

What it prints on a catch is the whole of what the next step needs: the
instruction that did it, the segment it addressed through, the stack around SP
(a wild transfer's return chain is usually still on it) and the bytes either
side of CS:IP.
"""
import os, sys, time
# ...off this file, not off an absolute path. It was /home/user/os8088, so the
# row ran in one container and nowhere else.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty, os88mouse, os88sym, dispcp, os88layout
S = os88sym.linear
SY = os88sym.syms()
KSEG = 0x0060
TITLE_H = 18


NOP = "--nop" in sys.argv


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def hexd(b):
    return " ".join("%02X" % c for c in b)


def runs_of(a, b):
    """Differing RUNS, not bytes - live state is contiguous."""
    out, start = [], None
    n = min(len(a), len(b))
    for i in range(n + 1):
        d = i < n and a[i] != b[i]
        if d and start is None:
            start = i
        if not d and start is not None:
            out.append((start, i - start))
            start = None
    return out


BSS0 = 0xC12E       # .text ends here; everything above is live state and a
                    # diff against the built image says nothing about it

# .cold: its segment, where it sits in kernel.bin, and how much of it there is
# - all three from os88layout.cold_span, none of them written down here.
#
# ALL THREE USED TO BE WRONG, and the row said so every run without failing:
# "33,819 byte(s) in 766 run(s)  <-- .cold IS CODE ONLY: THIS IS CORRUPTION",
# on a machine where nothing was wrong. COLD_SEG was pinned at 0x0E00 and has
# since moved twice (0x0E20, then 0x0FC0), so the RAM side read a different
# section entirely; COLD_MAX was pinned at 0x8900 against a section that is
# 0x9ECF; and the base was found by SEARCHING the image for fm_layout's first
# four opcode bytes, which tests/dispcold.py had already argued is two ways of
# being wrong at once - the prologue is not an ABI, and a search that matches
# the wrong place answers confidently.
#
# What made the search necessary was the thing nobody had named: since SPEC.md
# 2.9 a segment delta is not a file offset, so the arithmetic anyone would
# write instead lands BOOT2_PAD bytes early, in the cold thunk table. That is
# what cold_span is for, and it carries the assertions.
COLD_SEG, _COLD_OFF, COLD_MAX = os88layout.cold_span(ROOT)


def coldbase():
    """Where .cold starts inside kernel.bin, and the image it starts in.

    .cold is CODE AND NOTHING ELSE - SPEC.md 2.6 rule 1 forbids a data
    directive there, and tools/os88ovlchk.py enforces it - so unlike .text,
    which is full of initialised data words, a byte of .cold that differs
    from the built image is CORRUPTION and nothing else. That is what makes
    this the sharpest memory check in the tree.
    """
    return _COLD_OFF, open(os.path.join(ROOT, "build", "kernel.bin"),
                           "rb").read()


def colddiff(m, pad=""):
    base, k = coldbase()
    img = k[base:base + COLD_MAX]
    ram = m.read(COLD_SEG << 4, COLD_MAX)
    rs = runs_of(img, ram)
    print("%s.cold vs kernel.bin: %d byte(s) in %d run(s)%s"
          % (pad, sum(l for _, l in rs), len(rs),
             "   <-- .cold IS CODE ONLY: THIS IS CORRUPTION" if rs else ""))
    for off, ln in sorted(rs, key=lambda r: -r[1])[:10]:
        near = sorted((v, n) for n, v in SY.items()
                      if os88sym.sections().get(n) == ".cold" and v <= off)
        who = near[-1][1] if near else "?"
        print("%s     COLD_SEG:%04X %3d  was %s  now %s   (%s+%d)"
              % (pad, off, ln, hexd(img[off:off + min(ln, 8)]),
                 hexd(ram[off:off + min(ln, 8)]), who, off - near[-1][0]))
    return rs


def textdiff(m, pad=""):
    """.text against the image it was built from. THE CONTROL MATTERS: the
    same diff over .bss is all live state, so only the part below BSS0 is
    evidence, and it must be taken while the machine is still healthy as well
    as after - a corrupt byte that was already there before the click is a
    different bug from one the click produced."""
    img = open("build/kernel.bin", "rb").read()[:BSS0]
    ram = m.read(0x0060 << 4, BSS0)
    rs = runs_of(img, ram)
    print("%s.text vs kernel.bin: %d byte(s) in %d run(s)"
          % (pad, sum(l for _, l in rs), len(rs)))
    for off, ln in sorted(rs, key=lambda r: -r[1])[:8]:
        print("%s     +0x%05X %4d  was %s  now %s"
              % (pad, off, ln, hexd(img[off:off + min(ln, 8)]),
                 hexd(ram[off:off + min(ln, 8)])))
    return rs


def postmortem(m, healthy):
    """What in memory is no longer what it was - the kernel image, the
    interrupt vectors and the whole of LOW_SEG, each against a snapshot taken
    while the machine was still answering."""
    print("   --- post-mortem -------------------------------------------")
    for name, (base, before) in sorted(healthy.items()):
        try:
            now = m.read(base, len(before))
        except Exception as e:
            print("   %-10s unreadable: %s" % (name, str(e)[:50])); continue
        rs = runs_of(before, now)
        tot = sum(l for _, l in rs)
        print("   %-10s %d byte(s) changed in %d run(s)%s"
              % (name, tot, len(rs), ":" if rs else ""))
        for off, ln in sorted(rs, key=lambda r: -r[1])[:8]:
            print("        +0x%04X %4d  was %s  now %s"
                  % (off, ln, hexd(before[off:off + min(ln, 8)]),
                     hexd(now[off:off + min(ln, 8)])))
    textdiff(m, "   ")
    colddiff(m, "   ")


def ticks(m):       # THE KERNEL'S OWN tick: sched_unhook stops this and
    return u16(m.read(S("ticks"), 2))            # leaves 0040:006C running


def bios_ticks(m):
    return u16(m.readseg(0x0040, 0x006C, 2))


def show(m, why):
    # PAUSE FIRST. A register dump is only about the instruction you stopped
    # at, and this is reached from the running-poll as well as from a
    # breakpoint - read without stopping, the answer is a smear.
    try:
        m.pause()
    except Exception:
        pass
    r = m.cmd(cmd="regs")
    cs, ip = r["cs"], r["ip"]
    print("   *** %s" % why)
    print("   cs=%04X ip=%04X  ds=%04X es=%04X ss=%04X sp=%04X bp=%04X "
          "flags=%04X (ZF=%d)"
          % (cs, ip, r["ds"], r["es"], r["ss"], r["sp"], r["bp"],
             r["flags"], (r["flags"] >> 6) & 1))
    print("   ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X"
          % (r["ax"], r["bx"], r["cx"], r["dx"], r["si"], r["di"]))
    try:
        code = m.readseg(cs, max(0, ip - 16), 32)
        print("   code @cs:%04X-16: %s" % (ip, hexd(code)))
    except Exception as e:
        print("   code unreadable: %s" % str(e)[:60])
    try:
        stk = m.readseg(r["ss"], r["sp"], 32)
        print("   stack @ss:sp  : %s" % hexd(stk))
        print("   stack words   : %s"
              % " ".join("%04X" % u16(stk, i) for i in range(0, 32, 2)))
    except Exception as e:
        print("   stack unreadable: %s" % str(e)[:60])
    print("   ui_rebootq now  : %02X" % m.read(S("ui_rebootq"), 1)[0])
    try:
        print("   callstack:", m.cmd(cmd="callstack").get("callstack"))
    except Exception as e:
        print("   callstack unavailable: %s" % str(e)[:60])
    return r


def main():
    print("ui_task at 0x%04X (step 0 reads the flag every pass)" % SY["ui_task"])
    flat = S("ui_rebootq")
    # The reboot's OTHER door. `ui_rebootq` is only one of two ways into
    # ui_cmd_reboot (SPEC.md 20.10) and the IVT says the body ran with the
    # flag never set, so the entry points themselves are what to watch.
    watch = {n: S(n) for n in ("ui_cmd", "ui_cmd_reboot", "ui_reboot_post")
             if n in SY}
    # NOT ui_task+7. An exec breakpoint here fires on the 8088's PREFETCH,
    # not on execution - the queue pulls that byte in while IP is still on
    # the `je` at +5 - so it trips on every benign pass and reports ZF=1.
    # Anything one or two bytes downstream of a linear path has the same
    # problem; ui_cmd_reboot survives it only because the `call` at ui_task+12
    # flushes the queue and the push is already on the stack when it fires.
    for n, a in sorted(watch.items()):
        print("watch %-16s flat 0x%05X" % (n, a))
    print("ui_rebootq flat 0x%05X" % flat)

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine="os8088_xt_vga_herc", boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        pri = [i for i, c in cards.items() if c["type"] == "vga"][0]
        sec = [i for i, c in cards.items() if c["type"] == "mda"][0]
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        ctx = m.read(S("vid_ctx"), 84); seam = u16(ctx, 42 + 36)
        print("seam at x=%d" % seam)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
        w = dispcp.win_list(m, S); disk = w[-1]
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "APPS", card=pri)
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "NOTEPAD.O88",
                          card=pri)
        time.sleep(2)
        wins = dispcp.win_list(m, S)
        if len(wins) < 2:
            sys.exit("notepad did not launch")
        np = [s for s in wins if s != disk][-1]
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        target = (seam - nw // 2) & ~7
        mo.drag(nx + nw // 2, ny + TITLE_H // 2,
                target + nw // 2, ny + TITLE_H // 2)
        os88marty.settle(m, card=sec)
        nx, ny, nw, nh = dispcp.win_rect(m, S, np)
        print("notepad x %d..%d (seam %d) %s"
              % (nx, nx + nw - 1, seam,
                 "STRADDLES" if nx < seam <= nx + nw - 1 else "does NOT"))
        bx, by, bw, bh = dispcp.win_rect(m, S, disk)
        print("disk window x %d..%d y %d..%d" % (bx, bx + bw - 1, by, by + bh - 1))

        # THE PUMP IS WHAT MAKES THE FILTER SAFE. A machine stopped at a
        # breakpoint clocks no serial bytes, so a button packet sent while it
        # is stopped is not delivered - and a button LEFT down for the length
        # of the window is not a click at all, it is fm_drag's press-and-hold.
        # So every wait below is spent here, resuming ui_task's own accesses.
        state = {"benign": 0}

        def pump(secs):
            t0 = time.time()
            while time.time() - t0 < secs:
                st = m.status()
                if st["state"] != "breakpoint":     # state_name(), NOT the
                    # THE DIFFERENTIAL. A `mem` breakpoint is a check inside
                    # the CPU's own bus cycle, so it sees CPU writes and
                    # NOTHING ELSE - a DMA transfer lands in memory without
                    # passing it. Finding the byte non-zero while the machine
                    # is still RUNNING therefore says the writer was not the
                    # CPU, which no amount of stopping at the flag can say.
                    if m.read(flat, 1)[0] != 0:
                        return show(m, "flag NON-ZERO (the poll beat `status`; "
                                       "show() pauses before reading regs)")
                    time.sleep(0.01)                # Rust enum's own spelling
                    continue
                # fm_layout's ENTRY, and it is only interesting when DS is
                # wrong: the routine is called from nine sites and a correct
                # DS is the ordinary case, so the filter is the invariant
                # itself (SPEC.md 2.6 rule 1 - cold code's CS is COLD_SEG and
                # its DS is still KERNEL_SEG).
                if st["cs"] == COLD_SEG and \
                        abs(st["ip"] - (SY["fm_layout"])) <= 4:
                    if m.cmd(cmd="regs")["ds"] == KSEG:
                        m.run()
                        continue
                    return show(m, "fm_layout ENTERED WITH DS != KERNEL_SEG")
                hit = [n for n, a in watch.items()
                       if st["cs"] == KSEG and abs(st["ip"] - (a - 0x600)) <= 4]
                if hit:
                    return show(m, "REACHED %s" % hit[0])
                state["benign"] += 1
                if NOP:                 # step 0 cannot be the one accessing
                    return show(m, "ACCESS to ui_rebootq with step 0 NOPped: "
                                   "this is the writer")
                # The branch is unconditional now, so nothing clears the flag
                # and its value is an honest filter again: zero is ui_task's
                # own read, non-zero is the write.
                if m.read(flat, 1)[0] == 0:
                    m.run()
                    continue
                return show(m, "ui_rebootq NON-ZERO at a bus access")
            return None

        LOWSEG = 0x17C0
        STK0_BOT = SY["kernel_low_end"]
        healthy = {}

        def snapshot():
            healthy["IVT"] = (0x0000, m.read(0x0000, 0x0400))
            healthy["LOW_SEG"] = (LOWSEG << 4, m.read(LOWSEG << 4, 0x2400))
        # MAKE THE FLAG STICKY. A `mem` breakpoint trips inside a bus cycle
        # and the CPU then runs on to the next instruction boundary, which on
        # the fatal pass carries it through `mov byte [ui_rebootq], 0` before
        # the host can read anything - so the one stop that matters is
        # indistinguishable from the 70-odd benign ones. NOPping ui_task's
        # step 0 removes both of ui_task's own accesses at once: the flag is
        # then never read and never cleared, the machine never reboots, and
        # every remaining access to that byte is the writer we are looking
        # for. The bytes are CHECKED before they are overwritten, so a kernel
        # that has moved fails here loudly instead of being patched somewhere
        # else.
        # Step 0 grew when the flag became a REQUEST byte (UI_RBQ_*): the
        # cmp/je pair now shelters a mov al / sub al / clear before the call,
        # so the fingerprint is 18 bytes and the je hops 0x0D. The bytes are
        # still CHECKED before they are overwritten, for the same reason.
        step0 = m.read(S("ui_task"), 18)
        want = bytes([0x80, 0x3E, SY["ui_rebootq"] & 0xFF, SY["ui_rebootq"] >> 8, 0x00,          # cmp [q], 0
                      0x74, 0x0D,                            # je .keys
                      0xA0, SY["ui_rebootq"] & 0xFF, SY["ui_rebootq"] >> 8,                      # mov al, [q]
                      0x2C, 0x01,                            # sub al, RBQ_FLUSH
                      0xC6, 0x06, SY["ui_rebootq"] & 0xFF, SY["ui_rebootq"] >> 8, 0x00])         # mov byte [q], 0
        if step0[:17] != want or step0[17] != 0xE8:
            sys.exit("ui_task step 0 is not the expected 18 bytes: %s"
                     % hexd(step0))
        if NOP:
            m.write(S("ui_task"), b"\x90" * 20)             # ...call included
            print("ui_task step 0 NOPped: the flag is now sticky and the "
                  "reboot cannot fire")
        else:
            # THE DISCRIMINATOR, and it fits in the same five bytes.
            #   80 3E B2 CD 00   cmp byte [ui_rebootq], 0
            # becomes
            #   A0 B2 CD         mov al, [ui_rebootq]
            #   3C 00            cmp al, 0
            # which is the identical test with the byte it read LEFT IN AL -
            # and AL is dead here, the next use being int 16h AH=01h's own
            # return. Nothing between +5 and +12 touches AL, so AL at the
            # ui_cmd_reboot catch IS what the compare saw. AL non-zero means
            # the flag really was set; AL ZERO means `cmp al,0` set ZF and the
            # `je` was taken, so control cannot have come through this pair at
            # all and the entry is from somewhere else.
            # ...and `74 0D` (je .keys) becomes `EB 0D` (jmp .keys), so the
            # branch is ALWAYS taken: the read/sub/clear at +7 and the call at +17 are
            # both unreachable, the flag STICKS once written, and the reboot
            # cannot fire. Two bytes for two bytes and the same instruction
            # count, which is why this is the patch to use rather than 15
            # NOPs - those change what the loop does, and six clicks under
            # them produced no write at all.
            m.write(S("ui_task"), bytes([0xA0, SY["ui_rebootq"] & 0xFF,
                                         SY["ui_rebootq"] >> 8, 0x3C, 0x00,
                                         0xEB, 0x0D]))
            print("ui_task step 0 patched: AL carries the byte read, and the "
                  "branch is unconditional so the flag sticks")

        snapshot()
        can = u16(healthy["LOW_SEG"][1], STK0_BOT)
        print("STK0_BOT 0x%04X canary %04X (%s)"
              % (STK0_BOT, can, "SCH_MAGIC" if can == 0x5A57 else "NOT SCH_MAGIC"))
        textdiff(m, "healthy: ")
        colddiff(m, "healthy: ")

        for click in range(1, 7):
            px = bx + bw // 2
            py = by + TITLE_H + 8 + (1 + 2 * (click % 3)) * 16
            print("click %d at (%d,%d)" % (click, px, py))
            try:
                mo.to(px, py)
            except Exception as e:
                print("   position raised: %s" % str(e)[:80]); break
            os88marty.settle(m, card=pri)

            ka, kb = ticks(m), bios_ticks(m)
            m.breakpoints([{"type": "mem", "addr": flat}] +
                          [{"type": "exec", "addr": a} for a in watch.values()] +
                          [{"type": "exec",
                            "addr": (COLD_SEG << 4) + SY["fm_layout"]}])
            state["benign"] = 0
            caught = pump(0.3)
            if caught is None:
                m.mouse(0, 0, l=True)
                caught = pump(0.6)
            if caught is None:
                m.mouse(0, 0, l=False)
                caught = pump(25.0)
            m.breakpoints([])
            if caught is not None:
                print("   (%d benign ui_task accesses before it)" % state["benign"])
                return 1
            m.run()
            k2, kb2 = ticks(m), bios_ticks(m)
            time.sleep(1.2)
            k3, kb3 = ticks(m), bios_ticks(m)
            how = ("ALIVE" if k3 != k2 else
                   "*** REBOOTED (kernel tick dead, BIOS tick alive) ***"
                   if kb3 != kb2 else "*** FROZEN ***")
            print("   %d benign ui_task accesses, nothing else; %s"
                  % (state["benign"], how))
            if colddiff(m, "   ") or how != "ALIVE":
                postmortem(m, healthy)
                return 1
            if how != "ALIVE":
                show(m, "the machine after the click")
                return 1
            os88marty.settle(m, card=pri)
            snapshot()
        print("no non-ui_task access to ui_rebootq in 6 clicks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
