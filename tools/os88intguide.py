#!/usr/bin/env python3
"""The INTERRUPTS page: the first 1.5 KB of an IBM PC's memory, what an
interrupt is, how the 8088 takes one, every vector in the table - the ones a
PC has and the ones os8088 and its DOS install - walkable, beside the boot
ladder (tools/os88ladder.py) in the same hand.

    python3 tools/os88intguide.py                  # -> build/interrupts.html
    python3 tools/os88intguide.py --selfcheck      # ...is it still TRUE?
    python3 tools/os88intguide.py -o PATH          # somewhere else
    python3 tools/os88intguide.py --fragment       # no <html>/<head>/<body>

**ON DEMAND, LIKE THE LADDER - NOTHING IN `make` RUNS IT.** Unlike the ladder
it needs no emulator and no build: it is a description, written down, and it
takes a fraction of a second.

**WHAT GOES STALE, AND WHAT CATCHES IT.** Half of the page is the PC itself -
the 8088, the 8259, IBM's table - and that has not moved since 1981. The other
half is what os8088 does with the table, and THAT moves whenever somebody
rehooks a vector. So every claim about os8088 is written next to an ANCHOR: a
file and a pattern that must still match for the claim to stand (`A(...)`
below). `--selfcheck` runs every anchor, and the page is never written
without it - a vector that loses its hook makes the generator refuse, naming
the file to go and look at, rather than write a page that is confidently
wrong. An anchor is evidence that the thing still exists, not proof the prose
is still right: when one fails, re-read the code before you re-point it.

THE PROSE RULE IS THE LADDER'S, and is checked rather than intended: the
reader is technically literate and knows NOTHING about this machine or this
operating system. A term that is the subject is marked `[[term]]` and defined
in GLOSS, in words that need no further looking up (a definition may not mark
another term). And no page text cites a specification section, a source file
or a symbol - the reader has none of those. The anchors carry that.

THE PAGE HAS ROOM FOR A SECOND CHAPTER. The bar across the top is the whole
first 1,536 bytes - the table, the BIOS data area and the 0500h page - and
chapter 2, the BIOS's own state and the disk path through it, is the rest of
that bar. It is here as a PLANNED chapter (CHAPTERS, the `low` rung): its
regions are drawn hatched and its rung says what it will cover. Adding it is
adding rungs with `chapter="low"` and region steps; nothing in the page
script is chapter-specific.
"""

import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, "tools", "intguide")


class Stale(SystemExit):
    """The tree moved under the page. Always names the file to look at."""

    def __init__(self, what, where, fix):
        SystemExit.__init__(
            self, "os88intguide: %s\n  it lived in: %s\n  what to do : %s\n"
                  "  (the page is NOT rebuilt by `make`: this check is the only "
                  "thing that notices)" % (what, where, fix))


# -----------------------------------------------------------------------------
# 1. ANCHORS. Every os8088-specific sentence on the page stands on at least
#    one of these. (file, regex): the regex must match somewhere in the file.
# -----------------------------------------------------------------------------

def A(path, rx):
    return (path, rx)


ANCHORS_SEEN = []


def check_anchor(a, who):
    path, rx = a
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        raise Stale("%s cites a file that is gone" % who, path,
                    "find where the code went, re-read it, and re-point the "
                    "anchor - or rewrite the claim")
    src = open(full, errors="replace").read()
    if not re.search(rx, src, re.M | re.I):
        raise Stale("%s no longer matches /%s/" % (who, rx), path,
                    "re-read the code: if the claim still holds, re-point the "
                    "anchor; if it does not, the prose is now wrong too")
    ANCHORS_SEEN.append(a)


# -----------------------------------------------------------------------------
# 2. THE GLOSSARY.
# -----------------------------------------------------------------------------

GLOSS = {
    'interrupt table':
        'The first 1,024 bytes of memory: 256 entries of four bytes, one '
        'per interrupt number, each holding the address of the code to run '
        'when that number arrives. The processor itself knows where it is.',
    'vector':
        'One entry of the table: a four-byte address, the place in memory '
        'the processor jumps to for that interrupt number. "Hooking" an '
        'interrupt means writing your own address there.',
    'segment:offset':
        "The 8088's way of writing a memory address: two 16-bit numbers, "
        'the first multiplied by 16 and added to the second. 0000:0024 is '
        'byte 36 of memory; F000:E987 is byte FE987h, inside the ROM.',
    'interrupt controller':
        'The Intel 8259A, a chip beside the processor that collects eight '
        'hardware request lines, decides which is most urgent, and tells '
        'the processor which interrupt number to run. An AT has two, the '
        'second wired into one input of the first.',
    'IRQ':
        'Interrupt ReQuest: one of the controller\'s numbered input lines. '
        'IRQ 0 is the timer, IRQ 1 the keyboard. A PC or XT has eight; an '
        'AT has sixteen.',
    'interrupt flag':
        'One bit in the processor\'s status word. While it is clear the '
        'processor ignores hardware requests entirely; the instructions CLI '
        'and STI clear and set it. It does not stop an INT instruction or '
        'a fault.',
    'end of interrupt':
        'The byte 20h written to the controller\'s port 20h. Until a '
        'handler sends it, the controller believes that line is still being '
        'served and holds it - and every less urgent line - back.',
    'IRET':
        'The instruction that ends a handler. It takes back the three '
        'things the processor saved on the way in - the return address and '
        'the status word - so the interrupted code carries on as if nothing '
        'had happened.',
    'mask':
        'A register in the controller with one bit per line. A set bit '
        'shuts that line out completely, however urgent; a clear bit lets '
        'it through.',
    'stack':
        'A region of memory used last-in, first-out, for return addresses '
        'and saved values. The register pair SS:SP points at its top, and it '
        'grows downward: saving a value moves SP down by two.',
    'chain':
        'To pass an interrupt on: after doing its own work, a handler jumps '
        'to (or calls) whatever address was in the table before it hooked '
        'it, so the earlier owner still sees every interrupt.',
    'BIOS':
        'The firmware in ROM at the top of the first megabyte: the self-test '
        'at power-on, the code that reads the boot sector, and a library of '
        'services - screen, disk, keyboard - reached by numbered interrupts.',
    'port':
        "An address in the processor's separate input/output space, reached "
        'with the IN and OUT instructions rather than memory instructions. '
        "The keyboard's data byte is port 60h; the controller is ports 20h "
        'and 21h.',
    'scan code':
        'The number a keyboard sends for a key going down or coming up. It '
        'names the key\'s position, not its letter; turning it into "a" or '
        '"A" is the handler\'s job.',
    'timer chip':
        'The Intel 8253, which counts down a 1.19 MHz clock. Its first '
        'channel is wired to IRQ 0 and, left as the firmware sets it, '
        'fires 18.2 times a second.',
    'tick':
        'One IRQ 0 from the timer: 18.2 a second, or one every 55 '
        'milliseconds. The firmware counts them to keep the time of day.',
    'exception':
        'An interrupt the processor raises on itself, because an '
        'instruction could not be completed as written - dividing by zero, '
        'say. No chip and no INT instruction is involved.',
    'task switch':
        'os8088 stopping one program part way through and resuming another, '
        'by saving one set of registers and loading the other. Done often '
        'enough, every program appears to run at once.',
    'protected mode':
        'The second way a 286 or later processor can run, in which a '
        'segment number is a reference into a table the system keeps rather '
        'than an address, and the first 32 interrupt numbers are reserved '
        'for the processor\'s own faults.',
    'DMA':
        'Direct Memory Access: a chip that moves data between a device and '
        'memory by itself while the processor does something else. The '
        'floppy drive delivers every sector this way.',
    'critical section':
        'A few instructions that must not be interrupted half way, written '
        'between saving the status word with interrupts turned off and '
        'putting it back - so they cannot be cut in two by a hardware line.',
    'option ROM':
        'A second piece of firmware on an expansion card - a hard-disk '
        "controller's, an EGA's - which the main firmware finds and runs at "
        'power-on, and which typically hooks the vectors for its own '
        'device.',
    'PSP':
        'The 256-byte header DOS builds in front of every program it starts: '
        'the command line, the exit address, and where the program\'s memory '
        'ends.',
    'polling':
        'Asking a device over and over whether it has anything, instead of '
        'letting it raise an interrupt line. Simpler and wasteful: the '
        'processor does the asking whether or not there is anything to find.',
}
_GLOSS_LC = dict((k.lower(), v) for k, v in GLOSS.items())


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


TERM = re.compile(r"\[\[([^\]|]+)(?:\|([^\]]+))?\]\]")
JUMP = re.compile(r"\{\{([0-9A-Fa-f]{2})(?:\|([^}]+))?\}\}")
RUNG = re.compile(r"\{\{@([a-z0-9-]+)\|([^}]+)\}\}")
CODE = re.compile(r"`([^`]+)`")


def inline(text, who):
    """Escape, then the four marks: [[term]], {{09}}, {{@rung|text}}, `code`."""
    def term(m):
        key = m.group(1).lower()
        if key not in _GLOSS_LC:
            raise Stale("%s marks `%s`, which has no definition" % (who, key),
                        "GLOSS in tools/os88intguide.py",
                        "add one, in words that need no looking up - or take "
                        "the marks off and say it plainly")
        return '<b class="gl" tabindex="0" data-g="%s">%s</b>' % (
            esc(key), esc(m.group(2) or m.group(1)))
    s = esc(text)
    s = TERM.sub(term, s)
    s = RUNG.sub(lambda m: '<button type="button" class="jump" onclick="igRung'
                 "('%s')\">%s</button>" % (m.group(1), m.group(2)), s)
    s = JUMP.sub(lambda m: '<button type="button" class="jump" onclick="igJump'
                 '(%d)">%s</button>' % (int(m.group(1), 16),
                                        m.group(2) or "INT %sh" %
                                        m.group(1).upper()), s)
    s = CODE.sub(lambda m: "<code>%s</code>" % m.group(1), s)
    return s


def md(text, who):
    """Paragraphs on blank lines; a run of lines starting '- ' is a list."""
    if not text:
        return ""
    out = []
    for block in re.split(r"\n\s*\n", text.strip()):
        lines = [l.strip() for l in block.strip().split("\n")]
        if lines[0].startswith("- "):
            items, cur = [], None
            for l in lines:
                if l.startswith("- "):
                    if cur is not None:
                        items.append(cur)
                    cur = l[2:]
                else:
                    cur += " " + l
            items.append(cur)
            out.append("<ul>%s</ul>" % "".join(
                "<li>%s</li>" % inline(i, who) for i in items))
        else:
            out.append("<p>%s</p>" % inline(" ".join(lines), who))
    return "".join(out)


# -----------------------------------------------------------------------------
# 3. THE TABLE. 256 vectors: a default for every range, then the ones worth
#    a paragraph. `own` says whose code answers each vector in each of the
#    three moments the map can be coloured by; `quiet` marks the ones whose
#    answer is a handler that does nothing.
# -----------------------------------------------------------------------------

