#!/usr/bin/env python3
"""sbirq5: SOUND.DRV refuses IRQ 5 when it is TAKEN on an XT (SPEC.md 34.5.6).

On an IBM XT the fixed-disk controller owns IRQ 5; a Sound Blaster jumpered
there plays one block and falls silent. sbl_f_irqdisc - cut out of sb.inc as
it ships, with sbl_irq5_taken, by tests/picomem.py's sbslice - runs in
Unicorn on tests/picomem/sbhx.asm, an SB16-class DSP whose mixer register
80h names the card's line, under a planted OSAPI_CPU_INFO tier and IRQ 5
vector (int 0Dh):

  8088 (an XT), int 0Dh in a disk ROM (C800h), card on 5  REFUSED, one toast
  ...asked again                                           refused, no 2nd toast
  8088, int 0Dh the BIOS's own dummy (F000h), card on 5    accepted on 5
  286 (an AT), int 0Dh in a ROM, card on 5                 accepted on 5
  386, int 0Dh in a ROM, card on 5                         accepted on 5
  8088, disk ROM on IRQ 5, card on IRQ 7                   accepted on 7

The accepted cases are the negative controls: a refusal that fired on every
IRQ 5 (or every XT) would fail them. Host only: python3 + unicorn.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import picomem as P                                     # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


MIXLINE = {5: 2, 7: 4}          # line -> register 80h's bit: 2 = IRQ5, 4 = IRQ7


def case(image, syms, cpu, vec13, line, want_al, want_irq, toasts,
         what, again=False):
    st = {"idx": 0, "pic": 0xFF}

    def inb(p):
        if p == 0x21:
            return st["pic"]
        if p == 0x225:                  # the CT1745 mixer's data port
            return MIXLINE[line] if st["idx"] == 0x80 else 0xFF
        return 0xFF

    def outb(p, v):
        if p == 0x21:
            st["pic"] = v
        elif p == 0x224:
            st["idx"] = v

    box = P.Box(image, syms, inb, outb)
    box.put(P.BASE + syms["res_cpu"], bytes([cpu]))
    for n in (2, 3, 5, 7):
        vec = vec13 if n == 5 else P.DEFVEC
        box.put((8 + n) * 4, vec[0].to_bytes(2, "little") +
                vec[1].to_bytes(2, "little"))
    box.put(0x46C, b"\0\0")
    box.go("entry_disc16")
    if again:
        box.go("entry_disc16")
    al, irq, t = box.b("res_al"), box.b("sbl_irq"), box.b("res_toast")
    check(al == want_al and irq == want_irq and t == toasts,
          "%s - AL %d, sbl_irq %02X, %d toast(s)" % (what, al, irq, t))


def main():
    tmp = tempfile.mkdtemp(prefix="sbirq5-")
    P.sbslice(tmp)
    image, syms = P.assemble(tmp, os.path.join(HERE, "picomem", "sbhx.asm"),
                             [os.path.join(P.ROOT, "drivers", "sound"), tmp],
                             "sbhx")
    rom = (0x1234, 0xC800)              # the ST-506 card's handler
    print("sbirq5: SOUND.DRV and a taken IRQ 5 (SPEC.md 34.5.6)")
    case(image, syms, 0, rom, 5, 0, 0xFF, 1,
         "an 8088, IRQ 5 hooked by a disk ROM, card on 5: REFUSED, said once")
    case(image, syms, 0, rom, 5, 0, 0xFF, 1,
         "...asked again: refused, and not said twice", again=True)
    case(image, syms, 0, P.DEFVEC, 5, 1, 5, 0,
         "an 8088, IRQ 5 the BIOS's own dummy: accepted on 5")
    case(image, syms, 1, rom, 5, 1, 5, 0,
         "a 286 (an AT), IRQ 5 hooked: accepted on 5 - its disk is on 14")
    case(image, syms, 2, rom, 5, 1, 5, 0,
         "a 386, IRQ 5 hooked: accepted on 5")
    case(image, syms, 0, rom, 7, 1, 7, 0,
         "an 8088, disk on IRQ 5, card on 7: accepted on 7")
    if FAILS:
        print("sbirq5: %d FAILED" % len(FAILS))
        return 1
    print("PASS sbirq5")
    return 0


if __name__ == "__main__":
    sys.exit(main())
