# The mouse is dead for the first half second on the desktop — why, and what it costs to have it moving sooner

> **OPEN — two of the mouse-side fixes are BUILT (B1 and B3, SPEC.md 9.4.8);
> the first frame is accepted as-is and the rest wait on a field reading.**
> Every number below is cycle-exact off MartyPC's 5150 models
> (`os8088_5150_cga_gla`, `os8088_5150_herc_gla`), one 360KB floppy boot each,
> the mouse driven by injected packets at a fixed guest pace.
>
> **Built (SPEC.md 9.4.8):**
> * **B1 — the poll interval is timed from the desktop.** `kmain` stamps a new
>   base word `[mou_hpbase]` at the first frame, so the poller's first DTR drop
>   moves from the first UI pass (on a long boot) to ~3s after the desktop.
>   `[mou_hpt]` is left 0-until-a-drop so `sysbench`'s "poller stamp (0=nvr)"
>   (§9.4.2) still reads true.
> * **B3 — the drain ends on quiet, bounded by a ceiling.** Each dropped byte
>   re-stamps `[mou_dstamp]`; the window closes `MOU_DRAINQ` = 3 ticks after the
>   last byte **or** `MOU_DRAINT` = 9 ticks after the raise, whichever first.
>   An idle-then-move user's first motion is read at once (was a fixed 505 ms);
>   continuous motion stays bounded at the ceiling exactly as before.
> * Verified on both adapters: `mou_hpbase` stamped, `mou_hpt` 0; idle-then-move
>   read at once; continuous bounded at +10 ticks. 31 bytes of `.text`, no rung.
>
> **Still owed** is one reading off the machine that shows the freeze (§4): the
> three mechanisms below have different signatures in a block the field machine
> already publishes, and which one it is decides whether B2 or C1 are worth
> building on top.

## 0. The finding

**Exactly three things can hold the arrow after the desktop appears, and a
machine pays for one, two or all three depending on what its mouse said to
`mouse_init`'s reset edge.**

| mechanism | fires when | dead time, measured | deterministic |
|---|---|---|---|
| **The first frame.** The arrow is not shown until `wm_paint_all` has finished; the desktop pattern is the first thing it draws, so the user sees a desktop with no pointer for the rest of the frame | every boot, every machine | **162.8 ms** CGA, **242.1 ms** Hercules — from the dither's first row to `cursor_show` | yes |
| **The hot-plug poller's first reset cycle** (SPEC.md §9.4). `mou_hotplug` drops DTR/RTS on the first UI pass, holds 3 ticks with the mouse unpowered, raises, and the ISR then discards every received byte for `MOU_DRAINT` = 9 ticks | only when the identify burst did **not** pass §9.4.1's four rules (`[mou_idany]` = 0), **and** 55 ticks have passed since `sched_init` by the time the desktop is up | **12 ticks ≈ 660 ms**; the drain half alone read **505 ms** (494 quantised by the packet pace) — *"the first half second"* | yes — at the first UI pass, or at tick 55 if the boot was shorter |
| **The packet contest** (SPEC.md §9.5). `MOU_LOCKN` = 8 clean packets before the first one is acted on | two live serial ports, and the mouse's port not strictly identified (`[mou_need]` still 8) | **8 reports**: 252.7 ms at one packet per 31 ms, ~200 ms at the mouse's own 40 Hz, longer for a slow hand | yes, once per boot |