KINDS = {
    "cpu":   "Raised by the processor",
    "irq":   "Hardware line",
    "bios":  "Firmware service",
    "hook":  "Hook the firmware calls",
    "ptr":   "Pointer to a table, not code",
    "dos":   "DOS",
    "drv":   "Driver service",
    "user":  "Left for programs",
    "basic": "ROM BASIC's",
    "free":  "Unassigned",
}
KIND_ORDER = ["cpu", "irq", "bios", "hook", "ptr", "dos", "drv", "basic",
              "user", "free"]
OWNERS = {
    "rom":  "the firmware",
    "os":   "os8088",
    "dos":  "os8088's DOS",
    "none": "nothing (zero)",
}
OWN_ORDER = ["rom", "os", "dos", "none"]
VIEWS = [
    {"id": "kind", "name": "What it is",
     "hint": "the conventional use of every number",
     "cap": "The conventional assignment, as IBM's technical reference and "
            "DOS laid it out - and as every PC program since has assumed. "
            "Hover a square for its name; click it for the whole story."},
    {"id": "boot", "name": "At power-on", "fact": "At power-on",
     "mark": "a do-nothing handler",
     "hint": "as the firmware leaves the table when it reads the boot sector",
     "cap": "The table the moment the firmware hands over to the floppy, on "
            "an IBM PC with no hard disk. Everything from 20h up is still "
            "zero: there is no DOS, and nothing else has been loaded."},
    {"id": "desk", "name": "os8088 desktop", "fact": "On the desktop",
     "mark": "a do-nothing handler",
     "hint": "while os8088's desktop runs, on the same PC",
     "cap": "What os8088 changes is small on purpose: the timer, the "
            "keyboard, the two serial lines for the mouse, and one table "
            "pointer. Everything else still answers in the firmware - os8088 "
            "uses the firmware's disk, screen and clock services as they "
            "are."},
    {"id": "dos", "name": "A DOS program", "fact": "Under a DOS program",
     "mark": "returns at once",
     "hint": "while a DOS program runs full-screen under os8088",
     "cap": "A DOS program needs DOS's block, 20h to 3Fh, and expects to "
            "find something real wherever a real DOS puts something - so "
            "os8088's DOS fills each of those slots, if only with a handler "
            "that returns at once, and puts the whole table back when the "
            "program ends."},
]


BY = {"cpu": "the processor itself", "irq": "a hardware line",
      "bios": "an INT instruction", "hook": "the firmware's own code",
      "ptr": "never run - it is read as data",
      "dos": "an INT instruction", "drv": "an INT instruction",
      "user": "an INT instruction", "basic": "an INT instruction",
      "free": "nothing assigns it"}


def vec_defaults(n):
    if n <= 0x07:
        k = "cpu"
    elif n <= 0x0F:
        k = "irq"
    elif n <= 0x1A:
        k = "bios"
    elif n in (0x1B, 0x1C):
        k = "hook"
    elif n <= 0x1F:
        k = "ptr"
    elif n <= 0x3F:
        k = "dos"
    elif 0x70 <= n <= 0x77:
        k = "irq"
    elif 0x60 <= n <= 0x67:
        k = "user"
    elif 0x80 <= n <= 0xF0:
        k = "basic"
    elif n >= 0xF1:
        k = "user"
    else:
        k = "free"
    boot = "rom" if n <= 0x1E else "none"
    return {
        "n": n, "kind": k, "name": KINDS[k].capitalize(), "tag": "",
        "by": BY[k],
        "irq": None, "text": "", "os": "", "dos": "",
        "own": {"boot": boot, "desk": boot, "dos": boot},
        "quiet": {}, "anchors": [],
    }


VEC = dict((n, vec_defaults(n)) for n in range(256))


def V(n, name, tag, **kw):
    v = VEC[n]
    v["name"], v["tag"] = name, tag
    if "kind" in kw and "by" not in kw:
        kw["by"] = BY[kw["kind"]]
    own = kw.pop("own", None)
    if own:
        v["own"].update(own)
    quiet = kw.pop("quiet", None)
    if quiet:
        v["quiet"].update(dict((k, 1) for k in quiet))
    v.update(kw)
    return v


# The IBM PC's own firmware points 00h-1Fh at itself at power-on. The ones
# with nothing to do get its stand-in handler, which notes that the
# interrupt happened (and, for a hardware line, shuts the line off) and
# returns. Those are `quiet` at power-on and stay so wherever nothing
# replaces them.
FW_QUIET = [0x00, 0x01, 0x03, 0x04, 0x06, 0x07, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F,
            0x1B, 0x1C]

# --- raised by the processor ---------------------------------------------------
V(0x00, "Divide error", "Divide", text="""
The processor raises this on itself when a `DIV` or `IDIV` cannot produce an
answer - dividing by zero, or a quotient too big for the register it has to
land in. There is no chip and no instruction asking for it: this is an
[[exception]].

On the 8088 the saved return address points at the instruction AFTER the
divide, so a handler cannot simply retry it. Later processors point at the
divide itself.
""", dos="""
A DOS program's divide error lands here too, and DOS does not install a
handler of its own: the firmware's stand-in notes it and returns, and the
program carries on with whatever was left in the registers.
""")
V(0x01, "Single step", "Step", text="""
When the trap bit in the processor's flags is set, the processor raises
interrupt 1 after every instruction. That is how a debugger walks a program
one instruction at a time. Taking any interrupt clears the bit, so the
debugger's own handler is not single-stepped.
""")
V(0x02, "Non-maskable interrupt", "NMI", by="a hardware pin of its own",
  text="""
The processor has a second interrupt input that the [[interrupt flag]]
cannot turn off. On an IBM PC it is wired to the memory parity checker - a
bad bit in RAM - and, on the XT, to the 8087 maths coprocessor's error
output. The firmware's handler prints PARITY CHECK and stops the machine.

It is not entirely unmaskable: the PC has its own switch for it, a bit in
[[port]] A0h (on an AT, the top bit of port 70h), which the firmware turns
on at power-on.
""", os="""
os8088 never hooks it. On an AT-class machine, port A0h belongs to the
second [[interrupt controller]] - so os8088 only ever touches that port once
it knows the machine has one, because on a PC or XT the same port is the
NMI switch.
""", anchors=[A("kernel/mouse.inc", r"0xA1|0A1h")])
V(0x03, "Breakpoint", "Brk", by="the one-byte INT 3 instruction", text="""
`INT 3` has an encoding of its own one byte long (`CCh`), so a debugger can
overwrite the first byte of any instruction with it without disturbing the
next. When execution reaches it, the debugger's handler gets control.
""")
V(0x04, "Overflow", "INTO", by="the INTO instruction", text="""
`INTO` raises interrupt 4 if the overflow flag is set and does nothing
otherwise - a cheap check after signed arithmetic. Almost nothing uses it.
""")
V(0x05, "Print Screen", "PrtSc", kind="bios",
  by="the keyboard handler, on Shift+PrtSc", text="""
IBM's firmware gives this number to Print Screen: the keyboard handler calls
it when you press Shift+PrtSc, and it copies the text screen to the printer.
The status lives at 0000:0500, which is why that byte is spoken for.

It is the first of IBM's collisions with Intel. Intel had reserved 0 to 31
for the processor, and from the 186 on, interrupt 5 is also what the `BOUND`
instruction raises when an index is out of range.
""")
V(0x06, "Invalid opcode (286+)", "Opcode", text="""
On a 286 or later, the processor raises 6 when it meets a byte that is not
an instruction. The 8088 never does: it treats an undefined byte as
something, often a copy of a neighbouring instruction, and carries on. So
on the 8088 this vector is spare, and the firmware points it at its
stand-in.
""", os="""
Testing for a feature by trying it is one of the few places this vector is
useful, and os8088's extended-memory driver does exactly that: it points 6
at a handler of its own for the length of one probe, runs an instruction
only some 286s have, and puts the vector back. If the instruction is
missing, the handler gets control instead of the machine hanging.
""", anchors=[A("apps/os88la.inc", r"^la_ud\s*:")])
V(0x07, "No coprocessor (286+)", "NoFPU", text="""
On a 286, raised when a maths-coprocessor instruction runs with no
coprocessor present (so software can emulate it). Spare on the 8088.
""")

# --- the first controller -------------------------------------------------------
V(0x08, "Timer", "Timer", irq=0, text="""
The [[timer chip]] raises IRQ 0 18.2 times a second - one [[tick]] - and the
firmware's handler counts them in a 32-bit number at 0040:006C: the time of
day, in ticks since midnight. The same handler counts down the floppy motor
timer, turns the motor off when it reaches zero, and calls {{1C}} so a
program can add work to every tick without replacing the handler.

This is also the second of IBM's collisions with Intel. IBM put the first
controller's eight lines at 08h-0Fh, inside the range Intel reserved; on a
286, 08h is also "double fault" and 0Dh is "general protection fault". In
the 8088's own mode nothing ever raises those, so it did not matter - until
a processor that can.
""", os="""
This is os8088's heartbeat. On every tick its handler first [[chain|chains]]
to the firmware's, so the clock and the floppy motor are still looked after
and the firmware still sends the [[end of interrupt]], and then it does the
operating system's work: wakes anything waiting for that tick, feeds the
sound, and - because this is pre-emptive multitasking - may perform a
[[task switch]] before returning, so the [[IRET]] at the end resumes a
different program from the one that was interrupted.

The firmware's handler is called on a small [[stack]] of its own, not the
interrupted program's: a task's stack is sized for the task, and the
firmware's appetite is not something os8088 controls.

The rate is the firmware's, 18.2 a second, so the time of day stays exact.
When a program asks for a faster clock - a game that wants to update its
physics more often, say - os8088 reprograms the timer and still calls the
firmware's handler only as often as the original rate would have.

During boot, before the operating system proper is running, the same vector
belongs for a few seconds to the loading screen, which turns its logo on the
tick while the firmware reads the disk.
""", dos="""
Still os8088's. A DOS program can install its own timer handler, and many
do; when the program ends, os8088's DOS puts the timer chip back to its
standard rate and the table back as it was.
""", own={"desk": "os", "dos": "os"},
  anchors=[A("kernel/sched.inc", r"^sch_isr\s*:"),
           A("kernel/sched.inc", r"call far \[[^\]]*sch_old08"),
           A("kernel/sched.inc", r"^sch_chstack\s*:"),
           A("kernel/sched.inc", r"^sch_rhook\s*:"),
           A("kernel/splash.inc", r"^spl_isr\s*:"),
           A("apps/dos/dos.asm", r"^dos_restore_machine\s*:")])
