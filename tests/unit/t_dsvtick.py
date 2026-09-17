#!/usr/bin/env python3
"""A driver that publishes a sound verb must know DSV_TICK exists (SPEC.md 34.13.7).

THE DEFECT THIS GUARDS.  Since decision D6 of docs/RADBOX-PLAN.md the kernel
takes DX as its new `DSV_TICK` after every successful `DSV_FM` verb, every
`DSV_STREAM` verb but 3, and `DSV_RELINST` - and far-calls whatever it took
from inside IRQ0, at IF = 0, 18.2 times a second. A driver written before
that, or by somebody who has not read the section, answers its verbs with DX
as it happened to leave it: the requesting instance stamped in DH, a rate, a
count. The first successful verb plants that in a cell IRQ0 jumps through.
Nothing assembles differently, no table length moves (`DRV_H_DSV` does not
catch it, because no cell was added), and the machine dies on the next tick.

`drivers/hda/` on `origin/codex/hda-1015pn` is that driver today: its
`hda_stream` publishes `DSV_STREAM` and never names `DSV_TICK`. Whichever of
it and the switched tick merges second carries the fix; this row is what makes
"second" impossible to forget.

THE RULE, and its reach, said as narrowly as SPEC.md 34.13.7 says it:

    a driver whose sources write a NON-ZERO DSV_FM, DSV_STREAM or DSV_RELINST
    cell must name DSV_TICK somewhere in those sources

A driver is a directory under `drivers/` - every `.asm` and `.inc` in it is
one assembly. It catches a driver that has never HEARD of the tick. It cannot
catch an old `SOUND.DRV`, which named `DSV_TICK` to publish its watchdog at
attach, and it does not try: SPEC.md names that hazard and it is a pairing
question, not a source one.

MUTATION-TESTED IN PLACE: the two fixtures below are an HDA-shaped source that
must be refused and a shipped-shape one that must pass, run through the same
scanner as the tree, and the tree must yield at least one publisher - the
liveness check, so a regex that stops matching fails rather than passing
vacuously.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harness import check, done                                # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
DRIVERS = os.path.join(ROOT, "drivers")

# `mov word [tbl+DSV_STREAM], hda_stream` - the publish shape every driver in
# the tree uses (the table is built at attach, never authored with `dw`).
PUBLISH = re.compile(r"\[\s*[\w.]+\s*\+\s*DSV_(FM|STREAM|RELINST)\s*\]\s*,"
                     r"\s*([^\s;]+)")
# ...and an AUTHORED table, `dw hda_stream ; DSV_STREAM`, in case one ever is.
AUTHORED = re.compile(r"^\s*dw\s+([^\s;]+)\s*;\s*DSV_(FM|STREAM|RELINST)\b",
                      re.M)
TICK = re.compile(r"\bDSV_TICK\b")


def strip_comments(src):
    return "\n".join(line.split(";", 1)[0] for line in src.splitlines())


def verdict(sources):
    """(publishes, names_tick) for one driver's concatenated sources."""
    code = strip_comments(sources)
    pubs = [v for _, v in PUBLISH.findall(code) if v not in ("0", "0h", "0x0")]
    pubs += [v for v, _ in AUTHORED.findall(sources)
             if v not in ("0", "0h", "0x0")]
    return bool(pubs), bool(TICK.search(code))


HDA_SHAPE = """
    mov word [hda_services+DSV_CAPS], SND_CAP_PCM_BG
    mov word [hda_services+DSV_STREAM], hda_stream
    mov word [hda_services+DSV_STREAM], 0
"""
SOUND_SHAPE = HDA_SHAPE + """
    mov [snd_services+DSV_TICK], dx     ; the atomic answer
"""
COMMENT_ONLY = HDA_SHAPE + "; DSV_TICK is somebody else's problem\n"


def main():
    p, t = verdict(HDA_SHAPE)
    check((p, t) == (True, False),
          "the scanner refuses an HDA-shaped driver (fixture)",
          "the negative control: if this passes the row cannot fail",
          got=(p, t), want=(True, False))
    check(verdict(SOUND_SHAPE) == (True, True),
          "the scanner passes a driver that answers DX (fixture)",
          "the positive control", got=verdict(SOUND_SHAPE), want=(True, True))
    check(verdict(COMMENT_ONLY)[1] is False,
          "a DSV_TICK in a COMMENT does not count (fixture)",
          "a driver that only mentions the tick has not answered it",
          got=verdict(COMMENT_ONLY), want=(True, False))

    publishers = 0
    for name in sorted(os.listdir(DRIVERS)):
        d = os.path.join(DRIVERS, name)
        if not os.path.isdir(d):
            continue
        src = ""
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if f.endswith((".asm", ".inc")):
                    src += open(os.path.join(root, f), errors="replace").read()
                    src += "\n"
        pubs, tick = verdict(src)
        if not pubs:
            continue
        publishers += 1
        check(tick, "drivers/%s publishes a sound verb and answers DSV_TICK"
              % name,
              "SPEC.md 34.13.7: the kernel takes DX as DSV_TICK after every "
              "successful DSV_FM verb, DSV_STREAM verb but 3 and DSV_RELINST, "
              "and far-calls it from IRQ0. Every CF = 0 exit must answer DX = "
              "the driver's tick proc or 0 - a driver that never wants the "
              "tick answers 0 everywhere", got=False, want=True)

    check(publishers >= 1, "the scanner still finds SOUND.DRV as a publisher",
          "liveness: a pattern that stops matching must fail, not pass",
          got=publishers, want=">= 1")
    print("t_dsvtick: %d sound-verb publisher(s) under drivers/" % publishers)
    done("t_dsvtick")


if __name__ == "__main__":
    main()
