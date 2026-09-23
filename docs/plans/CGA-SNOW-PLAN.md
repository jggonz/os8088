# CGA SNOW IN THE DOS BOX'S FULL SCREEN — the options, priced

**STATUS: NOT STARTED, AND DELIBERATELY SO.** The owner asked for the options
to be recorded and then left: *"Single char is single snow, now. Dir is mega
snow as you predicted. Fully record our options for this, and we'll let it go
for now."* Nothing here is built. What is here is the arithmetic, so that
whoever picks it up is not re-deriving it — and the arithmetic is what makes
three of the six options obviously wrong.

It is **CGA ONLY**. Hercules has no snow and neither has VGA, so every option
below is gated on the adapter and costs the other two nothing.

## 1. What snow is, and what the machine that has it does about it

An IBM CGA's video RAM is single-ported: the CRTC reads it to paint the
screen, and a CPU access that lands outside a blanking interval steals a cycle
the CRTC needed. The missing byte reaches the screen as a bright speck. It is a
property of the 1981 card, not of the software.

**The IBM BIOS does the avoidance, one word at a time.** Measured in the real
5150 ROM (`95c2de42-BIOS_IBM5150_27OCT82`, three sites at `0x138F`, `0x13D8`
and `0x1409`):

```
8B 16 63 00   mov dx, [0063]      ; the CRTC base out of the BIOS data area
83 C2 06      add dx, 6           ; -> 0x3DA, the status register
EC            in al, dx
A8 01         test al, 1          ; bit 0 = DISPLAY ENABLE
75 FB         jnz -5              ; spin while NOT blanking
FA            cli                 ; ...and do not be interrupted inside it
EC            in al, dx
A8 01         test al, 1
74 FB         jz -5               ; spin until blanking BEGINS
8B C3         mov ax, bx
AB            stosw               ; ONE WORD
FB            sti
E2 E8         loop
```

That is the whole technique and it is the ceiling on what any faithful answer
can achieve: **one word per horizontal blanking interval**.

## 2. The arithmetic

CGA's horizontal rate is 15,700 Hz, so the BIOS's dance moves **15,700 words a
second — 31 KB/s**. Everything below follows from that one number.

| what | words | at 15,700/s |
|---|---|---|
| one 80-column row | 80 | **5.1 ms** |
| a whole 80x25 screen | 2,000 | **127 ms** |
| one scroll-up (VRAM to VRAM: read 1,920 + write 1,920) | 3,840 | **245 ms** |
| a 25-line `DIR`, one scroll per line (§96.33.11) | ~96,000 | **6.1 s** |

The last row is the one that matters and it is why `DIR` looks the way it
does. It is **not** a consequence of snow avoidance — we do none — it is the
volume of VRAM traffic that a per-line scroll generates, and it is the same
volume whether or not it is synchronised.

**A real DOS `DIR` on a real CGA is therefore SLOW rather than snowy.** Its
output goes through `int 10h`, which does the dance above. Our box is fast and
snowy because it writes `con_scr` into `B800` with `rep movsw` and never asks
the status register.

## 3. The options

### A. Do nothing (the status quo)

Zero bytes, zero time. Snow on CGA during any bulk write; none on Hercules or
VGA. A single keystroke is already a single speck since §96.33.12 made the
dirty mark a SPAN — the field confirms *"single char is single snow"* — so
what is left is exactly the bulk cases: a scroll, a `CLS`, a program's exit
screen landing via `dos_snap`.

### B. Blank the display while writing — **the cheapest real answer**

CGA's mode register (`0x3D8`) bit 3 enables video. Clear it, write at full
`rep movsw` speed, set it again. The CRTC stops fetching, so there is nothing
to steal and no snow at all.

- **Cost: about 10 bytes and two `out`s**, and NO time cost — the write runs at
  memory speed rather than at 31 KB/s.
- **What it costs the LOOK** is the trade: the screen goes black for the
  duration. For one row (160 bytes, ~30 us) that is invisible. For a 25-line
  listing it is a visible flicker — but a flicker of ~6 ms, not the 6.1 s that
  option C costs.
- Period software did exactly this, which is the argument that it is not a
  cheat.

### C. Do the BIOS's dance ourselves in `con_tx_row`/`con_tx_scroll`

Faithful, and the numbers in §2 are what it costs: a `DIR` becomes **6.1
seconds** of listing on a 4.77 MHz machine. That is not a trade, it is a
different program. **Refused on the arithmetic**, and recorded here so it is
not proposed again.

### D. Batch into the VERTICAL blank instead of the horizontal

The vertical blanking window is ~1.2 ms of a 16.7 ms frame. At the ~12.5
clocks a byte `rep movsw` costs on this class of adapter, 1.2 ms buys roughly
**450 bytes** — so a full screen is 9 frames (**150 ms**) and a 25-line `DIR`
is ~215 frames (**3.6 s**). Better than C and still far worse than B, for much
more code. `fsx.inc` already has the primitive (`FSXW_VSYNC`, `fsx_insync`),
which is the only thing to recommend it.

### E. Coalesce the scroll again — and this one is OURS, not the card's

§96.33.11 made the full screen stream a line at a time, because the field
asked for it: *"can we print each line as it arrives, so it feels more like
DOS?"* `con_takescroll` ACCUMULATES pending scrolls, so one flush at the end of
a verb is **one** `rep movsw` of N rows; per-line flushing is **N** of them.
For a 25-line listing that is the difference between ~8 KB of VRAM traffic and
~96 KB — a **12x** reduction, and it is the single largest factor in "mega
snow".

**It costs the thing that was asked for**, which is why it is an option and
not a fix. A middle version exists: stream per line, but let `dos_fsx_owed`
skip the flush while output is arriving faster than a frame, so a fast listing
coalesces and a slow program's output still appears line by line. That is a
timestamp and a compare, and it is the only option here that improves both
halves.

### F. Give the full screen back to the ROM

Inside the bracket the screen IS the ROM's, and `dos_tty` already writes
through `int 10h AH=0Eh` when `[dos_inbr]` is set. Routing the CONSOLE's output
the same way would inherit the BIOS's snow avoidance for free and be maximally
faithful.

**It breaks §70.8.7**, which is the console's central claim: `con_scr`'s cell
IS the cell in VRAM, so the renderer is a move and not a translation, and
`tests/doscon.py` asserts that cell for cell over all 2,000. Give the screen to
the ROM and the buffer and the glass diverge — `dos_snap`, the scroll-back and
the windowed re-render all lose their source of truth. **Refused on the
architecture**, not on the cost.

## 4. What to take if it is ever picked up

**E's middle version first, then B.** E is the one that reduces the actual
traffic rather than hiding it, it is bounded work, and it makes the windowed
path faster too. B is then a few bytes for the residue, gated on
`[vid_kind]` = CGA so the other two adapters never execute it.

C, D and F are recorded as refused: C and D on the arithmetic in §2, F on
§70.8.7.

## 5. What has NOT been measured

Everything above is arithmetic from the 15,700 Hz line rate and the ROM's own
loop. **No snow measurement has been taken on real hardware**, and none can be
taken here: MartyPC models the CGA correctly enough to run the games but the
harness compares FRAMEBUFFERS, and snow is a property of what the CRTC fetched
rather than of what the framebuffer holds. 86Box's `hercules` renderer has
already produced one artifact that was not in this kernel
(docs/FIELD-NOTES.md, SPEC.md 79.5.9), so an emulator reading here would want
the same scepticism.

So the two numbers worth taking on the 5150 before building anything are: how
long a 25-line `DIR` takes now, and how it looks with option B's two `out`s in
front of the write.