V(0x09, "Keyboard", "Keybd", irq=1, text="""
Every key going down or coming up raises IRQ 1. The firmware's handler
reads the [[scan code]] from [[port]] 60h, tells the keyboard interface it
has it, and turns it into a character using the shift state it keeps at
0040:0017. The result goes into a 16-key ring buffer at 0040:001E, with
its read and write positions at 0040:001A and 0040:001C. {{16}} reads from
that buffer; nothing else has to know about the keyboard at all.

Ctrl+Alt+Del is also caught here: the handler writes 1234h at 0040:0072, so
the restart skips the memory test, and jumps to the firmware's start.
""", os="""
os8088 hooks it and keeps the firmware's handler. Its own part is short:
glance at the scan code to keep its own key-state map and to wake the
desktop, and guard against the buffer overflowing - then [[chain]] to the
firmware's handler, which does the translating.

On machines whose firmware offers it, os8088 takes the gentler route: the
firmware's keyboard handler calls {{15}} with every scan code before it
does anything with it, and os8088 listens there instead of replacing this
vector at all.
""", own={"desk": "os", "dos": "os"},
  anchors=[A("kernel/mouse.inc", r"^kbm_isr\s*:"),
           A("kernel/mouse.inc", r"kbm_old9"),
           A("kernel/mouse.inc", r"^kbm_i15\s*:")])
V(0x0A, "IRQ 2", "IRQ2", irq=2, text="""
Free on a PC or XT, for an expansion card that wants it. On an AT this
input is where the second controller is wired in, so it never fires by
itself; the old IRQ 2 pin on the expansion bus is rerouted to IRQ 9 and the
AT firmware forwards that here, so old cards keep working.
""")
V(0x0B, "Serial port 2", "COM2", irq=3, text="""
IRQ 3 belongs to the second serial port (and the fourth, which shares it).
The firmware's own serial service, {{14}}, never uses it: it asks the port
over and over instead - [[polling]] - so at power-on this vector holds only
the stand-in handler.
""", os="""
The serial mouse. os8088 installs one handler on both serial lines, 3 and
4, because the line that fired does not say which port fired it: the
handler reads the status of every serial port it is using, takes every byte
waiting, and decodes mouse packets as they complete. It does not chain -
nothing was there to chain to - and sends its own [[end of interrupt]].

It runs on a private [[stack]] of its own, because it can arrive in the
middle of anything, including another program's deepest call.
""", own={"desk": "os", "dos": "os"},
  anchors=[A("kernel/mouse.inc", r"^mou_isr3\s*:"),
           A("kernel/mouse.inc", r"mou_ivecs"),
           A("kernel/mouse.inc", r"mou_pstack")])
V(0x0C, "Serial port 1", "COM1", irq=4, text="""
IRQ 4: the first serial port (and the third). As with {{0B|IRQ 3}}, the
firmware never uses it.
""", os="""
The same mouse handler as {{0B|IRQ 3}}'s, installed on both lines because a
mouse can be on either port. If a full-screen program takes the line for
itself, os8088 notices when it next asks for the mouse - the vector no
longer points into os8088 - and takes it back.
""", own={"desk": "os", "dos": "os"},
  anchors=[A("kernel/mouse.inc", r"^mou_isr\s*:")])
V(0x0D, "IRQ 5", "IRQ5", irq=5, text="""
On an XT, the hard-disk controller's line; on an AT, the second printer
port's. Sound cards made it their usual choice once neither of those was
common.
""", os="""
Only if a sound card is set to it. os8088's sound driver finds the card's
line by trying them - pointing 2, 3, 5 and 7 at small catch handlers,
asking the card to interrupt, and seeing which one runs - and puts all four
back. It refuses line 5 on an XT whose vector already points into an
[[option ROM]], because that means a hard-disk controller owns it.
""", anchors=[A("drivers/sound/sb.inc", r"^sbl_hook\s*:"),
              A("drivers/sound/sb.inc", r"^sbl_isr\s*:")])
V(0x0E, "Floppy controller", "Floppy", irq=6, text="""
The floppy controller raises IRQ 6 when a command finishes: a seek has
reached its track, a sector has been read. The firmware's handler does
almost nothing - it sets the top bit of 0040:003E and sends the [[end of
interrupt]] - because the real work happens in {{13}}, which waits for that
bit. The data itself never passes through the processor: [[DMA]] has
already put it in memory by the time the line rises.
""", os="""
os8088 leaves it to the firmware and reads and writes every floppy through
{{13}}. Because the firmware's disk service spends most of its time waiting
for this line with interrupts turned on, the timer keeps ticking through
every disk read - which is how the mouse pointer keeps moving, and the
loading screen keeps animating, while the drive works.
""", own={"desk": "rom", "dos": "rom"},
  anchors=[A("kernel/disk.inc", r"^dsk_xfer\s*:")])
V(0x0F, "Printer port", "LPT1", irq=7, text="""
IRQ 7, the first printer port. The firmware's printer service polls instead,
so it is usually spare. It is also where a "phantom" interrupt shows up: if
a line drops again before the processor acknowledges it, the controller
answers with IRQ 7 anyway, and a careful handler checks whether it was
real before acting on it.
""", os="""
Only a sound card set to line 7 uses it on os8088, and that handler checks
for exactly the phantom case: an interrupt the card did not raise gets an
[[end of interrupt]] and nothing else.
""", anchors=[A("drivers/sound/sb.inc", r"spurious")])

# --- firmware services ----------------------------------------------------------
V(0x10, "Video services", "Video", text="""
The firmware's screen service: set a display mode, place the cursor, write
a character, read the font. A program loads `AH` with a function number and
executes `INT 10h`. It works on every display card because each card's
firmware supplies its own (an EGA or VGA has an [[option ROM]] that takes
this vector over).

It is correct and slow: drawing a character in a graphics mode through it
costs tens of milliseconds on a 4.77 MHz machine.
""", os="""
os8088 uses it for what only the firmware can do portably - choosing the
display mode at boot, asking an EGA or VGA what it is, borrowing the
firmware's font - and then draws everything itself, straight into display
memory. Hercules cards have no graphics service here at all, so os8088
programs those directly.
""", dos="""
Full-screen, a DOS program uses the firmware's own service. In a window,
os8088 puts a stand-in here that draws the program's text into its window;
a program that asks for a graphics mode is moved to full screen instead.
""", anchors=[A("kernel/viddet.inc", r"^vid_setmode\s*:"),
              A("apps/dos/dosw.inc", r"^dvw_int10\s*:")])
V(0x11, "Equipment list", "Equip", text="""
Returns the word the firmware worked out at power-on and keeps at
0040:0010: how many floppy drives, which display, how many serial and
printer ports, whether a coprocessor is fitted.
""", os="""
Read at boot to decide between a colour and a monochrome text screen, and
to count floppy drives.
""", anchors=[A("boot/boot2.asm", r"int\s+(11h|0x11)")])
V(0x12, "Memory size", "Mem", text="""
Returns how much memory is fitted below 640 KB, in kilobytes - the number at
0040:0013. Everything that sizes itself to the machine starts here.
""", os="""
The very first thing os8088's boot sector asks, before it does anything: it
moves itself to the top of whatever memory there is, and refuses a machine
too small to run on. The kernel later takes the same number as the ceiling
of the memory it hands out.
""", dos="""
os8088's DOS rewrites 0040:0013 to the top of the memory it gave the
program, so a program that asks gets an answer that agrees with the rest of
what DOS told it - and puts the real value back afterwards.
""", anchors=[A("boot/boot.asm", r"int\s+(12h|0x12)"),
              A("kernel/memory.inc", r"int\s+12h")])
V(0x13, "Disk services", "Disk", text="""
The firmware's disk service: reset the controller, read sectors, write
sectors, format a track, describe the drive. A call names a drive, a
cylinder, a head, a sector and a count, and the firmware drives the
controller, waits for {{0E|IRQ 6}}, and returns a status in `AH`.

Two rules come with it. A transfer must not cross a 64 KB boundary in
physical memory, because the [[DMA]] chip cannot carry across one - the
call fails with status 09h. And a floppy is allowed to fail the first time
(the motor is still coming up to speed), so every caller resets and tries
again.
""", os="""
Every floppy read and write os8088 makes - the boot sector's, the loader's,
the operating system's - goes through here. os8088 cuts each run at the
64 KB boundary and at the end of the track itself, tries three times with a
reset between, and drops to one sector at a time before it gives up. A disk
that is write-protected is not retried.

Chapter 2 of this guide follows that path through the low memory it
uses.
""", dos="""
Full-screen, a DOS program reaches the firmware's service directly. In a
window, it is refused, because a program writing raw sectors under a disk
os8088 has open would corrupt it.
""", anchors=[A("kernel/disk.inc", r"^dsk_runcap\s*:"),
              A("boot/boot.asm", r"int\s+(13h|0x13)"),
              A("apps/dos/dosw.inc", r"^dvw_int13\s*:")])
V(0x14, "Serial services", "Serial", text="""
The firmware's serial-port service: set the speed, send a byte, receive a
byte. It polls - it waits on the port's status rather than using the port's
interrupt line - which is why serial software that cannot afford to miss a
byte talks to the port directly.
""", os="""
Not used. os8088 drives the serial ports itself, by interrupt.
""", anchors=[A("kernel/serial.inc", r"^srl_irq\s*:")])
V(0x15, "System services", "System", text="""
On the original PC, the cassette-tape interface. On the AT and later it
became a grab-bag: the size of memory above 1 MB, copying memory up
there, "what machine is this" - and two hooks the firmware calls on
itself: the keyboard handler calls function 4Fh with each scan code before
using it, and the disk service calls 90h while it waits.
""", os="""
os8088 asks it for the size of memory above 1 MB, and whether the keyboard
handler offers the scan-code hook. Where it does, os8088 installs a handler
here that looks only at function 4Fh and passes every other call straight
on to the firmware - which lets it watch the keyboard without replacing
{{09}}.
""", own={"desk": "rom", "dos": "rom"},
  anchors=[A("kernel/mouse.inc", r"^kbm_i15\s*:"),
           A("kernel/xmem.inc", r"int\s+(15h|0x15)")])
V(0x16, "Keyboard services", "Keys", text="""
Reads the keyboard buffer that {{09}} fills: is there a key, what is it,
which shift keys are down. A program that only wants characters never has
to touch the keyboard hardware.
""", os="""
Used by os8088 where it wants a character rather than a key - and in a DOS
window, replaced by a stand-in fed from the window's own small queue of
keys, so a program waiting for a key sleeps instead of spinning.
""", anchors=[A("apps/dos/dosw.inc", r"^dvw_int16\s*:")])
V(0x17, "Printer services", "Print", text="""
Send a byte to a printer port, or ask its status. Polled, like the serial
service.
""")
V(0x18, "ROM BASIC / no boot disk", "BASIC", text="""
On an IBM PC this starts the BASIC interpreter built into the ROM - which
is what the PC does when there is no disk to boot from. Clones without
IBM's BASIC put a "no boot disk" message here instead.
""", os="""
os8088 can itself be installed as an [[option ROM]], and then points this
vector at its own entry, so a machine with nothing to boot from starts
os8088.
""", anchors=[A("boot/osrom.asm", r"18h|0x18")])
V(0x19, "Bootstrap", "Boot", text="""
Reads the first sector of the boot disk into 0000:7C00 and jumps to it -
the last thing the firmware does at power-on. Calling it again is a quick
way to restart without the memory test.
""", os="""
os8088's Restart: put back everything it hooked, park the drives, and call
this. It is also the way out of a failed resume from hibernation.
""", anchors=[A("kernel/ui.inc", r"int\s+(19h|0x19)")])
V(0x1A, "Time of day", "Clock", text="""
Reads or sets the [[tick]] count at 0040:006C. On an AT, also reads and sets
the battery-backed clock chip, which a PC or XT does not have.
""", os="""
The last of several ways os8088 tries to read the date and time: it talks
to a clock chip directly where it finds one, and asks the firmware only
when that fails.
""", anchors=[A("kernel/clock.inc", r"int\s+1ah")])

