#!/usr/bin/env python3
"""Exercise SOUND.DRV's ESS setup and DMA paths, with scripted I/O ports.

The binary and map are checked against the shipping Makefile recipe. Unicorn
executes the actual routines with SS != DS. This proves register/resource and
8237 programming contracts, not ESS analogue output or a motherboard's DMA.
--break-dma recreates the old channel-1 address port on the DMA-3 legs and
must fail; the row runs that broken arm itself and requires it to fail. An
XT whose disk controller holds IRQ 5 (SPEC.md 34.5.6's evidence) owns DMA 3,
and an unconfirmed part or port keeps DMA 1 rather than refusing. No source
slices or second implementation of the driver are used.
"""
import os
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build
import os88drvsym
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX,
    UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
    UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
    UC_X86_REG_SS, UC_X86_REG_EFLAGS)

SEG, BASE, RET = 0x1000, 0x10000, 0xFFF8
CPU_INFO = 0x0060 * 16 + 0x0155         # OSAPI_CPU_INFO's far cell
FAILS = []
CHECKS = 0
QUIET = False                           # the broken arm's failures are expected


def check(ok, what):
    global CHECKS
    CHECKS += 1
    if not ok:
        FAILS.append(what)
        if not QUIET:
            print("FAIL: " + what)


class Card:
    def __init__(self, image, syms, primary=0, secondary=3, model=0x1868,
                 control=0x800, base=0x220, version=(3, 1), ident=(0x68, 0x88),
                 config_base=None, stuck=None, reset=True, slow=0,
                 xt_disk=False):
        self.syms, self.base, self.control = syms, base, control
        self.model, self.ident, self.reset_ok = model, ident, reset
        self.slow, self.pending = slow, 0       # 2xEh polls before 0AAh
        self.cfg = {0x60: (config_base or base) >> 8,
                    0x61: (config_base or base) & 255,
                    0x74: primary, 0x75: secondary}
        self.stuck = stuck
        self.idx = self.mixidx = 0
        self.mixreads = deque()
        self.fifo = deque()
        self.io, self.writes, self.commands = [], [], []
        self.count_reads = 0
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.mu.mem_map(0, 0x100000)
        self.mu.mem_write(BASE, image)
        # OSAPI_CPU_INFO: mov al, tier / mov ah, 0 / retf - an 8088 or a 386
        self.mu.mem_write(CPU_INFO, bytes((0xB0, 0 if xt_disk else 3,
                                           0xB4, 0, 0xCB)))
        # int 0Dh: an XT disk ROM's (C800h) or the BIOS's own dummy (F000h)
        self.mu.mem_write(0x0D * 4, (0x0100 | (0xC800 if xt_disk else
                                              0xF000) << 16).to_bytes(4, "little"))
        self.mu.hook_add(UC_HOOK_INSN, self.inb, None, 1, 0, UC_X86_INS_IN)
        self.mu.hook_add(UC_HOOK_INSN, self.outb, None, 1, 0, UC_X86_INS_OUT)
        for name, value in (("sbl_base", base),):
            self.put(name, value, 2)
        for name, value in (("sbl_verhi", version[0]), ("sbl_verlo", version[1]),
                            ("sbl_dma", 1), ("sbl_dmaport", 2),
                            ("sbl_pageport", 0x83), ("sbl_auto", 1)):
            self.put(name, value)

    def put(self, name, value, size=1):
        self.mu.mem_write(BASE + self.syms[name], value.to_bytes(size, "little"))

    def get(self, name, size=1):
        return int.from_bytes(self.mu.mem_read(BASE + self.syms[name], size), "little")

    def inb(self, mu, port, size, _):
        assert size == 1, "byte I/O only"
        if port == self.base + 0x0C:
            return 0
        if port == self.base + 0x0E:
            if self.pending:
                self.pending -= 1
                return 0
            return 0x80 if self.fifo else 0
        if port == self.base + 0x0A:
            return self.fifo.popleft() if self.fifo else 0xFF
        if port == self.base + 5:
            return self.mixreads.popleft() if self.mixreads else 0xFF
        if port == self.control + 1:
            return self.cfg.get(self.idx, 0xFF)
        if port in (3, 7, 0xC6):
            v = (0x1234 >> (8 * (self.count_reads & 1))) & 255
            self.count_reads += 1
            self.io.append(("in", port, v))
            return v
        return 0xFF

    def outb(self, mu, port, size, value, _):
        assert size == 1, "byte I/O only"
        self.io.append(("out", port, value))
        if port == self.base + 0x0C:
            self.commands.append(value)
            if value == 0xE7:
                self.fifo.extend(self.ident)
        elif port == self.base + 6 and value == 0:
            self.fifo.clear()
            if self.reset_ok:
                self.fifo.append(0xAA)
                self.pending = self.slow
        elif port == self.base + 4:
            self.mixidx = value
            if value == 0x40:
                self.mixreads = deque((self.model >> 8, self.model & 255,
                                       self.control >> 8, self.control & 255))
        elif port == self.control:
            self.idx = value
        elif port == self.control + 1:
            check(not mu.reg_read(UC_X86_REG_EFLAGS) & 0x200,
                  "configuration writes are interrupt protected")
            self.writes.append((self.idx, value))
            if self.idx != self.stuck:
                self.cfg[self.idx] = value
        elif port in (0x0C, 0xD8):
            self.count_reads = 0

    def run(self, name, ax=0x1122, bx=0x3344, cx=0x5566, dx=0x7788, flags=0x202,
            count=300_000):
        mu = self.mu
        seed = {UC_X86_REG_BX: bx, UC_X86_REG_CX: cx, UC_X86_REG_DX: dx,
                UC_X86_REG_SI: 0x2345, UC_X86_REG_DI: 0x3456,
                UC_X86_REG_BP: 0x4567, UC_X86_REG_DS: SEG,
                UC_X86_REG_ES: 0x3000, UC_X86_REG_SS: 0x2000}
        for reg, value in seed.items():
            mu.reg_write(reg, value)
        mu.reg_write(UC_X86_REG_AX, ax)
        mu.reg_write(UC_X86_REG_CS, SEG)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.reg_write(UC_X86_REG_EFLAGS, flags)
        mu.mem_write(0x20000 + 0xFFF0, RET.to_bytes(2, "little"))
        mu.emu_start(BASE + self.syms[name], BASE + RET, count=count)
        check(mu.reg_read(UC_X86_REG_SP) == 0xFFF2, name + " returns on caller stack")
        for reg, value in seed.items():
            if name in ("sbl_dleft", "snd_hwinfo") and reg == UC_X86_REG_CX:
                continue
            if name == "snd_hwinfo" and reg == UC_X86_REG_BX:
                continue
            check(mu.reg_read(reg) == value, name + " preserves register " + str(reg))
        check((mu.reg_read(UC_X86_REG_EFLAGS) & 0x200) == (flags & 0x200),
              name + " preserves IF")
        result = mu.reg_read(UC_X86_REG_AX)
        if name in ("sbl_dma_auto", "sbl_dleft"):
            check(result & 0xFF00 == ax & 0xFF00, name + " preserves AH")
        elif name not in ("sbl_ess_setup", "snd_hwinfo", "sbl_find",
                          "sbl_attach"):
            check(result == ax, name + " preserves AX")
        return result


