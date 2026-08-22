# C64 port plan — VICE 3.10's `x64` as an os8088 C package

The design record for **`docs/C64-SPEC.md`** — the contract, which by the
user's instruction lives in its own file and not in SPEC.md — produced by
`.claude/skills/port-to-os8088`'s scouting workflow, reconciled against three
adversarial reviewers, then **re-cut against an outside adversarial review
(Codex, read-only) that found 6 blockers and 9 majors, all accepted**.
`workflows/implement.js` reads this file one wave at a time.

The reference source is **VICE 3.10** (https://vice-emu.sourceforge.io/),
GPL-2-or-later, © 1996–2025 the VICE team. It lives outside this repo and
**nothing of VICE's source is vendored** (`CONTRIBUTING.md` §6). The three
Commodore ROM binaries are the one stated, user-decided exception, and they
are Commodore's, not VICE's and not ours — Decision 1 and `C64-SPEC` §1.3.

## Summary

Port VICE 3.10's `x64` — the fast, non-cycle-exact C64 — to os8088 as one C
package in the RUNCPM shape: **`C64.O88` + `C64.OVL` + `C64.ROM`, three files
in one folder on every disk**. A hand-written 8086 6510 core runs in a 64KB
heap claim (the C64's RAM is its own segment) and the KERNAL, BASIC and
CHARGEN ROMs in a second, 20KB claim read at launch from a **sidecar
`C64.ROM`** built by `tools/c64rom.py` from binaries committed under
`apps/c64/rom/`. No worker, nothing blocking: the machine runs on the UI task
in wake-driven **wall slices** (`OSAPI_WM_WAKE` / `OSAPI_WM_ONWAKE`), a raw
6510-cycle budget of 256–16,384 seeded from `os88_cpu()` and adapted only on
genuinely exhausted slices, with **every device phase retained across slices**
so the floor is milliseconds and never a whole emulated jiffy.

**Time is 6510 cycles.** Every opcode carries its real cost from
`6510core.c`'s tables, page-cross and taken-branch penalties included, and
one cycle counter — kept in the emulated machine's own top-of-memory scratch,
never in the package's bss — is both the wall clock and the device clock.
Devices run on **VICE's own alarm model** (`src/alarm.c`): the C side computes
cycles to the next event (CIA1/CIA2 timer A/B underflow, VIC raster compare,
end of raster line, frame end, TOD tick), the core runs to it and calls one
event handler. Nothing quantises to a jiffy: the PAL frame is 63 × 312 =
19,656 cycles (50.123 Hz), the 60 Hz jiffy **emerges** from the KERNAL
programming CIA1 timer A itself, and CIA TOD ticks from its own 50 Hz
accumulator — three independent phase accumulators, so nothing is 20% wrong.

**The screen is dirty-tracked by the core, not shadowed by the model.** Every
RAM write ORs one bit into a 256-page dirty bitmap (32 bytes, ~5 instructions
a store, the stated per-write cost); the flush maps dirty pages through the
frame registers onto the cell rows they feed, composes only those rows,
compares each composed row against an **8,000-byte 1bpp frame shadow** and
draws only the differing spans. That is what lets bitmap modes, a RAM
character set and sprite data repaint at all — a matrix-and-colour shadow
provably cannot see them. The flush runs at most once per host tick, detects a
shift of **k = 1..24** rows and emits one `gfx_scroll` plus the `k` vacated
rows, and redraws the border only when `$D020` changed.

The VIC-II, the two CIAs, the keyboard matrix, the joystick, PRG autostart,
the menus and About are reimplemented in SPEC.md §73's C subset from the named
VICE files; what carries is **tables and behaviour** — opcode semantics and
cycle costs, the seven cartridge-less bank maps, the `$00`/`$01` processor
port, the CIA and VIC register files, `gtk3_sym.vkm`, `vice.vpl`,
`autostart-prg.c`'s injection, `uimachinemenu.c`'s strings, every
`hotkeys*.vhk` caption. The keyboard is a **level** model as VICE's
`keyboard.c` is: `OSAPI_KEY_DOWN` armed once in `os88_main`, host key state
polled **once per wake** and cached, the matrix **rebuilt** from the whole
down-list each wake (never cleared incrementally — mappings share the
synthetic SHIFT/CTRL bits), a fresh press guaranteed one slice before release
polling can clear it.

**The screen is monochrome on every adapter, VGA included** — a priced fact
(SPEC.md §5.4.1's span writer, ~215 µs a colour run, ~1,000 runs a text
band), not a shortcut; the EGA-16 map and the per-row colour record stay in
so a future kernel ink/paper `blit1` slot drops in without re-planning.
Speed is measured on the first core build and printed in the status bar as
VICE's own strings — `%7.0f%% cpu` from emulated cycles ÷ 985,248 and
`%8.1f fps` from **emulated VIC frames**, both from two-word counters folded
often; flushes per second is a separate harness counter and is not called
"fps". Nothing throttles.

**Rendering is gated in measured milliseconds on the icount bench, never in
API call counts**, and correctness of the composer is gated against
`tools/c64ref.py` — an independent, pixel-level Python compositor written
from VIC-II documentation and VICE's `src/vicii/`, compared **bit for bit**
against what the package composed. Automated evidence (QEMU + QMP +
screendumps, the host harnesses) and manual evidence (the three 86Box
machines, read by a person) are kept apart: **no `done_when` rests on 86Box.**

Budget, re-priced after the review: **~39,000 image + ~13,600 bss +
~6,000 overlay**, 52,600 resident of 61,440 — 2,400 under SPEC.md §73.9's
55,000 trigger before a line is written. There is **no wave-1 stub size
line**; the first honest one is at the end of wave 2 with the whole core in,
and five figures are reported at every wave: resident image, bss, overlay,
resident shims, largest frame.

---

## Decisions

### The user's

1. **ROMs: a sidecar, loaded at runtime — not embedded.** The user's words:
   *"move all the necessary code into my repo, and use a sidecar that is
   loaded at runtime. Don't cut features, make it modular so that we can load
   the rom at runtime instead of embedding it in the package."*
   `C64.ROM` is 20,480 bytes — KERNAL 8K + BASIC 8K + CHARGEN 4K — read into
   its own 20KB heap claim at launch. The three Commodore ROM binaries are
   **committed** under `apps/c64/rom/` (`kernal-901227-03.bin`,
   `basic-901226-01.bin`, `chargen-901225-01.bin` — the defaults VICE 3.10's
   own `c64-resources.c` picks), with `apps/c64/rom/README.md` stating they
   are Copyright Commodore Business Machines and are distributed as VICE
   distributes them, so the build does not depend on `../vice-3.10`. **This is
   a stated, user-decided departure from `CONTRIBUTING.md` §6 for those three
   files only.** `tools/getc64rom.py` becomes **`tools/c64rom.py`**, which
   concatenates the three into `build/c64-rom/C64.ROM` with a fixed layout and
   a SHA-256 check. **No feature is cut.**
2. **Colour: monochrome now**, 1bpp by luminance on every adapter, stated as
   a fact. The plan keeps the 16-colour map and the per-row colour record so
   a future kernel ink/paper `blit1` slot drops in without re-planning. A
   follow-up, not this port.
3. **Programs: none ship.** `tools/c64prg.py` writes `.PRG` test fixtures
   from BASIC listings.
4. **Speed posture: correct first, honest about speed.** Measured on the
   first core build and printed in the status bar (the speed widget's
   strings); degrade by tier through `os88_cpu()`.
5. **Licence:** VICE is GPL-2-or-later; attribution in every derived file
   header, the About box and `apps/c64/COPYING`; nothing of VICE's source
   vendored.
6. **Three make-runnable 86Box machines**, not one: `vm/xt-c64` (360KB disk,
   copied from `vm/xt-runcpm`), `vm/286-c64` (720KB, from `vm/286-runcpm`),
   `vm/386-c64` (1.44MB, from `vm/386-runcpm`) — each with only the B: image
   (`fdd_02_fn`) and the uuid changed; make targets `xt-c64` / `286-c64` /
   `386-c64` on the three-line pattern the RUNCPM machines use; documented in
   `README.md` (the command list and the machine table) and `CLAUDE.md`'s
   machine list. `vm/386-c64` is in wave 1 and the other two in the final
   wave. **This overrides LESSONS 9's "one machine before it is measured".**
7. **Names:** package `C64`, directory `apps/c64`, images `build/c64.img` /
   `c64720.img` / `c64360.img`, files `C64.O88` + `C64.OVL` + `C64.ROM` in
   one folder on every disk (a folder of its own on `apps-all.img`, never
   `APPS/`).

### Decided from the outside review

Each replaces a piece of the drafted design; the superseded design is not
kept anywhere.

8. **Decided from review (Codex finding 1) — `$0000`/`$0001` are not RAM.**
   Every read and write of `$0000`/`$0001` goes to a special case (DDR /
   data-port semantics from `src/c64/c64mem.c`, `c64pla.c`), and a write to
   `$01` re-evaluates the bank map at once. Opcode and operand fetches from
   `$D000-$DFFF` with I/O mapped go through the I/O path — **a true slow
   fetch path** — not the biased-`ES` fetch. `C64-SPEC` §3.2 and `C64-SPEC` §3.4.
9. **Decided from review (Codex finding 2) — the fetch bias is re-evaluated
   on every mapping-boundary crossing**, not only at control transfers: the
   core keeps a "next boundary above PC" word in the C64's own scratch and
   does one `cmp` per instruction fetch against it (`$A000`, `$C000`,
   `$D000`, `$E000` and the wrap), plus on every write to `$00`/`$01`. The
   cached `ES` is saved before and reloaded after every cdecl I/O call (`ES`
   is caller-clobbered). Boundary cases for every map transition are in the
   CPU harness. **The "stale bias across a fall-through" deviation is
   withdrawn.** `C64-SPEC` §4.3.
10. **Decided from review (Codex finding 3) — time is 6510 CYCLES, not
    control transfers.** Each opcode carries its real cycle cost (plus
    page-cross and taken-branch penalties, from `6510core.c`'s tables); the
    core decrements ONE cycle counter kept in the C64's own top-of-memory
    scratch. That counter is the wall-clock slice budget AND the device
    clock. **The "~1,100 control transfers = 16,421 cycles" equivalence is
    withdrawn.** `C64-SPEC` §4.2.
11. **Decided from review (Codex finding 4) — VICE's ALARM model
    (`src/alarm.c`), no per-jiffy quantum.** The C side computes "cycles to
    the next event" (min of CIA1/CIA2 timer A/B underflow, VIC raster
    compare, end of raster line, frame end, TOD tick) and the core runs until
    it, then calls one C event handler. The PAL frame is 63 × 312 = 19,656
    cycles (50.12 Hz); the 60 Hz jiffy EMERGES from the KERNAL programming
    CIA1 timer A itself; CIA TOD ticks from its own 50 Hz accumulator. Three
    independent phase accumulators, so nothing is 20% wrong. `C64-SPEC` §4.4,
    `C64-SPEC` §5.2 and `C64-SPEC` §6.3.
12. **Decided from review (Codex finding 5) — sub-frame wall slices.** A raw
    cycle budget, `os88_cpu()`-seeded and adaptive exactly as RunCPM's:
    256..16,384-equivalent, doubled after four inside one tick, halved across
    two ticks, **ONLY exhausted slices adapt**; every device phase retained
    across slices; **the floor is never a whole jiffy.** `C64-SPEC` §4.4.
13. **Decided from review (Codex finding 6) — screen change detection is a
    256-page DIRTY BITMAP** (32 bytes) set by the core on EVERY RAM write
    (one `or` into the bitmap on the write fast path — the stated per-write
    cost, ~5 instructions). The flush composes only the rows whose source
    pages (matrix, charset/bitmap, colour RAM, sprite data and pointers, as
    the frame registers select them) are dirty, compares each composed 1bpp
    row against an 8,000-byte 1bpp **frame shadow** (320×200 bits, bss), and
    draws only the differing spans. **The 2,000-byte per-tick compare, the
    matrix and colour shadows, `c64_rowdiff` and the "compares per second"
    counter are all withdrawn**; the counter becomes **dirty pages per
    wake**. bss rises to ~14KB — re-priced in Decision 15. `C64-SPEC` §9.2 and `C64-SPEC` §9.3.
14. **Decided from review (Codex finding 7) — explicit `(segment, offset)`
    pairs.** Cross-segment data is passed as explicit `(segment, offset)`
    pairs to every `c64band`/`c64mem` routine, never as a C pointer; any
    `cmps`/`movs`/`stos` loads `DS`/`ES` on purpose, restores them, leaves
    `DF` clear and carries the `cc8086:allow` marker; **these routines are
    tested with `SS != DS` and an `ES` sentinel (the `rcmemtest` shape), not
    only the movers.** The composer takes a **changed SPAN** (first..last
    cell), not always 40 cells. `C64-SPEC` §3.6 and `C64-SPEC` §9.5.
15. **Decided from review (Codex finding 13) — the budget, re-priced, and the
    wave-1 stub size line withdrawn as dishonest.** The first honest size line
    is at the **end of wave 2** with the whole core in. bss ~13,600 (8,000
    frame shadow + 1,024 colour RAM + dirty structures + device phase +
    statics and out-parameters; the 152-entry vkm table is `.data`, not bss;
    the 6510 cycle table 256 bytes; the ×2 lookup 512); resident image
    ~38,000–40,000; overlay ~6,000. **Overlay loading shims stay RESIDENT and
    are counted.** The 55,000 trigger is stated. **Resident image, bss,
    overlay, resident shims and largest frame are reported separately at
    every wave.** `C64-SPEC` §13.
16. **Decided from review (Codex findings 9 and 10) — the keyboard.**
    `OSAPI_KEY_DOWN` is armed **once in `os88_main`** (its first call clears
    and arms the map, so a first-slice call would erase the make already
    seen); host key state is polled **ONCE PER WAKE** and cached, and every
    emulated CIA read uses the cached matrix; the matrix is **REBUILT from
    the full down-list every wake**, never cleared incrementally, because
    mappings share the synthetic SHIFT/CTRL bits; a fresh press is guaranteed
    at least one slice before release polling can clear it; the 16-entry
    down-list overflow path is bounded and tested. Price: **≤ ~20 thunks per
    WAKE, not per sub-slice.** `C64-SPEC` §7.2.
17. **Decided from review (Codex finding 12) — the scroll test detects
    `k = 1..24`**, not only one row, and emits one `gfx_scroll` plus the `k`
    vacated rows; the border is redrawn only when `$D020` changed; **the gate
    asserts one scroll per FLUSH, not per emulated line.** `C64-SPEC` §9.4.
18. **Decided from review (Codex finding 11) — rendering gates are in
    measured milliseconds** on the icount harness (compose instructions and
    cells), not in API call counts. **"Full bitmap frame = 25 calls" is not
    an acceptance criterion.** `C64-SPEC` §9.7.
19. **Decided from review (Codex finding 14) — two-word counters.** The
    calibration and every per-second figure use two-word (lo/hi) counters
    folded often, as `rc_ticks32` does — **"no `long`" is a C rule, not a
    no-32-bit rule.** The status bar's **fps is EMULATED VIC FRAMES per
    second** (it can reach 50.1), and flushes/s is a separate harness
    counter. `C64-SPEC` §10.2.
20. **Decided from review (Codex finding 15) — every callback is resident.**
    `os88_onfile` (and `os88_paint`, `os88_onkey`, `os88_onclick`,
    `os88_onwake`, `os88_worker`) is RESIDENT — a callback is reached by a
    near offset; it calls an already-loaded `ovl_*` helper. Moved out of
    `c64load.c`'s non-resident list and accounted for. `C64-SPEC` §11.3 and `C64-SPEC` §13.3.
21. **Decided from review (Codex finding 15) — automated and manual evidence
    are split.** Automated: QEMU + QMP + screendumps, the host harnesses.
    Manual: the 86Box machines — a person launches and looks; a `make` target
    cannot assert a boot. **No `done_when` may rest on 86Box.** The host
    harness gains an **independent pixel-level reference compositor**
    (`tools/c64ref.py`, Python, from VIC-II documentation + VICE's `vicii/`
    as the authority) that renders the same C64 memory to a 320×200 1bpp
    image, compared **bit for bit** against the package's composed frame —
    this is what validates bitmap, multicolour, custom charsets and sprite
    priority/expansion, not the cell-identity model. `C64-SPEC` §14.5 and `C64-SPEC` §14.6.
22. **Decided from review (Codex finding 8) — the CPU harness is nine rows,
    not one.** The boot-sector harness (`SS != DS`, the `rcz80test` shape)
    runs the SHIPPING core with: Klaus Dormann's 6502 functional and decimal
    tests; every one of the 7 bank maps; reads/writes/**fetches** at
    `$0000`/`$0001` and at `$9FFF`/`$A000`/`$BFFF`/`$C000`/`$CFFF`/`$D000`/
    `$DFFF`/`$E000`; **real I/O stub returns** (not park-at-FAIL);
    `ES`/`DS` restoration checks; IRQ/NMI entry; the illegal opcodes; and
    cycle totals per opcode family against `6510core.c`'s table — **with
    negative controls on every row.** `C64-SPEC` §4.6.

Everything else below was decided by the plan and stands unless a wave shows
it wrong — and then this file and `docs/C64-SPEC.md` are amended together.

---

## Authority table (surface → the VICE file that defines it)

The table is `docs/C64-SPEC.md` §2, in full, with every string verbatim and
every path relative to the VICE 3.10 source tree. It is the **first** thing
written (LESSONS 1: *"When you find yourself typing a menu string you did not
just read, stop"*), and it is not duplicated here — a second copy is a copy
that goes stale.

Its rows, by subject: the menu bar and every item string
(`uimachinemenu.c`); every hotkey caption, live or greyed (`hotkeys.vhk` and
its includes); the keyboard map (`gtk3_sym.vkm`, all 152 entries); the level
keyboard model (`keyboard.c`); the 16 colours (`vice.vpl`); the window title
(`ui.c:1842` + `c64.c:179`); the status bar (`uistatusbar.c`,
`statusbarspeedwidget.c:572`/`:653`); the About box (`uiabout.c`,
`configure.ac`, `README` 186–290, `COPYING`); the machine model
(`c64.h:35-40`, `c64model.c`); 6510 opcode semantics **and cycle costs**
(`6510core.c`, `interrupt.c`); the `$00`/`$01` port (`c64pla.c`,
`c64mem.c`); the bank maps (`c64meminit.c`); **the alarm model**
(`alarm.c`, `maincpu.c`); the CIAs (`ciacore.c`, `c64cia1.c`, `c64cia2.c`);
the VIC-II (`vicii-mem.c`, `viciitypes.h`, `vicii-draw.c`,
`vicii-timing.h`); PRG autostart (`autostart-prg.c:354-395`, `autostart.c`,
`kbdbuf.c`); the joystick bits (`joystick.c`, `c64cia1.c`); the SID register
file (`sid.c`); the icon (`vice-x64_16.png` as a reference look only); and
the os8088 precedent every non-VICE surface follows (SPEC.md §74, §74.1,
§74.2, §74.4, §9.6, §9.7, §5.4.1, §5.4.2; `apps/runcpm/*`).

---

## Scope

### Ships

- **6510 core**: all 256 opcodes including the illegal ones `6510core.c`
  implements, decimal-mode ADC/SBC, IRQ/NMI/BRK/RTI, the `$00`/`$01`
  processor port with the seven cartridge-less bank maps, **per-opcode cycle
  costs with page-cross and taken-branch penalties**, the boundary-guarded
  fetch, a true slow fetch path for I/O, and a wall-slice loop that answers
  `C64_RUN_SLICE` / `C64_RUN_JAM` only — every `$D000-$DFFF` access a direct
  cdecl call into `c64io.c`, the core never exiting mid-instruction
- **64KB RAM as a heap claim**; BASIC/KERNAL/CHARGEN from `C64.ROM` read into
  a 20KB claim at launch (a disk missing `C64.ROM` refuses at launch with the
  fact); colour RAM (1KB of nibbles) and the VIC/CIA/SID register files in
  bss; the core's hot scratch — the cycle counter, the dirty bitmap, the
  boundary word, the cached fetch `ES` — in the emulated machine's own top 64
  bytes, never in the package's bss (LESSONS 13's TCG finding)
- **The alarm scheduler**: cycles-to-next-event over both CIAs' timers, the
  VIC raster compare, the raster line, the frame end and the TOD tick; one
  handler call; three independent phase accumulators
- **CIA1**: timers A/B (one-shot and continuous, underflow IRQ), ICR with
  mask/flag semantics, PRA/PRB keyboard matrix scan with DDR, joystick bits
  on both ports; **CIA2**: timers, PRA VIC-bank bits, NMI from timer
  underflow; TOD read/write on its own 50 Hz accumulator. The 60 Hz jiffy is
  what the KERNAL's own CIA1 timer A produces, so BASIC's keyboard scanner,
  the cursor blink and `TI$` all run
- **VIC-II**: standard text, multicolour text, extended-background text; hires
  bitmap and multicolour bitmap; border/background `$D020-$D024`; screen and
  char base from `$D018` + the CIA2 bank; 25/24-row and 40/38-column flags
  honoured as border; `$D011`/`$D016` fine scroll as whole-cell offset only
  (stated); `$D012` read and raster compare off the cycle clock at raster-line
  granularity; `$D019`/`$D01A`; 8 hires sprites (position, enable, priority,
  x/y expand) composed into the rows they touch; multicolour drawn by
  luminance threshold; collision registers answer 0 (stated)
- **Composer**: 1bpp spans via `OSAPI_GFX_BLIT1` on EVERY adapter, VGA
  included; core-driven dirty pages → composed rows → bit compare against an
  8,000-byte 1bpp frame shadow → only the differing spans drawn; the `k`-row
  shift test first, emitted as one `gfx_scroll` + `k` rows and falling back to
  spans when `gfx_scroll` refuses; the flush at most once per host tick;
  border fills only on a `$D020` change; 1:1 in the window (320×200 + 8-px
  border), 2× in fullscreen where the tier affords it
- **Keyboard**: a level model driven from `OSAPI_KEY_DOWN`, armed once in
  `os88_main`, polled once per wake and cached, the matrix rebuilt from the
  down-list each wake; `gtk3_sym.vkm`'s 152 entries; Tab = C= routed on scan
  `0x0F` while Ctrl+I is scan `0x17`, Ctrl+H `0x23`, Ctrl+M `0x32`;
  `CTRL+digit` from the digit scancodes while Ctrl is down — no VICE Alt+digit
  chord stolen; Page_Up = RESTORE (NMI) with `os88_key_down(KSC_ESC)` read at
  that moment so RUN/STOP+RESTORE warm-starts; Esc = RUN/STOP; Home, Ins, Del,
  F1–F8, arrows, End/PgDn, Pound on backslash — all per the `.vkm`; SPEC.md
  §9.6's sentence stated as SPEC.md §74.2 states it, and
  `ScrollLock for joystick` printed in the status row when no mouse has spoken
- **Joystick**: port 2 on the cursor/numpad arrows + Ctrl as fire (this port's
  choice, stated: the only joystick source this machine has; VICE's default is
  none), read from the cached key state; port 1 empty; Alt+J swaps ports; the
  two 5-dot indicators
- **Program loading**: File > Smart attach... (Alt+A) on `.PRG` through the
  Standard File dialog; autostart is VICE's RAM-injection mode — reset, wait
  for `READY.` in screen RAM, inject at the load address,
  `mem_set_basic_text(start, end)` unconditionally, feed `RUN\r` into
  `$0277`/`$C6`; files up to 65,533 bytes; a `.PRG` whose end passes `$FFFF`
  refused with the fact, before the disk is touched
- **Menus** in the kernel bar (exactly 5 = `MENU_APPMAX`, `AM_NAME` `VICE`,
  Debug absent by VICE's own `#ifdef`): File, Edit, Snapshot, Preferences,
  Help; live items Smart attach..., Reset machine CPU (Alt+F9), Power cycle
  machine (Alt+F12 caption, the item guaranteed), Exit emulator (Alt+Q),
  Copy (Alt+Delete caption, item guaranteed), Paste (Alt+Insert caption, item
  guaranteed), Fullscreen (Alt+D), Warp mode (Alt+W), Pause emulation
  (Alt+P), Advance frame (Alt+Shift+P), Swap joysticks (Alt+J), About VICE...
- **Status bar** under the screen: message area, Joysticks indicators, drive 8
  + track (greyed), the speed widget's two strings folded on one row —
  `% cpu` from emulated cycles ÷ 985,248 and `fps` from emulated VIC frames,
  both from two-word counters — delta-drawn, warp and pause as two LED dots
- **SID**: voice 1's frequency and gate to `os88_snd_tone` once per slice on
  change (one far call); voices 2–3, waveforms, ADSR and filters greyed
- **Fullscreen**: `os88_fullscreen` on Alt+D both ways, stated as the
  SPEC.md §11.2.1 exception the way SPEC.md §74.2 states Alt+F, because the
  C64 owns F and Esc; 2× where the tier affords it, 1:1 centred otherwise
- **About panel** (12 rows): the product line, VICE 3.10, what this port is,
  the VICE copyright, GPL-2-or-later, the Commodore ROM line, OK
- **Disk images in three geometries**, `C64.O88` + `C64.OVL` + `C64.ROM` in
  one folder with a `README.TXT`; the `C64\` folder on `apps-all.img`; the
  three 86Box machines (Decision 6)
- **Harnesses**: `hosttest/c64uitest.c` (the whole program over a stub
  `os88.h`, a glass model, a scripted core, the cost table in ms);
  **`tools/c64ref.py`** (the independent pixel-level compositor);
  `hosttest/c64cputest.asm`/`.sh` (the nine-row core gate, `make c64cputest`);
  `hosttest/c64memtest.asm`/`.sh` (`c64mem.inc` **and** `c64band.inc` under
  `SS != DS`); `tests/c64band` (`make c64bandbench`); `tools/c64prg.py`

### Present and greyed (SPEC.md §47 — the fact that greys it)

The full table with every caption is `docs/C64-SPEC.md` §11.2. In brief:
disk images and the flip list (no 1541 — a D64 needs the drive's directory
walk and the KERNAL serial traps); datasette (no tape); cartridges (the bank
maps carry the cartridge-less 7 of VICE's 32); printer formfeed (no printer
path in this OS); the monitor (30,000 lines of host C); drive resets;
**every Snapshot item** (a VSF carries every chip's state and this machine's
chips are not VICE's); display-state, decorations and menu-in-fullscreen (the
window is os8088's); **emulation speed and FPS** (nothing throttles here —
the machine delivers what the 8088 can and the status bar prints it); show
status bar (it is the window's bottom row); mouse grab (no 1351); allow
keyset joysticks (the keyset *is* the joystick here — shown **checked and
disabled**); Settings/Load/Save/Restore (no resources file); Help's manual,
command line, compile-time features and hotkeys; **every machine model but
C64 PAL** (one ROM set, one timing); **SID voices 2–3, waveforms, ADSR,
filters**; **colour on the glass, VGA included** (Decision 2 and SPEC.md
§5.4.1); **sprite collision registers** (the composer draws cells, not
pixels); **cycle-exact raster effects** (the VIC is serviced at raster-line
granularity); the Tape and drive-8 status fields; and **Power cycle / Paste /
Copy as CHORDS** (the caption is VICE's, the menu item is the route).

Dropped rather than greyed: the status bar's **Recording, Volume, CRT and
Mixer** widgets — the 336-px row holds 42 cells and message + Joysticks +
drive 8 + the two speed strings fill it.

### Absent

- Every other machine (C128, VIC20, PET, PLUS4, CBM-II, DTV, SCPU) — not a
  C64
- The cycle-exact `x64sc` machine — `x64`'s fast model is the reference
- True drive emulation (`vdrive/`, GCR, the 1541 CPU), fsdevice host
  directory, IEC, RS-232, userport, 256K/+60K hacks, cartridges — 150,000
  lines of host C with no surface here beyond the greyed items
- reSID / fastsid synthesis and the host audio thread — float maths and no
  PCM path from C
- The GTK3 settings tree (345 widget files), the resources file, netplay,
  event recording, the monitor — no surface here
- Light pen, 1351 mouse, paddles — no device
- NTSC timing — one model ships (greyed above); adding it is a constant, not
  a feature
- A 4bpp composer (`c64_band4`) — priced out by SPEC.md §5.4.1 before being
  written; returns only with a kernel ink/paper band slot
- **A per-jiffy device quantum, a control-transfer clock, a matrix-and-colour
  screen shadow, and the wave-1 stub size line** — each replaced by Decisions
  10–13 and 15, and none of them kept as a fallback

---

## Files

| file | holds | resident |
|---|---|---|
| `apps/c64/c64.c` | the translation unit's root: the GPL-2 + VICE attribution header citing `apps/c64/COPYING`, prototypes, the key ring, `os88_main` (window, the 5-menu set with `AM_NAME` `VICE`, `about_set`, `onwake` install, the RAM claim, the `C64.ROM` claim + `read_seg` with the refusal quoting the fact, **`os88_key_down` armed here**), `os88_paint`, `os88_onkey` (the vkm lookup, the down-list, the Alt hotkeys, the Alt+D latch, the chord table with the BIOS-loss comments), `os88_onclick`, **`os88_onfile`** (size refusal, then an `ovl_` helper), **`os88_onwake`** — the wall-slice driver, the once-per-wake key poll, the once-per-tick flush under the lock, adaptive-on-exhausted-only, the `READY.` check for autostart — `os88_worker` (the Exit self-close), the `#include`s in order | yes |
| `apps/c64/c64io.c` | the `$D000-$DFFF` register files and the cdecl read/write dispatch the core calls on its slow path; **the alarm scheduler and cycles-to-next-event**; VIC (`$D000-$D02E` mirrors, `$D012`/`$D019` semantics, the frame-register dirty flag), SID (register file, voice 1 → speaker), colour RAM nibbles, CIA1/CIA2 (timers, ICR flag/mask, PRA/PRB against the cached matrix, joystick bits, TOD), the IRQ and NMI lines, **the `$00`/`$01` port and the bank-map index** | yes |
| `apps/c64/c64kbd.c` | the 152-entry `gtk3_sym.vkm` table (`.data`), the cached 8×8 matrix and the 16-entry down-list with its bounded overflow, the once-per-wake rebuild, the scan-routed Ctrl+H/I/M, the Ctrl-held digit poll, the RESTORE NMI with the Esc read, the joystick keyset, the PETSCII↔ASCII tables for Copy/Paste | yes |
| `apps/c64/c64scr.c` | the dirty-page → cell-row mapping through the frame registers; the 8,000-byte 1bpp frame shadow; the flush (once per host tick): the `k = 1..24` shift test FIRST, then per-row compose and span compare, border fills only on a `$D020` change, the status row delta-draw, the 1:1/2× choice from the tier table, full and partial expose; the EGA-16 luminance table; the **dirty-pages-per-wake** counter | yes |
| `apps/c64/c64menu.c` | the five `struct os88_menu` tables and every item string with its `.vhk` caption, the `OS88_MENU_DIS` greying with its fact in a comment, the menu-set struct, the `oncmd` dispatcher (two compares, then an `ovl_`) | yes |
| `apps/c64/c64cmd.c` | `ovl_*`: every menu command body — reset (CPU / power cycle: RAM pattern fill), Exit, Copy, Paste, warp/pause/advance-frame, swap joysticks, the greyed items' refusal toasts | no |
| `apps/c64/c64load.c` | `ovl_*`: the Smart-attach body called by the resident `os88_onfile` — the scratch claim, `read_seg`, `c64_zzcopy_in`, free — and the autostart state machine's setup | no |
| `apps/c64/c64about.c` | `ovl_about_show`: the 12-row About panel; the close and hit test are resident in `c64.c` (the `rcabout.c` split) | no |
| `apps/c64/c64cpu.inc` | the 6510 core (`docs/C64-SPEC.md` §4): the register plan, the 256-entry dispatch, per-opcode cycle costs, the boundary-guarded fetch and cached `ES`, the `$00`/`$01` special case, the dirty-bit `or` on every write, the scratch at `$FFC0-$FFF9`, the cdecl I/O and alarm calls with `ES` save/reload, IRQ/NMI entry, `C64_RUN_SLICE` / `C64_RUN_JAM` | yes |
| `apps/c64/c64mem.inc` | `rcmem.inc`'s shape: `c64_rd`/`c64_wr`/`c64_rd16`, `c64_rom_rd`, `c64_zcopy_in`/`out`, `c64_zzcopy_in`, `c64_zfill` — explicit `(segment, offset)` arguments, every `rep` marked `cc8086:allow`, `ES` restored, `DF` clear | yes |
| `apps/c64/c64band.inc` | the composers, 1bpp only, **span-taking**: `c64_band1(dst, first, last, ...)`, `c64_band_sprites`, `c64_band_x2` (8 or 16 rows), `c64_rowspan` (compose vs shadow → first/last differing cell), `c64_rowshift` (the `k`-row detector) — same segment-pair and `cc8086:allow` rules | yes |
| `apps/c64/c64.asm` | the shim: `CC_PKG_NAME 'C64'`, `CC_HAS_ONKEY`/`ONCLICK`/`ABOUT`/`ONWAKE`/`MENUS`/`FDLG`/`WORKER`/`OVL`, `CC_ICON "c64/icon.inc"`, `%include cc/crt0.asm`, `c64.gen.asm`, then the three `.inc`s, `CC_IMAGE_END` | yes |
| `apps/c64/icon.inc` | the 16×16 1-bit breadbin drawn for this port (`runcpm/icon.inc` format) | yes |
| `apps/c64/COPYING` | VICE's GPL-2 text, verbatim (~18KB in the repo, not on the floppy); cited from every derived-file header, the disk's `README.TXT` and the release zip | — |
| `apps/c64/rom/` | **committed** (Decision 1): `kernal-901227-03.bin`, `basic-901226-01.bin`, `chargen-901225-01.bin` + `README.md` stating Commodore's copyright and the `CONTRIBUTING.md` §6 departure | — |
| `apps/c64/build.sh` | the host checks that stop the build: `c64uitest` (glass model, `c64ref` compare, cost table) and `c64memtest.sh`; `c64cputest` is NOT here — it is `make c64cputest`, minutes, like `rcz80test` | — |
| `apps/c64/hosttest/os88.h`, `c64uitest.c`, `c64memtest.asm`/`.sh`, `c64cputest.asm`/`.sh` | the stub SDK (grown in the SAME edit as every thunk the program touches: `os88_key_down`, `os88_snd_tone`, `os88_clip_*`, `os88_fullscreen`, `os88_gfx_scroll`); the UI harness; the two boot-sector harnesses | — |
| `tools/c64rom.py` | validates the three committed ROMs by SHA-256 and concatenates them into `build/c64-rom/C64.ROM` with the fixed layout; stamps `build/c64-rom.stamp`. **No network, no VICE tree** | — |
| `tools/c64ref.py` | the independent pixel-level reference compositor (Decision 21): C64 memory in → a 320×200 1bpp image out, from VIC-II documentation and VICE's `src/vicii/`, never from `c64band.inc` | — |
| `tools/c64prg.py` | a deterministic BASIC V2 tokeniser: a listing in → a `$0801` `.PRG` out (plus a raw-bytes mode for a hand-assembled poke loop); writes the fixtures onto a scratch image through `tools/os88disk.py` | — |
| `vm/xt-c64`, `vm/286-c64`, `vm/386-c64`; `Makefile`; `README.md`; `CLAUDE.md` | Decision 6's three machines and their targets, and the documentation rows | — |

---

## Budget

- resident image estimate: **39,000** (range 38,000–40,000)
- bss estimate: **13,600**
- overlay estimate: **6,000**
- **resident total 52,600 of 61,440** — 2,400 under SPEC.md §73.9's 55,000
  trigger

**Basis** (the full arithmetic is `docs/C64-SPEC.md` §13.2). Measured ratios,
not cword's 5.9 bytes/line: RUNCPM ships image 39,412 + `RUNCPM.OVL` 7,389
for 4,679 lines of C once ~9.6KB of `rcz80`/tables/movers, ~6KB of
crt0/thunks and ~1.5KB of strings and icon come out — **~6.3 bytes per line
of C**. Resident C ~3,050 lines × 6.3 ≈ 19.2KB; `c64cpu.inc` at `rcz80.inc`'s
measured ~9.6KB (a 6510 handler is not smaller than a Z80 one here: every
reader carries the bank test, every writer the two-range test **and the
dirty-bit `or`**, and there are eight addressing modes per ALU op);
`c64band.inc` + `c64mem.inc` ~1.9KB; crt0 + thunks ~6KB; **overlay loading
shims ~600, resident**; `.data` ~1.8KB (the 152-entry vkm table 608, the ×2
lookup 512, the cycle table 256, the seven bank maps 112, luminance 48, the
dirty-bit mask table 256); strings ~2.3KB; icon 128.

bss is dominated by the **8,000-byte 1bpp frame shadow** (Decision 13), plus
colour RAM 1,024, the composed and ×2 bands 1,600, sprite staging ~520, the
register files ~110, the cached matrix and down-list ~72, key ring / paste
feeder / status line ~370, device phase and span tables ~140, and two-word
counters, About and SmallerC statics and out-parameters ~1,700.

**The ROMs are 0 bytes of image.** Embedded they would have been
39,000 + 20,480 + 13,600 = **73,080 — refused on paper**, which is what made
the sidecar the design and not a preference.

**Heap at launch:** the 64KB RAM claim + the 20KB ROM claim, plus the 6KB OVL
claim on the first menu command and a transient claim the size of the `.PRG`
on Smart attach. Launch refuses quoting `os88_mem_largest_kb()` if either of
the first two fails, or naming `C64.ROM` if the file is not there.

**Reporting.** Five figures at every wave, separately: resident image, bss,
`C64.OVL`, resident shims, largest frame. **No wave-1 stub build size line**
(Decision 15) — the first honest line is the end of wave 2.

---

## API gaps

- **need:** key STATE for the level keyboard model, the joystick and
  `CTRL+digit` — `os88_onkey` delivers presses only
  - **slot:** `OSAPI_KEY_DOWN` (slot `0x03F0`, SPEC.md §9.7): `AL` = a make
    scancode, `CF` = down; every register kept; **asking arms it and the
    first ask clears the map**; advice, not oracle (`apps/arkanoid`,
    `apps/cyclone` use it). The kernel already tracks every break code
    (`kernel/mouse.inc:1438`, `kbd_track`, `KBD_MAPSZ` 16 = all 128 make
    codes)
  - **action:** add a thunk `int os88_key_down(int scan)` to
    `apps/cc/os88thunk.asm` + `apps/cc/os88.h` (`CC_T_A1`-shaped, CF → 1/0; a
    dozen lines) **and the `hosttest/os88.h` stub in the SAME edit**. Armed
    once in `os88_main`; polled **once per wake** for the down-list, the 5
    joystick codes, Ctrl and — only while Ctrl is down — the 10 digits:
    ≤ ~20 × 46.7 µs per **wake** (Decision 16)
- **need:** colour on the glass at the cost of a 1bpp band
  - **slot:** none exists — `gfx_blit1` reads no pen (SPEC.md §5.4.2 pins
    Set/Reset off at rest); `gfx_blit4` on VGA is the span writer at ~215 µs a
    run (SPEC.md §5.4.1, PERFORMANCE.md Set 44)
  - **action:** grey the feature in this port (Decision 2), stated with the
    fact; the kernel slot — a `blit1` variant taking ink and paper — is a
    follow-up, because it spends kernel headroom
    (`KERN_BUDGET`/`KERN_CODE_MAX` are decisions, not build fixes)
- **need:** a slice loop on the UI task without blocking, file slots legal,
  re-entered without a user event
  - **slot:** `OSAPI_WM_WAKE` (slot `0x0450`) / `OSAPI_WM_ONWAKE` (slot
    `0x0458`), wrapped, `CC_HAS_ONWAKE` — SPEC.md §74.1
  - **action:** none; use as RUNCPM does
- **need:** a host time base for the flush rate and the per-second figures
  - **slot:** `os88_ticks()` — the 18.2 Hz tick
  - **action:** none; the machine's own clock is emulated 6510 cycles
    (Decision 10). `OSAPI_WM_TIMER` stays unwrapped — the wake is the re-post
- **need:** the CPU tier that seeds the wall slice and the fullscreen tier
  table
  - **slot:** `OSAPI_CPU_INFO` (slot `0x0188`), wrapped
  - **action:** none
- **need:** fullscreen on Alt+D
  - **slot:** `OSAPI_FULLSCREEN` (slot `0x0110`, SPEC.md §11.2's latch),
    wrapped
  - **action:** none; **verify on a real BIOS** that the chord reaches
    `os88_onkey` as ascii 0 / scan `0x20` unconsumed — on `vm/386-c64` and
    `vm/xt-c64` under 86Box, not only under QEMU's SeaBIOS. That verification
    is manual evidence (Decision 21) and is not a gate
- **need:** a scroll moved, not redrawn
  - **slot:** `OSAPI_GFX_SCROLL` (slot `0x01F8`), wrapped; x1 and x2+1
    multiples of 8 — `wm_snap` puts the content x on a cell boundary; refuses
    when the clip does not contain the rect
  - **action:** none; the flush emits one on the `k`-row shift test and falls
    back to spans on −1, as `rcterm` does
- **need:** composed spans down in one call
  - **slot:** `OSAPI_GFX_BLIT1` (slot `0x0418`), wrapped, −1 refusal on
    `kern_small` → the font path
  - **action:** none; the glyph bytes come from the CHARGEN ROM in the claim
    or the RAM character set, not from `OSAPI_FONT_GLYPHS`
- **need:** read a `.PRG` into the C64's RAM at an arbitrary,
  non-512-aligned address; read `C64.ROM` into its claim
  - **slot:** `OSAPI_FILE_READ_AT` (slot `0x0358`) exists unwrapped;
    `os88_file_read_seg` needs a 512-aligned base
  - **action:** write it in the package — claim a scratch of
    `ceil(size/1KB)`, `os88_file_read_seg` into it, `c64_zzcopy_in` into the
    RAM claim, `os88_mem_free`. The ROM claim is `read_seg`'d directly
    (20,480 is 512-aligned and starts at the claim's base). **No new slot**
- **need:** a self-close for Exit emulator (Alt+Q)
  - **slot:** `OSAPI_WM_CLOSE` (slot `0x0470`) exists unwrapped
  - **action:** keep the worker idiom (`CC_HAS_WORKER`) as RUNCPM does; a
    `WM_CLOSE` thunk is a follow-up, not required
- **need:** SID beyond one square wave
  - **slot:** `OSAPI_SND_FM` / the streaming path — deliberately not wrapped
    for C (driver verb protocols)
  - **action:** grey the feature; voice 1's gate and frequency →
    `os88_snd_tone` (slot `0x00E8`)

---

## Waves

Every `done_when` below is **automated evidence** (Decision 21): the host
harnesses, `make c64cputest`, `make c64bandbench`, and QEMU driven over QMP
with screendumps. **No `done_when` rests on 86Box.** The machines are
launched and read by a person, and what they say is *recorded* in
`docs/C64-SPEC.md` as a dated reading.

### Wave 1 — Window, chrome, menus from the source, the screen model, the harnesses, the build, the machine

- **`C64-SPEC` §1 and `C64-SPEC` §2 written FIRST** (LESSONS 1 and 11): the
  attribution, the ROM decision, and the authority table with every string
  transcribed. `apps/c64/COPYING` (VICE's GPL-2 text verbatim);
  `apps/c64/rom/` with the three binaries and its `README.md`;
  `tools/c64rom.py` producing `build/c64-rom/C64.ROM` against the SHA-256
  table
- `apps/c64/` skeleton with the GPL-2 + VICE attribution header in every file;
  the shim with `CC_HAS_OVL` from the first commit; Makefile:
  `$(eval $(call CC_PACKAGE,c64,c64,C64.OVL))`, `C64SRC`/`C64INC`/`C64HOST`
  prerequisites naming **every** included file (make cannot see through
  `#include`), the `build/c64.bin` dependency on `build/c64-rom.stamp`, the
  `build/c64.img` rule (`--verify`'d now; 720/360 in wave 5), phony targets
  `c64`, `c64disk`, `c64bandbench`, `c64cputest`, `386-c64`, the `cc-note`
  guard; `vm/386-c64/86box.cfg` = `vm/386-runcpm` with B: and uuid changed
- `os88_main`: window `VICE (C64)` authored 336 × (`TITLE_H` + 216 + 10 + 1)
  at (7,20) — content height `W_H − TITLE_H − 1` (LESSONS 13); `wm_snap`; the
  five menus with EVERY `uimachinemenu.c` string and every `hotkeys*.vhk`
  caption, `AM_NAME` `VICE`, greyed items wearing `OS88_MENU_DIS` with the
  fact in a comment; `about_set`; `onwake` installed; the 64KB RAM claim and
  the 20KB ROM claim + `read_seg` of `C64.ROM`; **`os88_key_down` armed here**
  (Decision 16)
- the data model: the seven bank maps as a static table, colour RAM, the
  VIC/CIA/SID register files, the **8,000-byte 1bpp frame shadow**, the
  dirty-page bitmap's C-side reader and the dirty-page → cell-row mapping
  (the core's setter arrives in wave 2; wave 1 drives the flush from a
  "dirty everything" and a "dirty these pages" test hook)
- `c64scr.c`'s flush over a STATIC screen (the boot screen's bytes poked by
  hand) with `c64band.inc`'s span composer, the `k = 1..24` shift test first,
  border fills only on a `$D020` change, the status row with the two speed
  strings as placeholders and the `ScrollLock for joystick` message path
- `tests/c64band` (`make c64bandbench`): `c64_band1` (text, bitmap,
  multicolour threshold), `c64_band_x2` at 8 and 16 rows, `c64_rowspan`,
  `c64_rowshift` — **per cell and per call, in µs**, on the icount harness;
  the tier table in `c64scr.c` is written FROM these numbers
- `tools/c64ref.py`; `hosttest/os88.h` + `c64uitest.c`: the glass model, the
  1bpp shadow audit, the **bit-for-bit compare against `c64ref`**, and the
  cost table **in milliseconds** for: full expose, one changed cell, one
  changed row, a `k`-row shift, a 25-row change that is not a shift, a border
  change, dirty pages per wake; `hosttest/c64memtest` for `c64mem.inc` **and**
  `c64band.inc` under `SS != DS` with an `ES` sentinel

**Files:** `docs/C64-SPEC.md`, `apps/c64/COPYING`, `apps/c64/rom/*`,
`apps/c64/c64.c`, `c64scr.c`, `c64menu.c`, `c64io.c` (register files only),
`c64kbd.c` (tables only), `c64band.inc`, `c64mem.inc`, `c64cpu.inc` (the
dispatch table and the entry/exit shell only), `c64.asm`, `icon.inc`,
`build.sh`, `hosttest/{os88.h, c64uitest.c, c64memtest.asm, c64memtest.sh}`,
`tools/c64rom.py`, `tools/c64ref.py`, `tests/c64band/*`,
`vm/386-c64/86box.cfg`, `Makefile`

**Done when:**
`make c64disk && make test TESTAPPS=build/c64.img` boots and a QEMU
screendump (`--crop 8,38,336,226 --zoom 3`) shows the `VICE (C64)` window
with a C64 boot screen composed from the CHARGEN ROM read out of `C64.ROM`
(the `**** COMMODORE 64 BASIC V2 ****` text poked into the matrix by hand,
white on black in 1bpp), the five menus drop with VICE's strings and the
greyed items checkerboarded on `VIDEO=cga`, and the bar reads `VICE`;
a `c64.img` rebuilt with `C64.ROM` removed refuses at launch naming the file
(screendump);
`c64uitest` passes and prints, **in milliseconds**: full expose, one changed
cell (composes 1 cell, 1 blit), one changed row (composes only its span), a
`k = 9` shift (**one** `gfx_scroll` + 9 composed rows), a 25-row non-shift
change, a `$D020`-only change (fills only, no bands), dirty pages per wake;
**`c64uitest`'s composed frame equals `tools/c64ref.py`'s bit for bit** for
standard text with the CHARGEN charset and with a RAM charset;
`c64memtest.sh` passes and its ES-not-restored negative control fails;
`make c64bandbench` prints per-cell and per-call µs and `c64scr.c`'s tier
table cites those numbers by value;
`python3 tools/checkdocs.py` is clean.
**No size line is quoted this wave** (Decision 15).

### Wave 2 — The 6510 core, the cycle clock, the alarm scheduler, the CIAs, the level keyboard — BASIC at `READY.`

- `c64cpu.inc` complete: every opcode `6510core.c` implements, BCD, **the
  per-opcode cycle table with page-cross and taken-branch penalties**, the
  `$00`/`$01` special case, the boundary word and `ES` re-evaluation on every
  crossing, the true slow fetch path for I/O, the cdecl calls out with `ES`
  save/reload, the dirty-bit `or` on every write, the scratch at
  `$FFC0-$FFF9`, IRQ/NMI/BRK entry, `C64_RUN_SLICE` / `C64_RUN_JAM`
- `hosttest/c64cputest.sh` — the **nine rows** of Decision 22 with a negative
  control on each; `make c64cputest`
- `c64io.c`: the alarm scheduler and cycles-to-next-event; CIA1/CIA2 timers,
  ICR, TOD on its own 50 Hz accumulator; the VIC raster line and frame-end
  alarms, `$D012`, the raster compare, `$D019`/`$D01A`; the IRQ and NMI lines
- `os88_onwake`: the wall slice (256..16,384 cycles, `os88_cpu()`-seeded,
  doubled after four inside one tick, halved across two, **only exhausted
  slices adapt**), every device phase retained across slices, the once-per-wake
  key poll, the once-per-tick flush under the lock, re-post only while running,
  the pause and warp flags
- the status bar's real figures from **two-word counters**: `% cpu` from
  emulated cycles ÷ 985,248, `fps` from emulated VIC frames; flushes/s as a
  harness counter only
- `c64kbd.c`: the 152-entry table, the cached matrix rebuilt from the
  down-list each wake, the bounded 16-entry overflow, the scan-routed
  Ctrl+H/I/M, Tab = C= on scan `0x0F`, the Ctrl-held digit poll, RESTORE with
  the Esc read; CIA1 PRA/PRB read against the cached matrix + joystick; the
  `os88_key_down` thunk added to `apps/cc/os88thunk.asm` + `apps/cc/os88.h` +
  `hosttest/os88.h` in **one** edit
- the power-on path: RAM pattern fill, reset vector, BASIC boots to `READY.`
  from the real ROMs; the flush shows what the KERNAL writes; the cursor
  blinks off CIA1 timer A
- **the first honest size line**: resident image / bss / `C64.OVL` / resident
  shims / largest frame

**Files:** `apps/c64/c64cpu.inc`, `c64io.c`, `c64kbd.c`, `c64.c`, `c64scr.c`,
`hosttest/{os88.h, c64cputest.asm, c64cputest.sh, c64uitest.c}`,
`apps/cc/os88thunk.asm`, `apps/cc/os88.h`, `Makefile`, `docs/C64-SPEC.md`

**Done when:**
`make c64cputest` passes **all nine rows** — Dormann functional and decimal,
the seven bank maps, `$0000`/`$0001`, fetches and accesses at every mapping
boundary including an instruction that straddles one, real I/O stub returns,
`ES`/`DS` restoration, IRQ/NMI entry, the illegal opcodes, and per-family
cycle totals against `6510core.c` — **and every negative control fails**;
a QEMU screendump shows the genuine boot screen reached by the real ROMs with
a blinking cursor;
`sendkey`ing `PRINT 2+2` prints `4` and `READY.`;
a `FOR` loop of 1000 `PRINT`s scrolls with the harness asserting **one
`gfx_scroll` per FLUSH** and `k` composed rows (never one scroll per printed
line), and one keystroke echo composing only its span;
holding a cursor key repeats at the KERNAL's own rate and Esc+PgUp warm-starts
to `READY.` (screendump sequence);
the harness asserts: `% cpu` is computed from emulated cycles (a scripted core
delivering exactly 985,248 cycles in a second prints `100`), `fps` from
emulated VIC frames (19,656 cycles a frame → 50.1 at 100%), and flushes/s is a
separate counter; a two-word counter fed 100,000 cycles a tick for 60 ticks
does not lap;
the harness asserts the slice adaptation: 100 keystrokes of ordinary typing,
each ending its slice early, leave the wall budget at its seed, while four
exhausted slices inside one tick double it;
the harness asserts the keyboard rules: the map is armed before the first
`os88_onkey`, a press survives at least one slice, a shared SHIFT bit is not
cleared by another key's release, and the 17th simultaneous key is dropped
rather than written past the end;
the **five-figure size line** is in the wave report against 61,440 and
SPEC.md §73.9's 55,000 trigger.

### Wave 3 — The commands: reset, exit, pause, warp, fullscreen, copy/paste, joystick, SID

- `c64cmd.c` as `ovl_*`: Reset machine CPU (Alt+F9), Power cycle (menu; Alt+F12
  where the BIOS passes it), Exit emulator (Alt+Q, the worker self-close),
  Pause (Alt+P, shown checked by text swap), Advance frame (Alt+Shift+P: run
  to the next VIC frame end), Warp (Alt+W: flush every 9 ticks), Swap
  joysticks (Alt+J), Copy (screen PETSCII → ASCII → `os88_clip_put`), Paste
  (`os88_clip_get` → the `$0277` feeder, 10 characters a jiffy)
- **the first `ovl_` call from the first wake** (the `.OVL` cannot load from
  `os88_main`); `Unable to load C64.OVL.` printed in the status row as well as
  toasted (the toast is under a fullscreen window)
- Alt+D fullscreen via `os88_fullscreen` with `c64_band_x2` where the tier
  table allows; 1:1 centred otherwise; border fill of the rest of the screen
- the joystick: port 2 on arrows + Ctrl from the cached key state; the two
  5-dot indicators delta-drawn; the warp and pause LED dots
- SID voice 1 → `os88_snd_tone` per slice on change; the rest greyed

**Files:** `apps/c64/c64cmd.c`, `c64.c`, `c64kbd.c`, `c64scr.c`,
`c64band.inc`, `hosttest/{os88.h, c64uitest.c}`, `docs/C64-SPEC.md`

**Done when:**
screendumps: Alt+D on `VIDEO=cga` (`mouse.py --screen 640x200`) shows the C64
screen filling 640×200 exactly, and on VGA 640×400 centred;
the harness asserts **Alt+D costs 0 flush calls either way** (SPEC.md §74.2's
zero) and prices a fullscreen frame in ms;
a typed `FOR` loop pauses on Alt+P and resumes (two screendumps), and Advance
frame steps exactly one VIC frame (the harness reads the emulated frame
counter);
Copy of the boot screen, read back from the clipboard by Note Pad, shows the
ASCII text (screendump);
a Paste of 40 characters composes at most 40 spans and the harness prints its
milliseconds;
an image with `C64.OVL` removed shows `Unable to load C64.OVL.` in the status
row (screendump), and every menu command then refuses politely;
`cc8086.py`'s counters show the `ovl_` functions moved, and the **five-figure**
size line — resident shims counted — is quoted.

### Wave 4 — Programs: Smart attach, autostart, bitmap modes and sprites

- `os88_onfile` **resident** (Decision 20): the size refusal, then an
  already-loaded `ovl_` helper in `c64load.c` — transient claim, `read_seg`,
  `c64_zzcopy_in` at the 2-byte load address, free
- autostart: reset, the 4-line `READY.` check resident in the slice driver,
  `mem_set_basic_text(start, end)` unconditionally, `RUN\r` into `$0277`/`$C6`
- `tools/c64prg.py`: the BASIC tokeniser writing the fixtures — a `PRINT`-loop
  program, a hires plotter (`POKE 53265`, bitmap clear, plot loop), a
  multicolour-bitmap fixture, a custom-charset fixture and a sprite mover —
  onto a scratch copy of `c64.img` through `tools/os88disk.py`; the harness
  pokes the same bytes into its RAM array directly for the cost rows
- VIC modes in `c64band.inc`: hires bitmap (the cell transpose), multicolour
  text/bitmap and extended background by luminance threshold, sprites overlaid
  per row with priority and x/y expansion; a `$D018`/`$D011` change dirties
  the frame, a `$D020` change is four fills
- the tier table in one place (`c64scr.c`) and the 1bpp-everywhere,
  collision-answers-0 and raster-line-granularity facts stated in the
  Preferences menu text and in `docs/C64-SPEC.md`

**Files:** `apps/c64/c64load.c`, `c64.c`, `c64band.inc`, `c64scr.c`,
`c64io.c`, `hosttest/c64uitest.c`, `tools/c64prg.py`, `tools/c64ref.py`,
`Makefile`, `docs/C64-SPEC.md`

**Done when:**
a `.PRG` written by `tools/c64prg.py` onto a scratch image, loaded via Smart
attach, autostarts to its output on the glass in QEMU (screendump);
a `.PRG` whose end passes `$FFFF` is refused with the fact and the disk is
never read (screendump + the harness asserting zero file calls);
**the harness's composed frame equals `tools/c64ref.py`'s bit for bit** for:
hires bitmap, multicolour bitmap, multicolour text, extended-background text,
a RAM character set, and sprites — enabled, priority both ways, x-expanded,
y-expanded, and two overlapping;
the hires-bitmap fixture reaches the glass in QEMU (screendump) and the sprite
fixture moves;
one sprite moved one cell composes only the spans it touched (harness);
`make c64bandbench` prices a full bitmap frame and a full multicolour frame,
and `docs/C64-SPEC.md` §9.7's table is filled in from those milliseconds;
the five-figure size line is quoted.

### Wave 5 — About, disks, the other two machines, docs, the closing re-measure

- `c64about.c`: the 12-row About panel, modal, the machine paused while it is
  up, its close as damage not repaint, reached from Help > About VICE... and
  the kernel's About alike
- the remaining greyed items' refusal toasts each naming their fact;
  Preferences > Allow keyset joysticks shown checked and disabled
- disk images `c64720.img` and `c64360.img` beside `c64.img`, each
  `--verify`'d: `C64.O88`, `C64.OVL`, `C64.ROM`, `README.TXT` (the licence
  pointer + the ROM copyright); the `allapps` lines (the `C64\` folder); the
  `release-os8088` skill told that `apps/c64/COPYING` rides in the zip
- **`vm/xt-c64` and `vm/286-c64`** (Decision 6) with their `xt-c64` and
  `286-c64` targets; `README.md`'s command list and machine table and
  `CLAUDE.md`'s machine list updated for all three
- `docs/C64-SPEC.md` completed: every measured number re-measured against the
  last build, the chord table filled in from the machines, the tier table,
  every greyed item's fact; `docs/C64-PORT-PLAN.md` gains each wave's measured
  paragraph; `README.md` row; `LESSONS.md` "What the C64 port added";
  PERFORMANCE.md's new Set from `tests/c64band`; `checkdocs` clean

**Files:** `apps/c64/c64about.c`, `c64menu.c`, `c64cmd.c`, `Makefile`,
`vm/xt-c64/86box.cfg`, `vm/286-c64/86box.cfg`, `README.md`, `CLAUDE.md`,
`PERFORMANCE.md`, `docs/C64-SPEC.md`, `docs/C64-PORT-PLAN.md`,
`.claude/skills/port-to-os8088/LESSONS.md`,
`.claude/skills/release-os8088/SKILL.md`

**Done when:**
About VICE... opens on VGA, on `VIDEO=cga` with OK inside the panel and the
panel inside the content box, and on Hercules (`tools/hercshot.py`), and its
close costs one fill plus the spans under it (harness);
all three images pass `os88disk.py --verify` and the 360KB one leaves clusters
free (the verify line quoted);
`make allapps` carries `C64\` and the package launches from that disk in QEMU
to `READY.` (screendump);
`make test-full` passes;
`python3 tools/checkdocs.py` is clean;
every number in `docs/C64-SPEC.md` is re-measured against the last build and
the final five-figure size line is recorded.

**Recorded, not gated** (manual evidence, Decision 21): `make 386-c64`,
`make 286-c64` and `make xt-c64` launched by hand, and what they show —
the boot to `READY.`, the `% cpu` and `fps` figures, the chord table of
`docs/C64-SPEC.md` §7.5, the look of a scroll and the feel of keystroke
latency — written into `docs/C64-SPEC.md` as dated readings with the machine
named.

---

## Verification

**Automated — every gate in this plan rests only on these.**

- **`apps/c64/hosttest/c64uitest.c`** (`cc -O1 -w -I apps/c64/hosttest -I
  apps/c64`, run by `build.sh` before every build): the whole program over a
  stub `os88.h` whose `gfx_blit1` records the pixels, `gfx_scroll` moves the
  model and refuses on a flag (to test the fallback), and `gfx_fill` whitens.
  After every step it asserts glass == model == 1bpp shadow. The core is
  SCRIPTED (it writes to the RAM array, sets dirty bits and answers
  SLICE/JAM) while `_c64_io_rd`/`_c64_io_wr` are the **real `c64io.c`**, so
  the alarm path, the level keyboard (press, poll, release), RUN/STOP+RESTORE,
  the autostart state machine, Copy/Paste and the About panel are driven key by
  key. It prints the cost table **in milliseconds** and the dirty-pages-per-wake
  counter, and asserts the rows of `docs/C64-SPEC.md` §9.7
- **`tools/c64ref.py`** — the independent pixel-level reference compositor
  (Decision 21). Written from VIC-II documentation and VICE's `src/vicii/`,
  **never** from `c64band.inc`; renders the same C64 memory to a 320×200 1bpp
  image; compared **bit for bit** against the package's composed frame. This
  is what validates hires bitmap, multicolour, custom character sets, the cell
  transpose and sprite priority and expansion — a cell-identity glass model
  provably cannot
- **`hosttest/c64cputest.sh`** (`make c64cputest`, minutes, not in
  `build.sh`): the shipping `c64cpu.inc` in a boot sector in raw QEMU under
  `SS != DS`, running Decision 22's nine rows with a negative control on each.
  Klaus Dormann's `6502_functional_test.bin` and `6502_decimal_test.bin` are
  fetched at pinned SHA-256s and never committed
- **`hosttest/c64memtest.sh`**: `c64mem.inc` **and** `c64band.inc`'s string
  loops under `SS != DS` with an `ES` sentinel and an ES-not-restored negative
  control (the `rcmemtest` precedent)
- **`tests/c64band`** (`make c64bandbench`): the icount harness pricing
  `c64_band1` (text, bitmap, threshold), `c64_band_x2` at 8 and 16 rows,
  `c64_rowspan` and `c64_rowshift`, **per cell and per call in µs**. The
  numbers go into the tier table, `docs/C64-SPEC.md` §9.7 and PERFORMANCE.md
  as a new Set
- **QMP recipe:** `python3 tools/qmp.py build/qmp.sock quit; rm -f
  build/qmp.sock; make c64disk && make test TESTAPPS=build/c64.img`; launch =
  double-click Disk B then the C64 icon as ONE driver (two presses inside 9
  ticks, LESSONS 10); `sendkey` the BASIC lines; `tools/shot.py
  build/qmp.sock out.png --crop 8,38,336,226 --zoom 3` for the screen and
  `--zoom 8` for the status row and a greyed menu item; `VIDEO=cga` with
  `mouse.py --screen 640x200` for the 1bpp fit and fullscreen; `VIDEO=herc
  HERCSEG=0x7000` + `tools/hercshot.py` for Hercules; Smart-attach tests write
  into a scratch copy of the image that `tools/c64prg.py` populated
- **`make test-full`** as the pre-merge gate; `tools/checkdocs.py` on every
  citation; `os88disk.py --verify` on all three images in the recipe

**Manual — recorded, never a gate.**

- The three 86Box machines (Decision 6): `make xt-c64`, `make 286-c64`,
  `make 386-c64`. Each is a copy of a machine that has booted, with the B:
  image and the uuid changed and nothing else; 86Box rewrites the cfg on exit,
  so `git checkout` it before committing and never commit `nvr/`; `RESET=1` if
  the CMOS is stale
- What is read off them: the boot to `READY.`, the `% cpu` and `fps` figures
  on each machine, **the chord table** (Alt+D, Alt+F9, Alt+F12, Alt+Insert,
  Alt+Delete, Ctrl+letter, Ctrl+digit, Alt+Q, Alt+J — arrives / does not),
  the look of a scroll, the feel of keystroke latency. QEMU's SeaBIOS passes
  enhanced codes an AT BIOS drops, so the chord table cannot be taken there
- Each reading goes into `docs/C64-SPEC.md` with its date and machine

---

## Risks

- **The C64 is MONOCHROME on every adapter in this port, VGA included** — a
  visible loss the user should expect. It is a priced fact (SPEC.md §5.4.1's
  span writer at ~215 µs a run, PERFORMANCE.md Set 44), not a guess, and the
  only way to colour at 1bpp cost is a kernel ink/paper band slot that spends
  kernel headroom the memory notes put at ~512 bytes. A package-side answer
  does not exist.
- **The 6510 core's speed on a 4.77 MHz 8088 is unknown until measured.** If
  it lands under ~8% of a real C64, BASIC is usable and games are not, and
  the honest posture is the number in the status bar — not a faster core
  promised later. The boundary-guarded fetch (Decision 9) is safer than the
  draft's bias but costs one `cmp` per instruction fetch, and **the dirty-bit
  `or` costs ~5 instructions on every store** (Decision 13): both are stated
  per-instruction taxes paid for correctness, and the first core build is
  where their real price appears.
- **The flush is no longer a fixed per-tick cost, but the dirty bitmap can be
  pessimistic.** A program that writes one byte per page across the bitmap
  dirties every row; a BASIC program that scrolls dirties the whole matrix by
  definition. What replaced the 2,000-byte compare is a *bounded* cost, not a
  free one, and the bench's milliseconds are what decide whether the 8086 tier
  flushes every tick or every other tick (a stated constant, not a cut in what
  is drawn).
- **Raster-line granularity, not cycle granularity.** The alarm model gets the
  interrupt COUNT and ORDER right and the within-line phase wrong, so music
  players timing on a CIA timer and ordinary raster splits work while
  mid-line colour changes, FLD and FLI do not. BASIC, the KERNAL, text games
  and most BASIC-era PRGs are fine; demos are not.
- **The key-state map is "advice, not oracle"** (SPEC.md §9.7): a key whose
  break code the ISR missed stays in the matrix until its next press. The
  per-wake rebuild bounds it, but a stuck key in a game is possible and must
  be stated.
- **Ctrl is both joystick fire and the CTRL key.** A game reading CTRL+letter
  from the matrix while the joystick fires sees both — which is what a real
  machine with both plugged in does — but a BASIC user holding Ctrl to type a
  colour code also fires port 2.
- **Three VICE chords (Alt+F12, Alt+Insert, Alt+Delete) do not exist on the
  target class.** The menu item is the route and the chord table taken from
  86Box is what `docs/C64-SPEC.md` §7.5 says; a reader who tests only under
  QEMU will file the captions as broken.
- **The 360KB disk:** `C64.O88` (~39KB) + `C64.OVL` (~6KB) + `C64.ROM` (20KB)
  + `README.TXT` is ~66KB of a 360KB disk — comfortable, but this does **not**
  ride on the XT apps disk (a folder of its own on `apps-all.img`). And
  `LOAD"*",8` answers `?DEVICE NOT PRESENT`, which is the honest machine and
  must be stated so nobody files it as a bug.
- **Licence:** GPL-2-or-later makes `apps/c64` GPL; the rest of the tree is
  not, and the PR body, `docs/C64-SPEC.md` §1.2 and the file headers must say
  so. `apps/c64/COPYING` is the text the licence requires to accompany copies.
  **The Commodore ROMs are neither GPL nor ours** — they are committed under
  Decision 1 as a stated departure from `CONTRIBUTING.md` §6, with
  `apps/c64/rom/README.md` and the About box saying whose they are.
- **The core's scratch lives in the C64's own top 64 bytes** (LESSONS 13's TCG
  finding forces it out of bss). Two stated deviations follow: a read of
  `$FFC0-$FFF9` in an all-RAM map reads the scratch, and a write there is
  dropped. The CPU harness holds a case for each, but a program that stashes
  data under the KERNAL at the very top will see it.
- **The 640×200 adapters:** the window is 216 + 10 rows of content + title,
  taller than CGA's ~136-row framed content — the kernel clamps and the status
  row is off-glass in a frame on CGA. Fullscreen is the CGA answer (exactly
  640×200 at 2×) and the About panel stays 12 rows.
- **The budget's 6.3 bytes/line and `rcz80`'s ~9.6KB are RUNCPM's
  measurements, not this port's.** With the wave-1 stub line withdrawn
  (Decision 15), nothing is measured until the end of wave 2 — 52,600 on paper
  leaves ~2,400 to the trigger and ~8,800 to the cap for it to be wrong by,
  and the overlay is a frequency split ready from the first commit.
- **`tools/c64ref.py` is a second implementation and can be wrong in the same
  way as the first** if it is written by reading `c64band.inc`. It must be
  written from the VIC-II documentation and VICE's `src/vicii/`, and a
  deliberate one-bit defect injected into the composer must make it fail.
- **One hand in the translation unit at a time**, and the harness's stub
  `os88.h` grows with every thunk the program touches — each new thunk's stub
  lands in the same edit.