V(0x1B, "Ctrl+Break", "Break", text="""
The keyboard handler calls this when Ctrl+Break is pressed. The firmware
leaves it pointing at a bare return; DOS points it at code that sets a
"break pressed" flag, which DOS checks later.
""")
V(0x1C, "User timer tick", "TickHk", text="""
Called by the firmware's timer handler on every [[tick]], after its own
work. The firmware leaves it pointing at a bare return, so a program can
add something to every tick by hooking this one vector - and, done
properly, [[chain|chaining]] to whatever was there before.
""", os="""
os8088 has its own timer handler on {{08}} and leaves this one alone, so
it still points wherever the firmware - or a DOS program - left it.
""")
V(0x1D, "Video parameters", "VidTab", text="""
Not code. The address of a table of display-controller settings, one row
per text and graphics mode, which the video service reads when it sets a
mode. A program can point it at a table of its own to change those modes.
""")
V(0x1E, "Diskette parameters", "FlpTab", text="""
Not code. The address of an 11-byte table of floppy timings - how long the
motor takes to spin up, how long the head takes to settle - and one number
that is not a timing at all: the last sector on a track. The firmware's
disk service reads it on every call.
""", os="""
os8088 points it at a copy of its own. It takes the firmware's 11 bytes and
changes only the last-sector number, so that a single request can run to
the end of a track and on round the other side of the disk - which turns
two disk calls into one. During boot the copy sits at 0000:0580; once the
operating system is running, it lives inside the kernel.
""", own={"desk": "os", "dos": "os"},
  anchors=[A("kernel/disk.inc", r"^dsk_dpt_init_x\s*:"),
           A("boot/boot.asm", r"DPT_AT\s+equ\s+0x0580")])
V(0x1F, "Graphics characters", "ChrTab", text="""
Not code. The address of a font for character codes 128-255 in CGA graphics
modes - the firmware's own font stops at 127. Zero until a program
supplies one.
""")

# --- DOS -------------------------------------------------------------------------
V(0x20, "Terminate program", "Exit", text="""
DOS's oldest way to end a program, carried over from CP/M: the first two
bytes of every program's [[PSP]] are `INT 20h`, so a program can end by
jumping to the start of its own header.
""", os="""
Nothing: os8088 is not DOS, and its own programs reach the system by far
calls rather than interrupts. Vectors 20h to 3Fh stay at zero on the
desktop.
""", dos="""
Ends the program with exit code 0, the same exit {{21}}'s function 4Ch
takes.
""", own={"dos": "dos"}, anchors=[A("apps/dos/dos.asm", r"^dos_int20\s*:")])
V(0x21, "DOS function call", "DOS", text="""
The one that matters. Load `AH` with a function number and execute
`INT 21h`: about a hundred services - read the keyboard, write the screen,
open, read, write and close files, find files, allocate memory, run another
program, exit. Every DOS program is, in the end, a series of these.
""", os="""
Zero on the desktop. os8088's own programs call the system through a table
of far addresses instead, which costs less than an interrupt and needs no
number to be agreed.
""", dos="""
os8088's DOS answers it. Its handler moves to a stack of its own before it
saves anything, then dispatches on `AH` to:

- console input and output, 01h to 0Ch (not 0Ah, buffered line input);
- the old file-control-block file calls, and the handle calls that replaced
them - create, open, close, read, write, seek, delete, rename, attributes;
- directories, the current drive, free space, finding files;
- memory, over a real chain of DOS memory blocks;
- running a child program (one level deep), exit codes, the program's
  header address;
- setting and reading vectors (25h and 35h), which write the real table
  directly.

A function it does not have fails cleanly with error 1, and the window
names the function, so a program that stops working says why.
""", own={"dos": "dos"},
  anchors=[A("apps/dos/dos.asm", r"^dos_int21\s*:"),
           A("apps/dos/dos.asm", r"^dos_hook_vectors\s*:")])
V(0x22, "Terminate address", "ExitTo", text="""
Not called, but read: the address DOS returns to when the program ends,
copied into the program's [[PSP]] when it starts.
""", dos="Reaching it counts as the program exiting with code 0.",
  own={"dos": "dos"})
V(0x23, "Ctrl+C handler", "Ctrl-C", text="""
DOS calls this when it notices Ctrl+C or Ctrl+Break. DOS's own default ends
the program; a program that wants to survive Ctrl+C installs its own.
""", dos="""
A handler that returns at once: pressing Ctrl+C does not kill the program.
""", own={"dos": "dos"}, quiet=["dos"])
V(0x24, "Critical error", "CritEr", text="""
DOS calls this when a device fails - a floppy with no disk in it, a
write-protected disk - and the answer decides what happens next. DOS's own
default asks the user: "Abort, Retry, Fail?".
""", dos="""
Always answers "fail": the call that hit the dead drive returns an error to
the program, and nobody is asked anything.
""", own={"dos": "dos"}, anchors=[A("apps/dos/dos.asm", r"^dos_int24\s*:")])
V(0x25, "Absolute disk read", "AbsRd", text="""
Read sectors by number from a DOS drive, under the file system. Unusually,
it returns with the flags it saved still on the [[stack]], and the caller
removes them - so a handler that ends with a plain [[IRET]] leaves the
caller's stack two bytes wrong.
""", dos="""
Refused - with the flags left on the stack, exactly as DOS does it. Raw
sector access underneath a disk os8088 has open has no safe answer.
""", own={"dos": "dos"}, anchors=[A("apps/dos/dos.asm", r"^dos_int25\s*:")])
V(0x26, "Absolute disk write", "AbsWr", text="""
The writing half of {{25}}, with the same odd return.
""", dos="Refused, the same way as {{25}}.", own={"dos": "dos"})
V(0x27, "Terminate and stay resident", "TSR", text="""
End the program but keep part of it in memory - how pop-up utilities and
drivers installed themselves under DOS.
""", dos="""
Treated as an ordinary exit. Nothing can stay behind after a program run
under os8088 ends.
""", own={"dos": "dos"})
V(0x28, "DOS idle", "Idle", text="""
DOS calls this while it waits for a key, as a hint that background work
would be safe now. Resident utilities hooked it.
""", own={"dos": "dos"}, quiet=["dos"])
V(0x29, "Fast console output", "FastCh", text="""
Write one character to the screen, as quickly as possible; DOS's own
console driver writes through it.
""", dos="""
Goes to the same place as {{21}}'s function 02h, keeping every register.
""", own={"dos": "dos"}, anchors=[A("apps/dos/dos.asm", r"^dos_int29\s*:")])
for _n in range(0x2A, 0x2F):
    V(_n, {0x2A: "Network / critical section", 0x2E: "Command back door"}
      .get(_n, "DOS reserved"),
      {0x2A: "Net", 0x2E: "Cmd"}.get(_n, "Rsvd"),
      text={0x2A: """
A network hook: DOS calls it around work that must not be interrupted, and
programs call it to ask whether a network is running.
""", 0x2E: """
An undocumented way into COMMAND.COM: pass it a command line and it runs
it.
"""}.get(_n, "Reserved by DOS."),
      dos="""
Points at a handler that returns at once - which is what a real DOS has
here too. Left at zero, calling it would be a jump to address zero.
""", own={"dos": "dos"}, quiet=["dos"])
V(0x2F, "Multiplex", "Mplex", text="""
A shared doorway for resident software: a program puts an identifying
number in `AH` and calls 2Fh, and whichever resident piece owns that
number answers. It is how a program finds out whether a print spooler, a
network or an extended-memory manager is present.
""", dos="""
Answers only the extended-memory questions - "is there an XMS manager?"
and "where is it?" - and says "not installed" to everything else.
""", own={"dos": "dos"})
V(0x30, "CP/M entry (2 slots)", "CP/M", text="""
Not a usable vector. DOS writes a five-byte far jump across the slots for
30h and 31h, as a doorway for programs translated from CP/M.
""")
V(0x31, "CP/M entry (cont.)", "CP/M", text="The second half of {{30}}'s five bytes.")
V(0x32, "DOS reserved", "Rsvd", text="Reserved by DOS.",
  dos="A handler that returns at once, as DOS has.",
  own={"dos": "dos"}, quiet=["dos"])
V(0x33, "Mouse driver", "Mouse", kind="drv", by="an INT instruction", text="""
Not DOS itself: the mouse driver, loaded separately, answers here. Function
0 resets it and says whether a mouse is fitted; others report the position
and buttons, set the pointer's shape, or install a routine to be called
whenever the mouse moves.

Because a program decides whether there IS a driver by checking this slot
for zero, a stale address left here is worse than none.
""", os="""
os8088's mouse is its own and needs no driver here. The one exception is a
USB mouse on a PicoMEM card, whose driver answers here and passes on what
it does not handle.
""", dos="""
os8088's DOS answers it - not with a mouse driver, but with a translation
of the position os8088 already keeps, scaled to the 640-by-200 screen DOS
mouse software expects. A program that installs a "call me when the mouse
moves" routine has it called from os8088's timer tick.
""", own={"dos": "dos"},
  anchors=[A("apps/dos/dos.asm", r"^dos_m33_tick\s*:"),
           A("drivers/usbmouse/usbmouse.asm", r"^um_pm33\s*:")])
for _n in range(0x34, 0x3F):
    V(_n, "Maths emulator (%d of 11)" % (_n - 0x33), "FPU-em", text="""
The eleven slots 34h-3Eh belong to the maths-coprocessor emulator that
compilers of the 1980s built into their programs: on a machine with no
8087, each coprocessor instruction is replaced by an interrupt here.
""", dos="""
A handler that returns at once, exactly as a real DOS leaves them - because
compiled programs read these slots before installing their own, and a zero
would read as a broken machine.
""", own={"dos": "dos"}, quiet=["dos"])
V(0x3F, "Overlay manager", "Ovl", text="""
Used by programs built with an overlay linker, to load a piece of the
program from disk on first use.
""")

# --- the rest of the table ---------------------------------------------------------
V(0x40, "Floppy services (moved)", "Flp13", kind="bios", text="""
On an XT with a hard disk, the hard-disk [[option ROM]] takes over {{13}}
and moves the firmware's floppy service here, so it can pass floppy
requests on.
""")
V(0x41, "Hard disk 0 parameters", "HD0", kind="ptr",
  by="never run - it is read as data", text="""
Not code: the address of a table describing the first hard disk's
geometry.
""")
V(0x42, "Video services (moved)", "Vid10", kind="bios", text="""
An EGA or VGA's [[option ROM]] moves the motherboard's {{10}} here before
taking 10h for itself.
""")
V(0x43, "EGA/VGA font", "Font", kind="ptr", by="never run - it is read as data",
  text="Not code: the address of the EGA or VGA font for graphics modes.")