def setup_cases(image, syms):
    for model in (0x1868, 0x1869):
        for base in (0x220, 0x240):
            for primary, secondary, want, routes in (
                    (0, 3, 3, (3, 4)), (1, 3, 1, (1, 3)),
                    (3, 0, 3, (3, 0)), (4, 1, 1, (1, 4)),
                    (0, 1, 1, (1, 4)), (2, 2, 255, (2, 2)),
                    (0, 0, 255, (0, 0)), (4, 4, 1, (4, 4))):
                c = Card(image, syms, primary, secondary, model=model, base=base)
                c.run("sbl_ess_setup")
                label = f"ES{model:04x} @{base:x} DMA {primary},{secondary}"
                check(c.get("sbl_dma") == want, label + f" selects {want}")
                check((c.cfg[0x74], c.cfg[0x75]) == routes, label + " routes")
                if want != 255 and (primary, secondary) != (4, 4):
                    check(c.get("sbl_dmaport") == {1: 2, 3: 6}[want], label + " address port")
                    check(c.get("sbl_pageport") == {1: 0x83, 3: 0x82}[want], label + " page port")
                if primary not in (1, 3) and secondary in (1, 3):
                    check(c.writes[:2] == [(0x75, 4), (0x74, want)],
                          label + " disconnects Audio 2 before moving its channel")
                check(not c.fifo, label + " leaves no pending DSP answers")
    for args, want in (({"model": 0x1688}, 1), ({"ident": (0x48, 0x88)}, 1),
                       ({"ident": ()}, 1), ({"version": (4, 5)}, 1),
                       ({"version": (3, 0)}, 1), ({"control": 0}, 1),
                       ({"control": 0x801}, 1), ({"control": 0x1000}, 1),
                       ({"config_base": 0x240}, 1), ({"reset": False}, 1),
                       ({"ident": (), "slow": 5000}, 1)):
        # An unconfirmed part, port or reset is not evidence: DMA 1, as the
        # driver did before 34.5.8. The slow non-ESS 3.01 reset is the SB Pro
        # 2 that a 4096-poll reset here once refused outright.
        c = Card(image, syms, **args)
        c.run("sbl_ess_setup", flags=2, count=2_000_000)
        check(c.get("sbl_dma") == want, f"identification/refusal {args}")
        check(not c.writes, f"no configuration writes for {args}")
    for stuck in (0x74, 0x75):
        c = Card(image, syms, stuck=stuck)
        c.run("sbl_ess_setup")
        check(c.get("sbl_dma") == 255, f"read-back failure {stuck:x} refuses PCM")
        check((c.cfg[0x74], c.cfg[0x75]) == (0, 3), "failed route rolls back both resources")
    for primary, secondary, want, routes in (
            (0, 3, 255, (0, 3)), (3, 1, 1, (1, 4)), (3, 0, 255, (3, 0)),
            (1, 3, 1, (1, 3)), (3, 4, 255, (3, 4))):
        c = Card(image, syms, primary, secondary, xt_disk=True)
        c.run("sbl_ess_setup")
        label = f"XT disk on IRQ 5/DMA 3, ESS {primary},{secondary}"
        check(c.get("sbl_dma") == want, label + f" selects {want}")
        check((c.cfg[0x74], c.cfg[0x75]) == routes, label + " routes")
    c = Card(image, syms, 0, 3, slow=5000)
    c.run("sbl_ess_setup", count=2_000_000)
    check(c.get("sbl_dma") == 3, "a slow ESS reset still configures DMA 3")
    c = Card(image, syms, reset=False)
    for name in ("sbl_dma", "sbl_dmaport", "sbl_pageport"):
        c.put(name, 0)
    c.run("sbl_find", count=2_000_000)
    check((c.get("sbl_dma"), c.get("sbl_dmaport"), c.get("sbl_pageport"))
          == (1, 2, 0x83), "sbl_find itself sets DMA 1, never channel 0")
    c = Card(image, syms, 0, 0)
    c.run("sbl_ess_setup")
    check(c.get("sbl_dma") == 255, "evidence refusal before the attach")
    c.put("sbl_irq", 255)                # what sbl_find does with it
    c.io.clear()
    c.run("sbl_attach")
    check(c.mu.reg_read(UC_X86_REG_EFLAGS) & 1, "a refused route refuses the tier")
    check(not [p for op, p, _ in c.io if op == "out"],
          "a refused tier touches no port, the 8237 included")
    check(syms["sbl_ess_setup"] >= syms["snd_keepmix"], "ESS setup is attach-only")
    print("essdma: ESS identification, routes, refusal and rollback checked")