On the field 5150 as it is documented — one serial port, a mouse that answers
`'M'` and nothing else, `poller stamp 0` (SPEC.md §9.4.1's own dump) — **only
the first row applies**, and it is 242 ms on the Hercules that machine boots
on. A freeze that really is half a second is one of the other two firing, and
§4 is the one reading that says which.

## 1. Where the arrow is between the splash and the desktop

`kmain`'s tail is `gfx_lock` → `wm_paint_all` → `gfx_unlock` → `cursor_show`
→ `drv_notice_x` → `jmp ui_task`. `cur_level` is −1 from boot, so nothing
draws the arrow before that `cursor_show` — and nothing should: SPEC.md §7.1
is that the ISR never moves the arrow while the gfx lock is held, and the whole
frame is one hold.

**The ISR is not idle through it.** Injecting packets from the moment
`wm_paint_all` is entered, `mouse_x` advanced by every packet (4 px each),
`[cur_dirty]` read 1, `[cur_level]` read −2 (the dither's `cur_unlazy` spent
`gfx_lock`'s promised hide on the already-hidden arrow), and `cursor_show`
then put the arrow at the **current** position. So there is no catch-up jump;
the pointer simply does not exist until the frame is done.

Where the frame's time goes, entry to entry, 5150 model:

| painter | CGA 640×200 | Hercules 720×348 |
|---|---|---|
| `thm_desk` — the desktop dither, full width, bar to dock | 46.5 ms | 97.7 ms |
| `desk_paint_x` — the drive icons and their labels | 33.8 ms | 60.2 ms |
| `dock_paint` — the strip | 10.4 ms | 9.8 ms |
| `menu_draw_bar` — the bar's fill, titles and clock | 71.9 ms | 74.2 ms |
| `wm_zwalk` + `gfx_unlock` + `cursor_show` | 2.0 ms | 2.1 ms |
| **frame** | **162.8 ms** | **242.1 ms** |

(`tools/os88boot.py`'s phase table agrees: `wm_paint_all` 776,882 cycles
on CGA, 1,155,363 on Hercules, with `gfx_unlock` 0.3 ms and `cursor_show`
1.6 ms after it.)

**Showing the arrow before the paint buys nothing.** `thm_desk` is unclipped
and calls `cur_unlazy`, so the arrow would be hidden again within the first
millisecond; and an arrow that survived to sit under the lock while the hand
moves is exactly the lit-and-stuck pointer SPEC.md §7.1.4.3 measured against
the hidden one and the field called a stutter (docs/FIELD-NOTES.md 28). A
pointer that is absent for a quarter of a second and then appears under the
hand is the better of the two behaviours the machine can offer here; the only
way to shorten it is to make the frame cheaper, and the table above is the
menu. All four painters are the same code every full repaint runs, so a byte
spent there is priced against PERFORMANCE.md Part 5's budget and not against
this document.

## 2. The mouse-side gates, and what each measured at

All four runs: break at the first entry of `ui_task` (the desktop is painted,
the arrow is up), read the mouse block, poke the scenario in, then inject one
packet every 150,000 cycles (31.4 ms) and read `mou_seen`, `mou_run`,
`mou_hpst`, `mou_drain`, `mouse_x` and `cur_drawn_x` after each. At that
breakpoint the tree as built reads:

```
ticks 30   seen 0   idany 1   need 1 / 8   idn 1 / 0   idb0 4D / 00
hpst 0     hpt 0    cur_level 0   lock 0   mouse (320,100) = drawn (320,100)
```

— MartyPC is a two-port machine, COM1 said `'M'` once, so COM1's threshold is
1 and COM2 still owes eight, exactly SPEC.md §9.4.1's dump.

### 2.1 The tree as built: the first packet moves the arrow

Packet 1, 31.6 ms: `seen 1`, `run 1`, `mouse_x 324`, `drawn_x 324`. Packet 2:
`hpst 2` — the poller stood down for the session on the UI task's next pass.
**Zero dead time after the arrow appears.** This is the case the documented
5150 is in.

### 2.2 The contest: `[mou_need]` forced to 8 on both ports

```
pk 1..7   seen 0   run 1..7   mouse_x 320   drawn_x 320      nothing moves
pk 8      seen 1   run 8      mouse_x 324   drawn_x 324      252.7 ms
```

Eight packets counted and discarded, the eighth acted on. SPEC.md §9.4.1
already prices this at ~200 ms of continuous motion and "a third of a second
of a real nudge"; the measurement agrees to the packet.

### 2.3 The poller: the half MartyPC can show, and the half it cannot

With `[mou_idany]` forced 0 and `[mou_hpt]` set so that `MOU_REPOLL` had
elapsed, pass 1 dropped DTR (`hpst 1` after packet 1) — and the packet
injected in that same pass was **delivered and settled the port while DTR was
low** (`seen 1` at packet 1, `hpst 2` at packet 2 as `.have` put the power
back). A mouse powered by DTR/RTS cannot do that, which is §9.4's premise and
the reason the drop exists; MartyPC's serial mouse answers the rising edge
(§9.4.1 confirmed that here) but keeps reporting through the low. So the
unpowered 3 ticks are the hardware's and are taken from SPEC.md §9.4; the
**drain** half was measured by arming it by hand — `[mou_drain]` 1 and
`[mou_dstamp]` = `[ticks]`, as the raise leaves them:

```
pk 1..15  seen 0   run 0   drain 1   mouse_x 320   drawn_x 320   eaten
pk 16     seen 1   run 1   drain 0   mouse_x 324   drawn_x 324   505.4 ms
```

Fifteen packets of a moving hand discarded whole, the sixteenth settles the
port and moves the arrow. `MOU_DRAINT` is 9 ticks = 494 ms; the 505 is that,
quantised by the pace. `mou_hotplug`'s own comment names this seam — *"motion
STARTED inside a drain window is eaten with it — up to half a second"* — and
it is the only mechanism in the machine whose number is the one reported.

**When it lands on the desktop.** Before B1 the interval base `[mou_hpt]`
started at 0 and `[ticks]` at `sched_init`, so pass 1 dropped DTR if and only
if `[ticks]` ≥ `MOU_REPOLL` = 55 at the desktop. On this bare 360KB boot it is
**30** (1.65 s: `mouse_init` 574
ms, `drv_boot` 815 ms for `SYSTEM.CFG`'s nine sectors, `desk_init` 42 ms, the
frame 163–242 ms), so the first cycle would fire **1.4 s after** the desktop
instead — the same 660 ms of dead mouse, arriving while the user is reaching
for it, and eaten with the same drain. Every driver `SYSTEM.CFG` asks for adds
one to three seconds of floppy in front of the frame and an empty drive B adds
§18.97's 32 ticks, so a configured machine is past 55 and pays it on pass 1.

### 2.4 What makes a real mouse fail the identify

`[mou_idany]` = 0 with a mouse plugged in means `mou_idjudge` refused its
burst, and of the four rules the one a working mouse fails is **rule 3**:
`MOU_IDMAX` = 8 bytes in all. A **PnP** serial mouse answers the raise with
`'M'` (or `'M3'`, `'MZ@'`) and then a Plug and Play ID string — `'('` … `')'`
plus checksum, 17 to 67 bytes at 7.5 ms each — which SPEC.md §9.4.1 names as
*"the accepted degradation"*: no stand-down, no threshold drop, and therefore
**both** of the second and third rows above, on every boot, with a mouse that
works perfectly once it is through them. The same mouse fails
`MOU_IDSTRICT` = 3 as well, so on a two-port machine its port keeps
`[mou_need]` = 8. Period Microsoft and Logitech parts answer one to three bytes
and pass; anything sold as PnP, roughly 1995 on, does not. Nothing in the tree
has ever seen a PnP burst — MartyPC's mouse sends one byte and 86Box's early
close fires on the same rules, so both emulators say `idn 1`.

## 3. Options, costed

Sizes are bytes of the section named, nothing else; a rung is not a design
input (CLAUDE.md). `kernsize` on this tree: image rung 63 left, cold rung 502
left, `.text`+`.bss` 8,255 under `KERN_CODE_MAX`.

### A. The first frame — no change proposed

The arrow cannot move under a lock hold and should not be lit under one
(§1), so the only lever is the frame's cost, and every painter in it is
shared with every full repaint. Nothing to build for this document's question;
`menu_draw_bar` at 72–74 ms and `thm_desk` at 46–98 ms are where a
PERFORMANCE.md Part 5 pass would look first.

### B1. Time the poll interval from the desktop — **BUILT (SPEC.md 9.4.8)**

`kmain` stamps a new base word `[mou_hpbase]` with `[ticks]` right after
`cursor_show`, and the poller counts `MOU_REPOLL` from it (re-based at each
raise) instead of from `[mou_hpt]`:

```
    mov ax, [ticks]
    mov [mou_hpbase], ax        ; the interval starts when the pointer exists
```

The first reset offer moves from the first UI pass to **~3 s after the
desktop** — measured, `mou_hpbase` = 29 at the desktop on the bare Hercules
boot, so the first drop is at tick ~84 rather than 55. A user who moves within
three seconds settles the port and the poller never fires; one who does not
still meets the pre-existing seam SPEC.md §9.4.1 prices, and a machine with no
mouse loses only a three-second delay of its first offer.

**Why `[mou_hpbase]` and not `[mou_hpt]`.** The first draft stamped `[mou_hpt]`
— but `sysbench` reads `[mou_hpt]` as *"poller stamp (0=nvr)"*, "0 = it never
dropped DTR" (§9.4.2), which is the field diagnostic this whole document points
the user at. Stamping it at the desktop makes every machine read as though the
poller had fired. So the base and the diagnostic are split: `[mou_hpbase]` bases
the interval, `[mou_hpt]` stays 0 until a real drop. Verified: `mou_hpt` reads
0 at the desktop on both adapters.

### B2. `MOU_IDMAX` 8 → 72 — **0 bytes, a spec decision**

A PnP burst then passes rule 3 and the poller stands down (`[mou_idany]`). It
weakens rule 3 on the **safe** half only: the threshold drop keeps
`MOU_IDSTRICT` = 3, so no PnP mouse claims a port on the strength of its
string, and the cost of a false stand-down is a later hot-plugged mouse
missing its reset edges. Rules 2 and 4 stand — `'M'` first, quiet for the
window's last 4 ticks — so a modem banner still has to begin with `'M'` and
finish inside the window to be mistaken. docs/FIELD-MACHINES.md's M1 (the
Compaq Portable III with its modem) is the A/B this was always waiting for,
and this constant should ride on it rather than ship ahead of it.

### B3. End the drain when the port goes quiet — **BUILT (SPEC.md 9.4.8)**

The `.drain` arm re-stamps `[mou_dstamp]` on every dropped byte and closes the
window when it has been quiet for `MOU_DRAINQ` = 3 ticks **or** `MOU_DRAINT` = 9
ticks have passed since the raise (a new base word `[mou_draise]`) — the
floor/quiet/ceiling shape of §9.4.5's identify window. The close is lazy, so an
idle-then-move user's first motion — arriving long after the burst — closes the
window and is read at once (measured: was a fixed **505 ms**).

**The naïve version — re-stamp and cut `MOU_DRAINT` to 3, no ceiling — is a
regression and was not built.** Motion is itself a stream of bit-6 bytes, so
continuous motion begun at the raise would re-stamp forever and the window
would never close: the mouse dead for as long as the hand kept moving. The
ceiling (`MOU_DRAINT` from `[mou_draise]`) bounds exactly that case as the old
fixed window did — verified, continuous injection closes at +10 ticks — so the
change has a better typical case and no worse worst case. `MOU_DRAINQ` = 3 is
wider than the sub-tick gaps inside a 1200-baud burst, so it cannot close
mid-burst. Cost: the arm gains one word (`[mou_draise]`), the ISR path a few
instructions.

### C1. Read the PnP frame in the identify window — **~40 bytes of `.ovl`, 2 of `.bss`**

`mou_idbyte` runs inside the window and sees every byte; noting `'('` and
`')'` per port and letting `mou_idjudge` count *`'M'` then a closed PnP
frame* as a **strict** identify fixes the stand-down and the threshold at
once, so a two-port PnP machine's contest goes from eight packets to one.
It is boot-overlay code, reused as heap once the desktop is up, and
`.ovl` has room for it; the risk is a PnP string a real mouse frames
differently from the specification, which only a PnP mouse on iron can say.

### C2. `MOU_LOCKN` 8 → 4 — **0 bytes, not recommended**

Halves the contest to ~100 ms and halves SPEC.md §9.5.1's defence with it:
twelve bytes of binary traffic must fall right instead of twenty-four. The
identify already makes the contest cost nothing on every machine that has
been measured, so this trades a documented safety margin for a case C1
answers properly.

## 4. The reading that decides it

`tests/sysbench`'s `MO` block (SPEC.md §9.4.2), on the machine that shows the
freeze, **with the mouse untouched from power-on until the desktop** — moving
it earlier settles the port and hides two of the three mechanisms:

| reading | mechanism | option |
|---|---|---|
| `identified COM1 1`, `ident bytes 1..3`, `packets needed 1`, `poller stamp 0` | the first frame only — 163–242 ms, not 500 | A: nothing to build here |
| `identified 0`, `ident bytes` > 8 | a PnP burst refused by rule 3; the poller fires (`poller stamp` ≠ 0 once it has) | B1 now; B2 or C1 to settle it |
| `identified 0`, `ident bytes 0` | the mouse did not answer the raise at all | B1; then a wire question, not a kernel one |
| `identified 1` but `packets needed 8` on the mouse's port | two ports and a burst over `MOU_IDSTRICT` | C1 |
| `poller stamp` ≠ 0 with `identified 1` | cannot happen on this tree — the stand-down is tested before the drop | report it |

`make MOUDIAG=1` (SPEC.md §9.4.6) draws the same numbers on the desktop for a
machine that cannot run a package.

## 5. Method, so the numbers can be taken again

- **Phase table**: `python3 tools/os88boot.py --machine os8088_5150_cga_gla`
  (and `_herc_gla`). It reads the compressed kernel's four `KZ_*` defines from
  `build/kernel.kz.json` exactly as `os88sym` does; before this commit it did
  not, and refused a plain tree with *"the listing is a DIFFERENT kernel"*.
- **Frame split**: from a `boot=False` launch, `bp_exec` one symbol at a time
  in the order `wm_paint_all`, `thm_desk`, `desk_paint_x`, `dock_paint`,
  `menu_draw_bar`, `wm_zwalk`, `gfx_unlock`, `cursor_show`, `ui_task`, reading
  `status()["cycles"]` at each; each gap is the earlier routine.
- **Dead time**: `bp_exec("ui_task")` (or `"wm_paint_all"` for the motion-
  through-the-frame run), read the block, `write()` the scenario —
  `mou_need` `08 00 08 00` for the contest; `mou_idany` 0 plus `mou_hpbase` =
  `ticks − 55` for the poller (B1 based it there); `mou_idany` 0, `mou_drain` 1,
  `mou_dstamp` = `mou_draise` = `ticks` for the drain — then loop
  `m.mouse(dx=4)`, `m.advance(cycles=150000)`,
  and read `mou_seen`, `mou_run`, `mou_hpst`, `mou_drain`, `cur_level`,
  `mouse_x`, `cur_drawn_x`. The first packet at which `cur_drawn_x` moves is the
  dead time, to within one pace. Guest cycles, so it repeats to the cycle.