V(0x46, "Hard disk 1 parameters", "HD1", kind="ptr",
  by="never run - it is read as data", text="""
Not code: the second hard disk's geometry table, on an AT.
""")
V(0x4A, "Alarm (AT)", "Alarm", kind="hook", by="the firmware's own code",
  text="""
On an AT, the clock chip's alarm calls this, so a program can be woken at a
set time.
""")
V(0x5C, "NETBIOS", "NetBIOS", kind="drv", text="""
The network service of the 1980s IBM PC network: present only when a
network adapter's software is loaded.
""")
for _n in range(0x60, 0x68):
    V(_n, "For programs" if _n != 0x67 else "Expanded memory",
      "User" if _n != 0x67 else "EMS",
      kind="user" if _n != 0x67 else "drv",
      text="""
IBM set 60h-67h aside for programs to use however they liked. Software that
needed a doorway of its own - a network packet driver, for instance -
looks for an empty one in this range rather than assume a number.
""" if _n != 0x67 else """
The expanded-memory manager's service: bank-switched memory above the
640 KB line on machines with an EMS board. It is the one slot from 60h-67h
everyone agreed on.
""", dos="""
When networking is enabled, os8088's DOS puts a packet driver - the
standard way DOS network programs reach a card - in the first empty slot
from 60h up, rather than choosing a number.
""" if _n == 0x60 else "")
VEC[0x67]["dos"] = """
Not provided: os8088's DOS offers extended memory through {{2F}} instead.
"""
for _n in range(0x70, 0x78):
    V(_n, {0x70: "Clock chip", 0x71: "IRQ 9", 0x72: "IRQ 10", 0x73: "IRQ 11",
           0x74: "PS/2 mouse", 0x75: "Coprocessor", 0x76: "Hard disk",
           0x77: "IRQ 15"}[_n],
      {0x70: "RTC", 0x71: "IRQ9", 0x72: "IRQ10", 0x73: "IRQ11", 0x74: "PS/2",
       0x75: "FPU", 0x76: "IDE", 0x77: "IRQ15"}[_n],
      irq=_n - 0x70 + 8, text={
          0x70: """
An AT has a second [[interrupt controller]], and IBM placed its eight lines
at 70h-77h, clear of Intel's reserved range. Line 8 is the battery-backed
clock chip's periodic and alarm interrupt. None of this second set exists
on a PC or XT - the slots are simply empty.
""",
          0x71: "The AT's rerouted IRQ 2: the firmware forwards it to {{0A}}.",
          0x74: """
Line 12: the PS/2-style mouse port, on machines that have one.
""",
          0x75: """
Line 13: the maths coprocessor's error output. The AT firmware turns it into
a {{02|non-maskable interrupt}}, so software written for the XT's wiring
still finds it.
""",
          0x76: "Line 14: the AT hard-disk controller.",
      }.get(_n, "A spare line on the AT's second controller."))
VEC[0x74]["os"] = """
On a machine with a PS/2 mouse port, os8088 installs its own handler here,
before turning the line on. It does not chain; it sends its [[end of
interrupt]] to the second controller and then to the first - both have to
be told - and it shares the serial mouse's private [[stack]]. A PC or XT has
no line 12, so on those machines the slot stays empty.
"""
VEC[0x74]["anchors"] = [A("kernel/mouse.inc", r"^mou_p2_isr\s*:")]
VEC[0x80]["text"] = VEC[0x86]["text"] = """
IBM's own BASIC claimed 80h-F0h while it ran: 80h-85h reserved, 86h-F0h used
by the interpreter as it went. Nothing else is expected here on a machine
that is not running BASIC.
"""
for _n in range(0x80, 0xF1):
    VEC[_n]["name"] = "BASIC" if _n < 0x86 else "BASIC (while running)"
VEC[0xF1]["text"] = """
F1h-FFh were left unassigned for programs. Nothing standard uses them.
"""

for _n in FW_QUIET:
    for _view in ("boot", "desk", "dos"):
        if VEC[_n]["own"][_view] == "rom":
            VEC[_n]["quiet"][_view] = 1


# -----------------------------------------------------------------------------
# 4. LOW MEMORY. The bar across the top - 0000h to 05FFh, drawn to scale.
# -----------------------------------------------------------------------------

LOW = {
    "top": 0x600,
    "ticks": [(0x000, "0000h"), (0x400, "0400h"), (0x500, "0500h"),
              (0x600, "0600h")],
    "regions": [
        {"id": "ivt", "lo": 0x000, "hi": 0x400, "col": "ivt", "chapter": "int",
         "name": "Interrupt table",
         "text": """
Two-thirds of the first 1.5 KB, and the one structure the processor itself
knows the address of. Entry `n` is the four bytes at `n` × 4: a
16-bit offset first, then a 16-bit segment - the [[segment:offset]] of the
code to run. So {{09}} lives at 0000:0024, and {{21}} at 0000:0084.

It is ordinary memory. Nothing protects it: any program can rewrite any
entry with two `MOV` instructions, and that is exactly how a handler is
installed. The only care needed is to write both halves without an
interrupt arriving in between and finding half an address - so it is done
with interrupts turned off, a [[critical section]].
""",
         "facts": [["Entries", "256 × 4 bytes"], ["Address of n", "0000:(n × 4)"],
                   ["Written by", "anyone", 1], ["Read by", "the processor", 1]]},
        {"id": "bda", "lo": 0x400, "hi": 0x500, "col": "bda", "chapter": "low",
         "name": "BIOS data area",
         "text": """
The firmware's notebook, at 0040:0000: what it found at power-on and what
its handlers keep up to date as the machine runs - the ports fitted, the
equipment list, the memory size, the keyboard's buffer and shift state, the
floppy's motor and status, the display mode, the [[tick]] count. The
handlers on this page read and write it constantly: the timer counts in it,
the keyboard fills it, the disk service leaves its last status in it.

os8088 reads much of it and writes a little: the keyboard buffer when it
overflows, the warm-restart word before a restart, the equipment word's
display bits. **Chapter 2 of this guide walks it field by field** - what
each byte is for, and which of them os8088 relies on.
""",
         "facts": [["Segment", "0040h"], ["Size", "256 bytes"],
                   ["Chapter", "2 · planned", 1]]},
        {"id": "low", "lo": 0x500, "hi": 0x600, "col": "low", "chapter": "low",
         "name": "The 0500h page",
         "text": """
The last 256 bytes before os8088's kernel. IBM gave the first byte to
{{05|Print Screen}}'s status, and DOS and BASIC used a few more; the rest
is spare.

os8088 uses one piece of it, during boot: the boot sector copies the
firmware's floppy timing table to 0000:0580 and points {{1E}} at the copy,
so that it can change one number in it. **Chapter 2** covers why.
""",
         "facts": [["0500h", "Print Screen status", 1],
                   ["0580h", "os8088's boot-time floppy table", 1],
                   ["Chapter", "2 · planned", 1]],
         "anchors": [A("boot/boot.asm", r"DPT_AT\s+equ\s+0x0580"),
                     A("boot/boot2.asm", r"DPT_AT\s+equ\s+0x0580")]},
    ],
}


# -----------------------------------------------------------------------------
# 5. THE WALK. One key press, from the keyboard to the IRET, on an IBM PC as
#    its firmware leaves it - the addresses are the 5150 firmware's own.
# -----------------------------------------------------------------------------

WALK_DEV = {"name": "Keyboard", "port": "port 60h", "irq": 1, "vec": 9,
            "isr": "F000:E987"}
WALK_IVT = [["0020", "FEA5", "08h  IP", 0], ["0022", "F000", "08h  CS", 0],
            ["0024", "E987", "09h  IP", 1], ["0026", "F000", "09h  CS", 1],
            ["0028", "FF23", "0Ah  IP", 0], ["002A", "F000", "0Ah  CS", 0]]
WALK_CODE = [["sti", 0], ["push ax … push ds", 0],
             ["in  al, 60h     ; the scan code", 0],
             ["… acknowledge the keyboard", 1],
             ["… translate, store in buffer", 1],
             ["cli", 0], ["mov al, 20h", 0],
             ["out 20h, al     ; end of interrupt", 0],
             ["pop ds … pop ax", 0], ["iret", 0]]


def stk(top, pushed=False, live=True):
    """Four words from FFFE down; `live` False = popped again, SP back up."""
    rows = [["FFFE", "0000", "program's", 0, (not pushed) or (not live)]]
    if pushed:
        rows += [["FFFC", "F202", "FLAGS", live, 0],
                 ["FFFA", "1234", "CS", live, 0],
                 ["FFF8", "0109", "IP", live, live]]
    else:
        rows += [["FFFC", "—", "", 0, 0], ["FFFA", "—", "", 0, 0],
                 ["FFF8", "—", "", 0, 0]]
    return rows


def cpu(cs="1234", ip="0107", sp="FFFE", fl="F202", iff="1", note="", chg=()):
    return {"cs": cs, "ip": ip, "ss": "1234", "sp": sp, "fl": fl, "iff": iff,
            "note": note, "chg": list(chg)}


IDLE = {"big": "—", "sm": "nothing pressed", "line": 0}
PRESSED = {"big": "1Eh", "sm": "‘A’ went down; its scan code waits "
           "at port 60h", "line": 1}
TAKEN = {"big": "—", "sm": "scan code taken; ‘a’ is in the "
         "buffer", "line": 0}
PIC0 = {"irr": 0, "imr": 0xBC, "isr": 0, "intr": 0}