def runtime_cases(image, syms, broken=False):
    for dma in (1, 3):
        c = Card(image, syms, primary=dma, secondary=4)
        c.run("sbl_ess_setup")
        if dma == 3 and broken:
            c.put("sbl_dmaport", 2)
        address, count, page = {1: (2, 3, 0x83), 3: (6, 7, 0x82)}[dma]
        for mode in (0x49, 0x59, 0x55):
            c.io.clear()
            c.run("sbl_dma_auto", ax=0x4567, bx=0x2345, cx=0x0600 | mode)
            writes = [(p, v) for op, p, v in c.io if op == "out"]
            check(writes == [(0x0A, dma | 4), (0x0C, dma | 4),
                            (address, 0x67), (address, 0x45), (0x0C, 0x45),
                            (count, 0x45), (count, 0x23), (0x0B, (mode & 0xFC) | dma),
                            (page, 6), (0x0A, dma)], f"DMA {dma} exact transfer recipe {mode:x}")
        c.put("sbl_dmaoff", 0x2000, 2)
        c.put("sbl_page", 3)
        for half in (0, 1):
            c.io.clear()
            c.run("sbl_arm_half", ax=half)
            values = [v for op, p, v in c.io if op == "out" and p == address]
            check(values == [0, 0x20 + half * 8], f"DMA {dma} single-cycle half {half}")
        c.io.clear()
        c.run("sbl_dleft")
        check(c.mu.reg_read(UC_X86_REG_CX) == 0x1235, f"DMA {dma} bytes left")
        check([p for op, p, _ in c.io if op == "in"] == [count, count],
              f"DMA {dma} reads its own count register")
        c.put("sbl_hdma", 5)
        c.put("sbl_fmt", 2)
        c.run("sbl_dleft")
        check(c.mu.reg_read(UC_X86_REG_CX) == 0x246A, "SB16 high DMA still counts words")
        c.put("sbl_fmt", 0)
        for hisp in (0, 1):
            c.put("sbl_hisp", hisp)
            c.put("sbl_xoff", 0x4567, 2)
            c.put("sbl_xcnt", 0x2345, 2)
            c.put("sbl_xpage", 6)
            c.put("sbl_half", 2048, 2)
            c.io.clear()
            c.run("sbl_hw_start")
            check(("out", page, 6) in c.io, f"DMA {dma} playback uses selected page register")
            c.io.clear()
            c.put("sbl_str_act", 1)
            c.put("sbl_direct", 0x101, 2)  # external ring: owns no heap claims
            c.put("sbl_ackp", c.base + 14, 2)
            c.run("sbl_stop_stream")
            check(("out", 0x0A, dma | 4) in c.io, f"DMA {dma} close masks selected channel")
            check(not c.get("sbl_str_act"), "close invalidates stream")
        c.put("sbl_hisp", 1)
        for blocked in (0, 4):
            c.put("sbl_drqmask", blocked)
            c.io.clear()
            c.run("sbl_halt")
            check(c.io == [("out", 0x0A, dma | 4)], f"DMA {dma} high-speed pause")
            c.io.clear()
            c.run("sbl_go_on")
            check(c.io == [("out", 0x0A, dma | blocked)], f"DMA {dma} high-speed resume (PicoMEM mask {blocked})")
        c.put("sbl_drqmask", 0)
        c.put("sbl_hisp", 0)
        c.put("sbl_rmask", 0x1FFF, 2)
        c.io.clear()
        c.run("sbl_hw_start_in")
        check(("out", 0x0B, 0x54 | dma) in c.io and ("out", page, 6) in c.io,
              f"DMA {dma} recording uses selected channel/page")
        c.run("snd_hwinfo")
        check((c.mu.reg_read(UC_X86_REG_BX) >> 8) == dma, f"DMA {dma} exported to DOS")
        c.run("sbl_state_init")
        check(c.get("sbl_dma") == dma, f"DMA {dma} survives tier state reset")
    c = Card(image, syms)
    c.put("sbl_dma", 255)
    c.run("snd_hwinfo")
    check(c.mu.reg_read(UC_X86_REG_EFLAGS) & 1, "refused ESS cannot export a guessed BLASTER")
    print("essdma: playback, recording, high-speed pause/resume, close, count and DOS checked")


def main():
    syms = os88drvsym.symbols("sound.bin")
    with open(os88build.at("build/sound.bin"), "rb") as f:
        image = f.read()
    broken = "--break-dma" in sys.argv
    setup_cases(image, syms)
    runtime_cases(image, syms, broken)
    if not broken:                      # the negative control, every run
        global QUIET
        kept, QUIET = list(FAILS), True
        runtime_cases(image, syms, True)
        QUIET = False
        bit = len(FAILS) - len(kept)
        FAILS[:] = kept
        check(bit > 0, "--break-dma's old channel-1 port fails the DMA-3 legs")
        print(f"essdma: the broken arm failed {bit} assertions, as it must")
    print(f"essdma: {CHECKS} checks, {len(FAILS)} failed (scripted ports; no physical ESS)")
    return bool(FAILS)


if __name__ == "__main__":
    sys.exit(main())