WALK = [
    {"short": "running", "t": "A program, running",
     "on": ["cpu"], "dev": IDLE, "pic": PIC0,
     "cpu": cpu(note="running a program; interrupts on"),
     "stk": stk(0), "ivt": 0, "code": -1, "h": """
The processor is working through an ordinary program at 1234:0107. Its
[[interrupt flag]] is set, so it will answer a hardware request. The
[[interrupt controller]]'s [[mask]] is as the IBM firmware leaves it,
`BCh`: lines 0, 1 and 6 - timer, keyboard and floppy - let through, the
others shut out.

Nothing on this page is special to the keyboard. Swap the device and the
number and every step is the same for the timer, the floppy or a mouse.
""", "facts": [["Interrupted code", "1234:0107"], ["Mask (IMR)", "BCh"]]},
    {"short": "key down", "t": "A key goes down",
     "on": ["dev"], "dev": PRESSED, "pic": dict(PIC0, irr=0x02),
     "cpu": cpu(note="unaware: still running the program"),
     "stk": stk(0), "ivt": 0, "code": -1, "h": """
The keyboard sends the [[scan code]] for the A key, 1Eh, down its cable.
The PC's keyboard interface collects the bits into a byte, holds it at
[[port]] 60h, and raises its request line - [[IRQ]] 1. The processor knows
nothing yet: the line goes to the controller, not to it.
""", "facts": [["Scan code", "1Eh"], ["Line", "IRQ 1"]]},
    {"short": "controller", "t": "The controller decides",
     "on": ["pic"], "dev": PRESSED,
     "pic": {"irr": 0x02, "imr": 0xBC, "isr": 0, "intr": 1},
     "cpu": cpu(note="INTR has gone high"),
     "stk": stk(0), "ivt": 0, "code": -1, "h": """
The controller records the request in its request register (IRR, bit 1).
It checks two things: is that line's [[mask]] bit clear, and is nothing more
urgent already being served (the in-service register, ISR, is empty)? Both
are true, so it raises its one wire to the processor, INTR.

If a more urgent line - the timer, line 0 - were in service, the keyboard
would simply wait in IRR until it was finished. Lower number, higher
priority.
""", "facts": [["Priority", "line 0 highest, 7 lowest"]]},
    {"short": "finish", "t": "The processor finishes its instruction",
     "on": ["cpu"], "dev": PRESSED,
     "pic": {"irr": 0x02, "imr": 0xBC, "isr": 0, "intr": 1},
     "cpu": cpu(ip="0109", note="looks at INTR only between instructions",
                chg=["CS:IP"]),
     "stk": stk(0), "ivt": 0, "code": -1, "h": """
An instruction is never cut in half. The processor looks at INTR only at
the boundary between two instructions, and only if the [[interrupt flag]]
is set. It finishes the two-byte instruction it was on, so the place to
come back to is 1234:0109.

That boundary rule is also the whole of the latency budget: a long
instruction - a multiply, say - delays the answer by its length.
""", "facts": [["Return to", "1234:0109"]]},
    {"short": "acknowledge", "t": "Acknowledge: which number?",
     "on": ["pic", "cpu"], "dev": PRESSED,
     "pic": {"irr": 0x00, "imr": 0xBC, "isr": 0x02, "intr": 0},
     "cpu": cpu(ip="0109", note="reads 09h off the data bus"),
     "stk": stk(0), "ivt": 0, "code": -1, "h": """
The processor answers with two acknowledge cycles on its bus. On the first,
the controller moves the request from IRR to ISR - line 1 is now "in
service", and stays so until it is told otherwise. On the second it puts a
single byte on the data bus: its base number plus the line, 08h + 1 = 09h.

That base, 08h, was chosen by IBM's firmware when it set the controller up
at power-on. It is the only reason "IRQ 1" and "interrupt 09h" are the same
thing.
""", "facts": [["Base + line", "08h + 1 = 09h"]]},
    {"short": "save", "t": "Save where it was",
     "on": ["cpu", "stk"], "dev": PRESSED,
     "pic": {"irr": 0x00, "imr": 0xBC, "isr": 0x02, "intr": 0},
     "cpu": cpu(ip="0109", sp="FFF8", fl="F002", iff="0",
                note="three words pushed; interrupts off",
                chg=["SS:SP", "FLAGS", "IF"]),
     "stk": stk(0, pushed=True), "ivt": 0, "code": -1, "h": """
Now the processor does, by itself, what every handler depends on. It
pushes its status word (FLAGS) onto the [[stack]], then turns off the
[[interrupt flag]] and the single-step bit, then pushes CS and IP - the
address to come back to. Six bytes, on whatever stack the interrupted
program was using.

That last point matters: a handler runs on the interrupted program's stack
unless it moves to one of its own, so it must not need much. (The 8088
always reads the top four bits of its status word as ones, which is why it
saves F202h rather than 0202h.)
""", "facts": [["Pushed", "FLAGS, CS, IP"], ["Bytes", "6"],
               ["SP", "FFFE → FFF8"]]},
    {"short": "look up", "t": "Look the number up",
     "on": ["ivt", "cpu"], "dev": PRESSED,
     "pic": {"irr": 0x00, "imr": 0xBC, "isr": 0x02, "intr": 0},
     "cpu": cpu(cs="F000", ip="E987", sp="FFF8", fl="F002", iff="0",
                note="jumped through the table", chg=["CS:IP"]),
     "stk": stk(0, pushed=True), "ivt": 1, "code": 0, "h": """
The number times four is the table entry: 09h × 4 = 24h. The processor
reads the word at 0000:0024 into IP and the word at 0000:0026 into CS -
E987h and F000h, the IBM firmware's keyboard handler - and carries on from
there.

From the controller's acknowledge to the handler's first instruction is
around 60 to 80 clock cycles: about 15 microseconds on a 4.77 MHz 8088.
That figure is arithmetic on the processor's published timings rather than
a measurement.
""", "facts": [["Entry", "0000:0024"], ["Handler", "F000:E987"],
               ["Cost to enter", "≈ 15 µs (derived)"]]},
    {"short": "handler", "t": "The handler does its work",
     "on": ["isr", "dev"], "dev": TAKEN,
     "pic": {"irr": 0x00, "imr": 0xBC, "isr": 0x02, "intr": 0},
     "cpu": cpu(cs="F000", ip="E98C", sp="FFF0", fl="F202", iff="1",
                note="handler running; interrupts back on", chg=["IF", "SS:SP"]),
     "stk": stk(0, pushed=True), "ivt": 0, "code": 2, "h": """
The handler saves every register it is going to use - the interrupted
program must find them untouched - and reads the scan code from port 60h.
It tells the keyboard interface it has the byte, which lets the request
line fall, turns 1Eh into "a" with the shift state, and puts it in the
keyboard buffer at 0040:001E.

This one also turned interrupts back on with its first instruction, so the
timer can still get in while it works. The keyboard itself cannot: the
controller still has line 1 in service, which holds back line 1 and
everything less urgent, whatever the [[interrupt flag]] says.
""", "facts": [["Reads", "port 60h"], ["Writes", "0040:001E…"]]},
    {"short": "EOI", "t": "End of interrupt",
     "on": ["isr", "pic"], "dev": TAKEN,
     "pic": {"irr": 0x00, "imr": 0xBC, "isr": 0x00, "intr": 0},
     "cpu": cpu(cs="F000", ip="EA2B", sp="FFF0", fl="F002", iff="0",
                note="tells the controller it is done"),
     "stk": stk(0, pushed=True), "ivt": 0, "code": 7, "h": """
The handler writes 20h to the controller's port 20h - the [[end of
interrupt]]. The controller clears line 1's in-service bit, and the next
key press can be answered.

Forget this and nothing crashes. The keyboard just stops, along with every
line less urgent than it, which is how a missing EOI usually shows up: the
mouse freezes too.
""", "facts": [["Write", "20h → port 20h"]]},
    {"short": "IRET", "t": "Return as if nothing happened",
     "on": ["cpu", "stk"], "dev": TAKEN, "pic": PIC0,
     "cpu": cpu(ip="0109", note="back in the program, one instruction on",
                chg=["CS:IP", "SS:SP", "FLAGS", "IF"]),
     "stk": stk(0, pushed=True, live=False), "ivt": 0, "code": 9, "h": """
The handler restores the registers it saved and ends with [[IRET]], which
pops IP, CS and FLAGS - in that order, the reverse of how they went on. The
status word comes back with the [[interrupt flag]] set, because it was set
when the program was interrupted, and the processor resumes at 1234:0109.

The program cannot tell anything happened, except that a key is now
waiting in the buffer. A software interrupt - `INT 13h`, say - is this same
walk from the "save" step on: no device, no controller, no acknowledge; the
number comes from the instruction itself.
""", "facts": [["Pops", "IP, CS, FLAGS"], ["Leaving costs", "≈ 10 µs (derived)"]]},
]


# -----------------------------------------------------------------------------
# 6. THE RUNGS.
# -----------------------------------------------------------------------------

CHAPTERS = [
    {"id": "int", "n": "Chapter 1", "name": "Interrupts",
     "hint": "the table, the hardware lines, the firmware, DOS - and os8088"},
    {"id": "low", "n": "Chapter 2", "name": "BIOS data & the disk",
     "planned": True,
     "hint": "the BIOS data area, the 0500h page, and the disk path through "
             "them - planned"},
]


def rng(a, b):
    return list(range(a, b + 1))


RUNGS = [
    {"id": "what", "grp": "Basics", "title": "What an interrupt is",
     "sub": "one mechanism, three ways in", "chapter": "int", "intro": """
An interrupt is the processor dropping what it is doing, running a short
piece of code somewhere else, and then carrying on exactly where it left
off. On the 8088 every interrupt has a number from 0 to 255, and every
number has an entry in the [[interrupt table]], the first kilobyte of
memory, saying where its code is.

There are three ways for a number to arrive, and the table does not care
which:

- **A device asks for attention.** A key goes down, a disk sector arrives,
  the clock ticks. The device raises an [[IRQ]] line; the [[interrupt
  controller]] turns that into a number. This is what "interrupt" first
  meant: the device interrupting the processor.
- **A program asks for a service.** The instruction `INT 21h` jumps through
  entry 21h exactly as a device would. The [[BIOS]] and DOS are libraries
  reached this way - the caller needs to know a number, not an address.
- **The processor objects.** Dividing by zero raises an [[exception]],
  number 0, on the processor itself.

Use {{@take|the next rung}} to watch one being taken, step by step. The grid
on the right of this page is the whole table: colour it by what each
number is for, or by whose code would answer it at power-on, on os8088's
desktop, and under a DOS program.
""", "steps": []},
    {"id": "take", "grp": "Basics", "title": "One interrupt, step by step",
     "sub": "a key press on an IBM PC", "chapter": "int", "walk": True,
     "intro": """
From a key going down to the program carrying on: the ten things that happen
in between, with the processor's registers, the controller's three
registers and the [[stack]] as they stand at each one. The addresses are
the original IBM PC firmware's own.

Step through with the arrow keys, or click a step under the diagram.
""", "steps": [{"walk": i} for i in range(len(WALK))]},
    {"id": "table", "grp": "Basics", "title": "Where they live",
     "sub": "the first 1,536 bytes of memory", "chapter": "int",
     "region": "ivt", "intro": """
The bar at the top of the page is the start of memory, drawn to scale. The
[[interrupt table]] fills the first kilobyte; the firmware's own data
follows it; then a page left for DOS; and at 0600h - byte 1,536 - os8088's
kernel begins. Nothing else in the first megabyte is anywhere near as busy.

Each entry is a far address: two bytes of offset, then two of segment.
Writing a new address into an entry is how every handler on this page is
installed, and keeping the old one, so as to pass interrupts on to it, is
how handlers are stacked one on another.
""", "steps": [{"region": "ivt"}, {"region": "bda"}, {"region": "low"}],
     "facts": [["Table", "0000h–3FFh"], ["Firmware data", "0400h–4FFh"],
               ["Spare page", "0500h–5FFh"], ["os8088 kernel", "from 0600h"]]},

    {"id": "cpu", "grp": "The table, by who raises them",
     "title": "Raised by the processor", "ms": "00–07", "kind": "cpu",
     "sub": "faults, traps and the debugger's two", "chapter": "int",
     "intro": """
Intel reserved the first 32 numbers for the processor's own use. The 8088
uses only five of them - 0 to 4 - and IBM, designing the PC around it,
treated the rest as free: it put Print Screen at 5 and the hardware lines
at 8 to 15.

That was harmless on the 8088 and a long-lived nuisance afterwards: on a
286 and later the processor raises 5, 6, 7, 8 and 13 itself, and they share
numbers with things IBM put there. A system that runs those processors in
[[protected mode]] has to move the hardware lines elsewhere.
""", "vecs": rng(0, 7)},
    {"id": "irq", "grp": "The table, by who raises them",
     "title": "Hardware lines", "ms": "08–0F", "kind": "irq",
     "sub": "the first interrupt controller", "chapter": "int", "intro": """
Eight [[IRQ]] lines go into one [[interrupt controller]], and the firmware
sets it up at power-on to answer line `n` with number 08h + `n`. Lower
lines win: the timer beats the keyboard, which beats everything else.

The controller is also a set of switches. Its [[mask]] (written at
[[port]] 21h) shuts lines out one by one, and every handler on a hardware
line must finish by sending the controller an [[end of interrupt]] at port
20h - or have the handler it [[chain|chains]] to send it.

os8088 hooks four of these lines - timer, keyboard, and both serial
ports - and leaves the floppy's to the firmware. Its network cards raise no
interrupt at all: they are [[polling|polled]].
""", "vecs": rng(8, 15)},
    {"id": "irq2", "grp": "The table, by who raises them",
     "title": "The AT's second controller", "ms": "70–77", "kind": "irq",
     "sub": "eight more lines, on an AT and later", "chapter": "int",
     "intro": """
The AT added a second controller, wired into line 2 of the first, and IBM -
having learned - put its eight lines at 70h-77h, away from the processor's
reserved range. A handler on one of these has to send an [[end of
interrupt]] to both controllers, second one first.

On the 8088 PC and XT these eight slots are empty, and the second
controller's [[port|ports]] belong to something else - so os8088 touches
them only once it knows the machine has one.
""", "vecs": rng(0x70, 0x77)},
    {"id": "bios", "grp": "The table, by who raises them",
     "title": "Firmware services", "ms": "10–1A", "kind": "bios",
     "sub": "the BIOS, reached by number", "chapter": "int", "intro": """
The [[BIOS]] is a library in ROM, and these are its doors. A program puts
a function number in `AH`, the details in other registers, and executes
`INT` - and gets an answer back in registers, usually with the carry flag
set for failure. Because the caller only needs the number, the same program
runs on any machine whose firmware answers it.

os8088 uses the firmware's disk service for every floppy access, its video
service to set the display up, and its memory, equipment, clock and restart
services as they are - and draws the screen itself.
""", "vecs": [0x05] + rng(0x10, 0x1A) + [0x40, 0x42]},
    {"id": "hooks", "grp": "The table, by who raises them",
     "title": "Hooks and pointers", "ms": "1B–1F", "kind": "ptr",
     "sub": "entries that are not services", "chapter": "int", "intro": """
Two kinds of entry are not services at all.

Some are **hooks**: the firmware calls them on itself, from inside its own
handlers, so that a program can add to what the firmware does without
replacing it. The timer handler calls {{1C}} on every tick; the keyboard
handler calls {{1B}} on Ctrl+Break. The firmware leaves them pointing at a
bare return.

Others are **pointers to data**. The table is a convenient place to keep a
far address everyone can find, so IBM put the addresses of a few tables
there - display settings, floppy timings, a font, hard-disk geometry - and
nothing ever executes them. Change the pointer and the firmware uses a
different table.
""", "vecs": [0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x41, 0x43, 0x46, 0x4A]},
    {"id": "dos", "grp": "The table, by who raises them",
     "title": "DOS", "ms": "20–3F", "kind": "dos",
     "sub": "the block a DOS owns", "chapter": "int", "view": "dos",
     "intro": """
DOS took 20h to 3Fh. Only a handful are services a program calls - {{21}}
above all, which is nearly all of DOS - and the rest are addresses DOS
reads, hooks DOS calls, or slots it reserved.

**os8088 is not DOS**, and on its desktop this whole block is zero: its own
programs reach the system through a table of far addresses. But it carries
a DOS of its own, a clean reimplementation, for running DOS programs. That
DOS fills the block before a program starts, everywhere a real DOS does -
with real handlers for the services it offers, and a handler that simply
returns for the rest, because a DOS program may call any of them and a zero entry is
not "absent": it is a jump to address zero.
""", "vecs": rng(0x20, 0x3F)},
    {"id": "rest", "grp": "The table, by who raises them",
     "title": "Everything else", "ms": "40–FF", "kind": "user",
     "sub": "drivers, programs, BASIC, and room", "chapter": "int",
     "intro": """
Above 3Fh the table is mostly empty, and what is there was claimed by
convention rather than by design: hard-disk and display [[option ROM|option
ROMs]] park the vectors they replaced at 40h and 42h, IBM left 60h-67h for
programs and F1h-FFh unassigned, and its BASIC used 80h-F0h while it ran.
Drivers that came later - the mouse at 33h, expanded memory at 67h - simply
took a number and became a standard by being first.

Because nothing protects the table, the rule that makes sharing work is a
courtesy: look for a slot that is zero before claiming it, and keep the old
address if it is not.
""", "vecs": [0x33, 0x40, 0x41, 0x42, 0x43, 0x46, 0x4A, 0x5C] + rng(0x60, 0x67)
     + [0x80, 0x86, 0xF1]},

    {"id": "os", "grp": "os8088", "title": "What os8088 takes over",
     "sub": "the few vectors it hooks", "chapter": "int", "view": "desk",
     "home": False, "intro": """
Colour the grid by **os8088 desktop** and the change from power-on is small
on purpose. os8088 writes its own address into five entries:

- {{08|the timer}}, its heartbeat - and the [[task switch]] that is
  pre-emptive multitasking;
- {{09|the keyboard}} (or, on firmware that offers it, the gentler
  {{15|scan-code hook}});
- {{0B|both}} {{0C|serial lines}}, for the mouse;
- {{1E|the floppy timing pointer}}, at a copy with one number changed;

plus {{74|the PS/2 mouse line}} on machines that have one, a sound card's
line when there is a card, and a USB mouse driver's {{33}} on a PicoMEM.
Everything else - disk, screen, clock - is still the firmware's, called
through the table as any program would.

Each install follows the same pattern: remember the old address, write the
new one with interrupts off so the entry is never half-written, and - for
the timer and keyboard - [[chain]] to the old handler so the firmware's own
bookkeeping still happens.
""", "steps": [{"vec": n} for n in (0x08, 0x09, 0x15, 0x0B, 0x0C, 0x74,
                                     0x0D, 0x1E, 0x13, 0x10)],
     "vecs": [0x08, 0x09, 0x15, 0x0B, 0x0C, 0x74, 0x0D, 0x1E, 0x13, 0x10]},
    {"id": "dosrun", "grp": "os8088", "title": "A DOS program's world",
     "sub": "what its DOS installs, and puts back", "chapter": "int",
     "view": "dos", "home": False, "intro": """
A DOS program expects the machine to itself and the table to itself. When
os8088's DOS starts one full screen, it:

- saves the whole table - all 1,024 bytes - and the [[BIOS]] data area;
- fills DOS's block, 20h to 3Fh, everywhere a real DOS fills it, and
  zeroes {{33}} unless it is answering it, so a program checking for a mouse driver never finds a stale address;
- leaves the timer, keyboard and mouse handlers as os8088's, so the system
  underneath keeps time and the mouse keeps moving;
- and lets the program rewrite anything it likes.

When the program ends, the table goes back exactly as it was, the timer
chip goes back to its standard rate, and the controller's [[mask]] is put
back - whatever the program did to them.

**Run in a window**, the program shares the machine with the desktop
instead, so its world is swapped in only while it is the program running:
os8088 gives it its own timer, screen, disk and keyboard entries - stand-ins
that draw into the window, refuse raw disk access, and feed it keys - and,
when it ends, puts back only the entries that still point into the
program's memory, because the desktop may have changed others meanwhile.
""", "steps": [{"vec": n} for n in (0x21, 0x20, 0x24, 0x25, 0x23, 0x2F,
                                     0x33, 0x12, 0x10, 0x13, 0x16)],
     "vecs": rng(0x20, 0x3F) + [0x08, 0x09, 0x10, 0x12, 0x13, 0x16],
     "anchors": [A("apps/dos/dos.asm", r"^dos_save_machine\s*:"),
                 A("apps/dos/dosw.inc", r"^dvw_mend\s*:"),
                 A("apps/dos/dosw.inc", r"^dvw_irq0\s*:")]},
    {"id": "back", "grp": "os8088", "title": "Putting it all back",
     "sub": "shutdown, restart and hibernation", "chapter": "int",
     "view": "desk", "home": False, "intro": """
Every vector os8088 hooks, it gives back. At shutdown each driver is told
to let go of its line; the timer entry is restored and the timer chip put
back to the firmware's own mode, in one [[critical section]]; the mouse and
keyboard entries are restored; and the controller's [[mask]] is left with
only the timer and keyboard open, so the firmware can still see
Ctrl+Alt+Del. Restart does the same and then calls {{19}}.

Hibernation is the exception that proves the rule. To write memory to disk
os8088 needs the firmware's disk service, which needs the firmware's own
timer handler - so it hands the timer back first and shuts the keyboard and
mouse lines. The table itself is saved with the rest of memory, and on
resume it is put back LAST, all 1,024 bytes in one copy with interrupts
off: until that copy is done, no interrupt may arrive and find a table that
is half one boot and half another.

When the system moves a driver in memory - the sound driver, say, to make
room - the table entries that point into it are moved with it: the table is
one of the few places outside a driver that holds its address.
""", "steps": [],
     "anchors": [A("kernel/sched.inc", r"^sched_stop\s*:"),
                 A("kernel/mouse.inc", r"^mouse_unhook\s*:"),
                 A("kernel/hiber.inc", r"^hbm_handover\s*:"),
                 A("kernel/hbstub.inc", r"rep\s+movsw"),
                 A("kernel/memory.inc", r"^mem_rr_ivt\s*:")]},
    {"id": "pm", "grp": "os8088", "title": "When the processor is a 286",
     "sub": "the collision, resolved", "chapter": "int", "home": False,
     "intro": """
On a 286 or later in [[protected mode]], the processor raises 08h to 0Fh
itself - a general protection fault is 0Dh - so the timer and the keyboard
can no longer arrive there without being mistaken for faults.

os8088's protected-mode kernel moves the first controller's lines to 20h-27h
before it switches, by setting the controller up again from scratch, and
builds the protected-mode table from the real one. When it has to hand a
service to the firmware, which knows only the old numbers, it steps back
into the original mode to do it. It is a separate kernel built for those
machines; the PC and XT kernel never touches the controller's numbering.
""", "steps": [],
     "anchors": [A("kernel/mega.inc", r"^mega_pic_pm\s*:"),
                 A("kernel/mega.inc", r"MEGA_PIC_BASE\s+equ\s+0x20")]},

    {"id": "low", "grp": "Chapter 2 · planned",
     "title": "BIOS data and the disk", "sub": "the rest of the first 1.5 KB",
     "chapter": "low", "region": "bda", "intro": """
**This chapter is planned, not yet written.** It will cover the other third
of the bar at the top of the page:

- the [[BIOS]] data area, 0400h-04FFh, field by field - the ports, the
  equipment word, the keyboard buffer, the floppy's motor and status bytes,
  the [[tick]] count and the warm-restart word - and which of them os8088
  reads, writes or puts back;
- the 0500h page, and the floppy timing table os8088 keeps there during
  boot;
- one disk read followed all the way down - from os8088 through {{13}}, the
  timing table at {{1E}}, the floppy controller, [[DMA]] and {{0E|IRQ 6}} -
  with the bytes in low memory each step touches, and why every run is cut
  at the 64 KB boundary and the end of the track.
""", "steps": [{"region": "bda"}, {"region": "low"}]},
]


# -----------------------------------------------------------------------------
# 7. CHECK, then BUILD.
# -----------------------------------------------------------------------------

BANNED = [(re.compile(r"SPEC\b|§|\.inc\b|\.asm\b|\.py\b|os88[a-z]+\b"),
           "a source file, a symbol or a specification section")]


def selfcheck():
    """Raise Stale on the first thing the tree no longer supports."""
    for k, v in GLOSS.items():
        if "[[" in v:
            raise Stale("the definition of `%s` leans on another marked term"
                        % k, "GLOSS", "say it in plain words")
    for n, v in VEC.items():
        for a in v["anchors"]:
            check_anchor(a, "INT %02Xh" % n)
        if v["own"]["desk"] == "os" and not v["anchors"]:
            raise Stale("INT %02Xh says os8088 owns it, with no anchor" % n,
                        "VEC in tools/os88intguide.py",
                        "anchor it to the code that installs it")
        for view in ("desk", "dos"):
            if v["own"][view] not in OWNERS:
                raise Stale("INT %02Xh has owner %r" % (n, v["own"][view]),
                            "VEC", "use one of %s" % ", ".join(OWNERS))
    for g in LOW["regions"]:
        for a in g.get("anchors", []):
            check_anchor(a, "region %s" % g["id"])
    ids = set(r["id"] for r in RUNGS)
    for r in RUNGS:
        for a in r.get("anchors", []):
            check_anchor(a, "rung %s" % r["id"])
        for s in r.get("steps", []):
            if "region" in s and s["region"] not in [g["id"] for g in LOW["regions"]]:
                raise Stale("rung %s names region %s" % (r["id"], s["region"]),
                            "RUNGS", "name a region in LOW")
    texts = [(k, v) for k, v in GLOSS.items()]
    for n, v in VEC.items():
        texts += [("INT %02Xh" % n, v["text"] + v["os"] + v["dos"] + v["name"])]
    for r in RUNGS:
        texts.append(("rung " + r["id"], r["intro"] + r["title"]))
        for m in RUNG.finditer(r["intro"]):
            if m.group(1) not in ids:
                raise Stale("rung %s links to rung %s" % (r["id"], m.group(1)),
                            "RUNGS", "link to a rung that exists")
    for w in WALK:
        texts.append(("walk " + w["t"], w["h"]))
    for g in LOW["regions"]:
        texts.append(("region " + g["id"], g["text"]))
    for who, t in texts:
        for rx, what in BANNED:
            m = rx.search(t)
            if m:
                raise Stale("%s cites %s (`%s`) on the page" % (who, what, m.group(0)),
                            "the prose in tools/os88intguide.py",
                            "the reader has none of those: say what the machine "
                            "does, and put the evidence in an anchor")
    return len(ANCHORS_SEEN)


def bold(s):
    return re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", s)


def model():
    vec = []
    for n in range(256):
        v = VEC[n]
        who = "INT %02Xh" % n
        vec.append({
            "name": v["name"], "tag": v["tag"], "kind": v["kind"],
            "by": v["by"], "irq": v["irq"],
            "html": bold(md(v["text"], who)) if v["text"] else
            "<p>%s. Nothing standard is assigned here.</p>" % esc(KINDS[v["kind"]]),
            "os": bold(md(v["os"], who)), "dos": bold(md(v["dos"], who)),
            "own": v["own"], "mark": v["quiet"],
        })
    rungs = []
    for r in RUNGS:
        vecs = r.get("vecs")
        steps = r.get("steps")
        if steps is None:
            steps = [{"vec": n} for n in vecs]
        rungs.append({
            "id": r["id"], "grp": r["grp"], "title": r["title"],
            "ms": r.get("ms", ""), "kind": r.get("kind"), "sub": r["sub"],
            "chapter": r["chapter"], "intro": bold(md(r["intro"], r["id"])),
            "steps": steps, "vecs": vecs or [s["vec"] for s in steps if "vec" in s],
            "walk": r.get("walk", False), "view": r.get("view"),
            "region": r.get("region"), "facts": r.get("facts"),
            "home": r.get("home", True),
        })
    walk = []
    for w in WALK:
        x = dict(w)
        x["h"] = bold(md(w["h"], "walk"))
        walk.append(x)
    low = {"top": LOW["top"], "ticks": LOW["ticks"], "regions": []}
    for g in LOW["regions"]:
        low["regions"].append({k: g[k] for k in
                               ("id", "lo", "hi", "col", "chapter", "name", "facts")})
        low["regions"][-1]["html"] = bold(md(g["text"], g["id"]))
    views = [dict((k, v) for k, v in w.items()) for w in VIEWS]
    return {"vec": vec, "rungs": rungs, "walk": walk, "low": low,
            "walkDev": WALK_DEV, "walkIvt": WALK_IVT, "walkCode": WALK_CODE,
            "kinds": KINDS, "kindOrder": KIND_ORDER, "owners": OWNERS,
            "ownOrder": OWN_ORDER, "views": views, "chapters": CHAPTERS,
            "gloss": _GLOSS_LC}


FONTS = ('<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?'
         'family=IBM+Plex+Mono:wght@400;500&'
         'family=IBM+Plex+Sans:wght@400;500;600&'
         'family=IBM+Plex+Sans+Condensed:wght@600;700&display=swap">')


def commit():
    try:
        return subprocess.check_output(
            ["git", "-C", ROOT, "log", "-1", "--format=%h %cs"],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""


def render(fragment=False):
    p = model()
    css = open(os.path.join(HERE, "page.css")).read()
    js = open(os.path.join(HERE, "page.js")).read()
    c = commit()
    body = """
<div class="wrap">
 <div class="board">
 <aside class="rail">
   <h1>os8088 <span class="sub">Interrupts</span></h1>
   <p class="lede">How an IBM PC stops what it is doing to answer a key, a
    clock or a disk — the table in the first kilobyte of memory that
    says where every answer is, all 256 of its entries, and which of them
    os8088 and its DOS take over.
    <span class="kbd">&larr;</span><span class="kbd">&rarr;</span> walk it a
    step at a time; <span class="kbd">&uarr;</span><span class="kbd">&darr;</span>
    move a whole rung. Underlined words define on hover.</p>
   <div class="chap" id="chap"></div>
   <div class="stages" id="stages"><span class="nav" id="navbtns">
     <button id="prev" type="button" title="back one step (left arrow)">&larr;</button>
     <button id="next" type="button" title="on one step (right arrow)">&rarr;</button>
   </span></div>
   <p class="where" id="where"></p>
 </aside>
 <main class="work">
 <div class="shead"><h2 class="stitle" id="stitle"></h2>
  <p class="smoved" id="smovedt"></p></div>

 <div class="panel">
  <p class="ph"><span>Memory — the first 1,536 bytes, drawn to scale</span>
   <span class="hint">os8088's kernel starts at 0600h · click a region ·
   hatched regions are chapter 2's</span></p>
  <div class="lbar" id="lbar"></div>
  <div class="lrule" id="lrule"></div>
 </div>

 <div class="panel" id="mechp" hidden>
  <p class="ph"><span>One interrupt, taken</span>
   <span class="hint">a key press on an IBM PC · orange is what this step
   changed</span></p>
  <div class="mech" id="mech"></div>
  <div class="flow" id="flow"></div>
 </div>

 <div class="split">
 <div class="panel detail" id="dpanel">
  <div><h3 id="dtitle"></h3><div id="dbody"></div></div>
  <div class="facts" id="dfacts"></div>
 </div>

 <div class="panel">
  <p class="ph"><span>The interrupt table \u2014 256 entries of 4 bytes, at
   0000:0000</span><span class="hint">row = first hex digit, column =
   second \u00b7 click any entry</span></p>
  <div class="views" id="views"></div>
  <div class="grid" id="grid"></div>
  <div class="legend" id="legend"></div>
  <p class="gcap" id="gcap"></p>
 </div>
 </div>
 </main>
 </div>

 <footer>
  <p><b>Two kinds of fact are on this page.</b> What the IBM PC does —
  the processor, the interrupt controller, the firmware's table and the
  conventions DOS added — has not changed since the 1980s, and is
  described as IBM's own firmware leaves the machine. What os8088 does with
  the table is checked against its source every time the page is made: a
  vector it stops hooking makes the page refuse to build rather than go on
  describing it.</p>
  <p>The two timings in the walk-through are arithmetic on the 8088's
  published instruction timings, not measurements, and are marked so.
  Everything about os8088 describes its desktop on an IBM PC or XT unless it
  says otherwise.</p>
  <p class="stamp">Checked against source <b>%(commit)s</b></p>
 </footer>
</div>
<script>window.IG = %(data)s;</script>
<script>%(js)s</script>
""" % {"commit": esc(c or "(uncommitted tree)"),
       "data": json.dumps(p, separators=(",", ":")).replace("</", "<\\/"),
       "js": js}
    title = "os8088 Interrupts"
    if fragment:
        return "<title>%s</title>\n%s\n<style>%s</style>\n%s" % (
            title, FONTS, css, body)
    return ("<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,"
            "initial-scale=1\"><title>%s</title>%s\n<style>%s</style></head>"
            "<body>%s</body></html>" % (title, FONTS, css, body))


USAGE = """os88intguide - the interactive Interrupts page (ON DEMAND, never in `make`)

  python3 tools/os88intguide.py [-o PATH] [--fragment]
  python3 tools/os88intguide.py --selfcheck

  -o, --out PATH   where to write (default build/interrupts.html)
  --fragment       emit without <html>/<head>/<body>, for a host page
  --selfcheck      check every anchor and the prose rules; write nothing
"""


def main(argv):
    out = os.path.join(ROOT, "build", "interrupts.html")
    fragment = check_only = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("-o", "--out"):
            i += 1
            out = argv[i]
        elif a == "--fragment":
            fragment = True
        elif a == "--selfcheck":
            check_only = True
        elif a in ("-h", "--help"):
            print(USAGE)
            return 0
        else:
            sys.stderr.write(USAGE)
            return 2
        i += 1
    n = selfcheck()
    html = render(fragment)       # renders every mark, so a bad term fails here
    if check_only:
        print("os88intguide: selfcheck ok - %d anchors hold, %d glossary terms, "
              "%d rungs" % (n, len(GLOSS), len(RUNGS)))
        return 0
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "w", encoding="utf-8").write(html)
    print("os88intguide: wrote %s (%s bytes, %d anchors checked)"
          % (out, "{:,}".format(len(html)), n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
