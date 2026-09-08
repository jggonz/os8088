# C64 port plan — VICE 3.10's `x64` as an os8088 C package

**Waves 1-3 are built; wave 4 (autostart, bitmap and multicolour modes,
sprites, `tools/c64prg.py`) never landed and its `done_when` is unmet.**
docs/C64-SPEC.md describes what ships.

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
port, the CIA and VIC register files, `gtk3_sym.vkm`, the VIC-II's own
luminance ladder (`vicii-color.c`),
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
keyboard model (`keyboard.c`); the 16 colours and their luminance ladder
(`vicii-color.c`, `vicii_colors_6569r5` — NOT `vice.vpl`, C64-SPEC §2); the window title
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
  both from two-word counters — delta-drawn, warp and pause as two labelled
  lamps `W`/`P`
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

**What wave 1 measured, and what it changed** (2026-08-21). Six things the
plan could not know, each found by a harness or a screendump and each written
back into `docs/C64-SPEC.md` rather than left in the code:

1. **A 256-page dirty bitmap cannot reach `C64-SPEC` §9.7's "one changed cell
   composes one cell"** — a page is 6.4 character rows. The core now keeps a
   WRITE WINDOW beside the bitmap (lowest and highest address written), four
   instructions on the write path, and one changed cell costs **3.7 ms**
   instead of 28. `C64-SPEC` §9.2.
2. **The shift test is a SIGNATURE test on the row's sources, and it is a
   hint.** Composing 25 rows to discover a scroll is the cost the scroll
   exists to avoid; a collision is harmless because the span compare still
   runs against the moved shadow. Its threshold is 20 dirty rows, and 8 was
   measured wrong: a one-row change found a spurious match and cost 1 scroll
   plus 24 blits where 1 blit was the answer. `C64-SPEC` §9.4.
3. **Re-signing all 25 rows at the end of every flush cost 25.8 ms** — half a
   host tick, on every flush, for a keystroke that touched one row. Only
   recomposed rows are re-signed now. Found by the harness's cost table.
4. **`MENU_POPMAX` is 11 items and `MENU_MAXCH` is 24 glyphs.** VICE's File
   menu is 15 items with four submenus; the folds, and which items lose their
   `.vhk` caption to the 24-glyph cell, are `C64-SPEC` §11.1's three rules.
5. **42 cells do not hold VICE's status bar.** The two speed strings are 24 of
   them; `Tape:` and drive 8's track counter are dropped with the arithmetic,
   and a message owns the whole row while it is up. `C64-SPEC` §10.1 and
   `C64-SPEC` §10.3.
6. **A refused launch's toast is the KERNEL's.** `os88_main` returning 0 put
   `Load failed` on the glass over the package's own `no C64.ROM`. The window
   comes up and says which file is missing instead — LESSONS.md 13's RUNCPM
   shape. `C64-SPEC` §1.4.

And two numbers the bench answered: **a composed cell is 184 µs and the call
floor 175 µs**, so a changed row is 11.9 ms and a full 25-row repaint ~270 ms
— four host ticks, which is why the `CPU_8086` tier flushes every other tick.
`C64-SPEC` §9.7 carries the whole table and `apps/c64/c64scr.c`'s constants
cite it by value.

**What wave 1's REVIEW changed** (2026-08-21, same day). Four blockers and
eight majors, every one of them a thing that assembled, booted and was wrong.
Each is written back into `docs/C64-SPEC.md` beside the design it corrects:

1. **`ovl_cmd` took the gfx lock**, from `os88_oncmd`, which the kernel
   dispatches with it already held — a non-recursive spin standing on itself,
   `kernel/ui.inc:1997`'s "no beep, no watchdog, no recovery". And the
   `os88_task_spawn` test was **inverted**, latching `C64_ST_DEAD` on the
   refusal the SDK calls normal and transient. Both passed the host harness,
   because the harness modelled the lock as two empty functions and spawn as
   `return 1`. **The harness now keeps `lock_depth`**, asserts every drawing
   stub is at exactly 1, models spawn's real 0/−1 with a refusal control, and
   drives File > Exit emulator under the lock — `rcuitest.c`'s shape.
   `C64-SPEC` §14.5.
2. **The write window was taken over the whole address space**, which is
   exact while `c64_poke_boot` is the only writer and useless from the first
   slice of a core: a JSR writes `$01xx`, a BASIC statement zero page and
   `$0800+`, so the window spans every matrix row and the mechanism
   degenerates to "recompose 40 cells". It is now taken over a **watch
   range** the C writes from `c64_frame_regs`, and the dirty ROWS come from
   the window rather than from 6.4-row pages. `C64-SPEC` §9.2.
3. **`c64_dirty_all` also FORCED**, so every write to `$D011`, `$D016`,
   `$D018` or `$DD00` was 25 forced full-width blits — ~234 ms — with the
   frame compare switched off, on registers a raster IRQ writes 50 times a
   second and the KERNAL's serial bit-banging touches per bit. The flags are
   split (sources changed / glass unknown), the registers are guarded by
   value and `$DD00` by its two bank bits. `C64-SPEC` §9.3.
4. **The shift test compared 25 rows and the flush only signed `nrows`**, so
   on any window `wm_fit` clamped — the CGA XT, the target — no scroll could
   ever be detected. And **the status row was at a fixed 216 from the top**,
   which a 200-line desktop cannot give: the row carrying `C64-SPEC` §1.4's
   permanent fact was off the glass. The window now asks `os88_video()` and the flush
   anchors the row to the live bottom. `C64-SPEC` §9.1, §9.4.
5. **`c64_say` set `c64_st_dirty` and not `c64_dirty_any`**, and the wake's
   flush is gated on the latter — so no message written from a menu command
   or a Smart attach ever reached the glass. `C64-SPEC` §10.1.
6. **Every row signature was taken twice on the scroll path** (25.8 ms), the
   About panel's close cost a full-screen repaint where its own rect was the
   answer, and the panel double-drew the rows under it on an expose.
   `C64-SPEC` §9.4, §12.
7. **Fidelity**: the About row said `1bpp, no drive` — how the build renders,
   which LESSONS 8 puts in the SPEC and the greyed items; the status row was
   in the REVERSE of `uistatusbar.c:2816`'s order; six `UI_MENU_TYPE_ITEM_CHECK`
   items showed no state at all while a comment claimed one was "shown
   CHECKED"; and Copy, Paste and Advance frame were live items that only
   toasted a refusal. `C64-SPEC` §10.1, §11.1, §12.

Two numbers moved as a result, and `C64-SPEC` §9.7's table is re-taken: a
`k = 9` shift **281.7 → 256.0 ms**, and a full expose **271.6 → 266.3 ms**
(ten dot fills on the status row became three `blit1` bands). Two new rows
gate what the review found: **eight `$D011` writes that change nothing draw
nothing**, and **a `$D016` change that composes the same picture blits
nothing**.

**What wave 1's SECOND review pass changed** (2026-08-21, same day). Nine
majors and six minors, and the three that matter most were each a thing the
first pass's own fixes had made reachable:

1. **Fullscreen was live and unimplemented.** `c64_flush` clamped its width to
   336 and anchored the screen at `org.x + 8`, while `kernel/wm.inc:7295`
   records that a `WF_FULL` window's frame is filled white *with no opt-out* —
   so Alt+D on a 640×480 desktop put a 320×200 picture in the corner of a
   white screen with a 336-pixel status strip under it, and nothing read
   `c64_full` at all. The geometry is now one function (`c64_geom`), the
   screen is CENTRED with its left edge on a multiple of 8 (`gfx_scroll`
   refuses anything else), and the border and status row fill the whole box.
   `C64-SPEC` §9.8 states that wave 1 does not MAGNIFY, with `c64_band_x2`'s
   20.19 ms per eight rows as the fact. None of the first pass's 24
   screendumps was a fullscreen one, which is how it survived.
2. **`OSAPI_FULLSCREEN` was paid for twice.** It repaints the window whole,
   synchronously, in both directions (`wm_fullscreen` → `wm_raise` `AL = 1`),
   so the `c64_sh_inval()` on its success arm threw away a shadow the kernel
   had just made true: 25 bands, ~300 ms, four host ticks of double-draw.
   `apps/runcpm/runcpm.c:1086-1095` records the identical defect in its own
   words. The harness now models the nested repaint and gates it.
3. **The cost model priced no text.** `font_run`/`font_str` were counted and
   left out of the sum, so the About panel's 160 glyph cells read as 0.0 ms
   against PERFORMANCE.md's ~153. Every row of §9.7 that draws a string is
   re-taken, and the panel is now redrawn only when the damage rect reaches
   it (**16.5 ms** for an expose that misses it, against 322.0 for one that
   does not).

And six more: the write window is now INTERSECTED with the dirty-page bitmap,
so two pokes in distant rows cost 13 rows and 125.6 ms instead of 25 and 299
(`C64-SPEC` §9.2); `ovl_about_show`'s STATUS is the `c64_abt` latch, so a
refused overlay load no longer parks the slice driver with nothing on the
glass; the luminance table is the VIC-II's own ladder
(`vicii-color.c:441`'s `vicii_colors_6569r5`, which is what VICE compiles) and
not `vice.vpl`'s, which VICE does not read — nine levels shared in seven
pairs, so the mono glass stops inventing contrast the machine does not show;
the warp and pause LEDs are labelled lamps `W`/`P` rather than two unlabelled
specks, with VICE's real LED row cited; `apps/c64/COPYING` ships **on the
floppy**, which `README.TXT` already claimed; and three menu heads lost
captions belonging to actions inside their submenus (`Attach disk image`,
`Flip list`, `Printer/plotter`), with `Flip list` and two milestone items
folding into their sections by rule 1 — File is 10 items and Snapshot 8.

The minors: the reset command no longer FORCES (`c64_poke_boot`'s
`c64_dirty_all` is the correct call); the About panel is snapped to the cell
grid so the hold rows are exact at both ends; the message deadline compares a
DIFFERENCE, not two 16-bit tick values; `c64_dirty_all` no longer dirties the
border and `c64_lum_update` raises it only when the border's level flips;
`c64_status` is called from every flush so its delta is reachable from the
product (`c64_st_dirty` is gone); and the C's span pair is `c64_cspf`/
`c64_cspl`, because `c64band.inc` already owns `c64_spf`/`c64_spl`.

### Wave 2 — The 6510 core, the cycle clock, the alarm scheduler, the CIAs, the level keyboard — BASIC at `READY.`

- `c64cpu.inc` complete: every opcode `6510core.c` implements, BCD, **the
  per-opcode cycle table with page-cross and taken-branch penalties**, the
  `$00`/`$01` special case, the boundary word and `ES` re-evaluation on every
  crossing, the true slow fetch path for I/O, the cdecl calls out with `ES`
  save/reload, the dirty-bit `or` on every write, the scratch at
  `$FFC0-$FFF9`, IRQ/NMI/BRK entry, `C64_RUN_SLICE` / `C64_RUN_JAM`
- `hosttest/c64cputest.sh` — the **twelve rows** of Decision 22 with a negative
  control on each; `make c64cputest` (the decision said nine: decimal
  `ADC`/`SBC` came out of Dormann's row into one of its own, `C64-SPEC` §4.6)
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
`c64cmd.c`, `c64mem.inc`, `hosttest/{os88.h, c64cputest.asm, c64cputest.sh,
c64memtest.asm, c64uitest.c}`, `tools/c64dec.py`, `apps/cc/os88thunk.asm`,
`apps/cc/os88.h`, `Makefile`, `docs/C64-SPEC.md`

**Done when:**
`make c64cputest` passes **all twelve rows** — Dormann functional and decimal,
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
holding a key the KERNAL repeats — the space bar or INST/DEL — repeats at its
own rate, and Esc+PgUp warm-starts to `READY.` (screendump sequence). **The
cursor keys are no longer that key**: the fix pass made the keyset consume
them, which is what VICE does with a keyset on port 2 (`C64-SPEC` §8), so they
drive the stick and do not type;
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

**What wave 2 measured, and what it changed** (2026-08-22). BASIC boots to
`READY.` off the real ROMs, `PRINT 2+2` prints `4`, a `FOR` loop of 1,000
`PRINT`s scrolls, RUN/STOP breaks it and RUN/STOP+RESTORE warm-starts.
**The first honest size line, after the wave's three fix passes: resident
image 36,434 + bss 11,346 = 47,780 of 61,440, `C64.OVL` 1,424, 24 resident
shims, largest frame 32 bytes** — 7,220 under SPEC.md §73.9's 55,000 trigger,
and smaller than the plan's estimate on all three figures (`C64-SPEC` §13.0
carries a per-pass delta, and says which two of the plan's figures moved and
why).

Nine things the plan could not know, each written back into
`docs/C64-SPEC.md` rather than left in the code:

1. **The boundary guard is a RANGE, not a ceiling.** `C64-SPEC` §4.3's one `cmp` per
   fetch is only correct while PC increases: a `JMP` back from `$E000` to
   `$0400` leaves PC BELOW the biased region and a one-compare guard fetches
   the KERNAL's bias over RAM. Two compares cost two instructions on the most
   frequent path in the machine; re-biasing on every control transfer, which
   is the other way to be correct, costs twenty. `C64-SPEC` §4.3.
2. **`and di,0x01FF` is the wrong stack wrap and BASIC does not notice.**
   `0x0200 & 0x01FF` is `0x0000`, not `0x0100`, so a pull with `S = $FF` read
   byte zero of the address space and left every later push writing at
   `$0000` downwards. The machine booted, ran BASIC and answered `PRINT 2+2`
   with that defect in it. **Klaus Dormann's "proper stack wrap around" test
   at `$0D89` is what caught it** — which is the whole argument for the gate
   being twelve rows and a fetched fixture rather than a boot screenshot.
   `C64-SPEC` §4.1.
3. **The countdown is a SIGNED word, so 32,767 is the cap on every budget.**
   A 60,000-cycle pass arrives negative and the core expires before its first
   fetch, which reads exactly like a test that will not start. `C64-SPEC` §4.2.
4. **The alarm's service is a RETURN to the C, not a call out of the core**,
   and **the end of a raster LINE is not an alarm**: `$D012` is computed from
   the cycle counter when it is read. The first would have cost what the
   entry/exit shell costs anyway; the second would have ended the run 312
   times a frame for a register most programs never touch. `C64-SPEC` §4.4
   and `C64-SPEC` §5.2.
5. **`os88_wm_onwake` INSTALLS the handler; it does not post a wake.** The
   machine sat at its reset vector with `0% cpu` on the status row until the
   user pressed a key, and then booted perfectly - which reads as a reset bug
   rather than a wake loop that was never started. `os88_main` posts the first
   kick (`build/port-shots/wave2-03-launch.png` is the defect).
6. **`% cpu` needs a cap that keeps an `int` positive and nothing tighter.**
   A "320 % of a real 6510 and nothing will see it" clamp clipped an honest
   figure into a wrong one that still looked like a number — `1777% cpu`
   beside `1195.1 fps`, two figures that could not both be true. Under QEMU
   this core runs at some **thousands** of per cent. `C64-SPEC` §10.2.
7. **Dormann's decimal test does not exist as a binary.** The project
   publishes the functional test's binary and the 65C02 one and nothing else,
   so `tools/c64dec.py` computes the reference for all 262,144 decimal cases
   in Python from the documented NMOS rules — the posture `tools/c64ref.py`
   already takes toward the composer. `C64-SPEC` §4.6 row 10.
8. **A negative control that reads a stale answer passes.** Two of the ten
   controls "worked" because the row's output byte still held what the
   POSITIVE run had written there. Every row clears its answer bytes first.
9. **A 64KB fixture lands on top of the core's scratch.** Dormann's image
   overwrites `$FFC0-$FFF9`, so its own bytes became the pending-interrupt
   flags and the first run took an NMI nobody raised. `C64-SPEC` §3.5.

**And seven more the wave's review found, every one of them silent on the
glass:**

10. **`(int)0xFFFF` is `-1`, and a CIA latch of `$FFFF` is the RESET DEFAULT.**
    `while (n > (int)c) { n -= (int)c + 1; … }` in the timer step never
    terminated for the standard free-running-timer idiom — an infinite loop on
    the UI task — and the same wrap in `c64_alarm_next` made such a counter
    look one cycle away, so the driver ran the core one emulated cycle per
    alarm query. Compare unsigned; skip a counter at or above `$7FFF` instead
    of casting it. **Neither reproduces on the host** (`int` is 32 bits
    there), which is why the harness's row carries a `short` model of the
    target's widths as its negative control. `C64-SPEC` §4.4.
11. **A PAUSED machine is still `C64_ST_RUN`, and re-posting on the state
    alone spins the UI task** at SPEC.md §74.1's ~1,400 wakes a second, for a
    machine the user deliberately stopped. One `c64_wants_wake()`, after
    `runcpm.c:847`. `C64-SPEC` §4.4.
12. **The fresh-key guarantee was counted in WAKES, and a wake is 1/64 of an
    emulated keyboard scan at the slice floor.** Under QEMU every press is
    scanned many times over; on the target typing loses characters at random —
    PERFORMANCE.md's input overrun, with the emulator-versus-target speed
    ratio as its cause. It is counted in emulated cycles now, and bounded.
    `C64-SPEC` §7.2 rule 4.
13. **A JAM was invented text and a five-second message.** VICE has the string
    (`maincpu.c:612` + `6510core.c:45` → `Main CPU: JAM at $E5CF`), and a
    jammed machine is a PERMANENT row state beside `C64.ROM missing` — the
    glass showed a dead machine and an idle one identically.
    `C64-SPEC` §4.5 and `C64-SPEC` §10.1.
14. **A greying is retired the moment its fact is.** Advance frame was greyed
    with "there is no raster accumulator until the alarm model lands" and this
    wave landed the alarm model. It is live, and its body raises a REQUEST the
    slice driver serves — `os88_oncmd` runs under the gfx lock and a PAL frame
    is 19,656 emulated cycles. `C64-SPEC` §11.1.
15. **The speed widget repainted 24 glyph cells a second where at most four
    change** (~23 ms every second, ten times a keystroke), nine of them two
    literal tails that never change — and its own figures were not a reason to
    flush, so a machine that runs without writing RAM froze them. Tails once,
    numbers delta-drawn, the widget's delta in the flush gate.
    `C64-SPEC` §10.2.
16. **`LOAD"*",8` hung at `SEARCHING FOR *`, and nobody had ever typed one.**
    `$DD00` answered the raw register file, so DATA IN read low — a device
    replying. An empty serial bus reads back what the machine drives,
    INVERTED (`iecbus.c:212-217` + `c64cia2.c:150-162`). With that modelled
    the KERNAL prints `?DEVICE NOT PRESENT  ERROR`, which is what §11.3 and
    `README.TXT` had been promising unread. `C64-SPEC` §6.2, §11.3.

**And six more the SECOND review found — every one of them in what the FIRST
fix pass had just added, which is the shape to expect of a fix pass:**

17. **A ROM-less machine's message never expired, and the new
    `c64_wants_wake` turned that into an unbounded wake spin.** The deadline
    was examined at the far end of `c64_flush`, past the `if (c64_norom)`
    early return, so on a disk with no `C64.ROM` the first menu command a user
    picked owned the status row for the session — with `C64-SPEC` §1.4's line
    behind it — while the handler re-posted ~1,400 times a second for ever.
    The deadline is now the first thing in the flush. `C64-SPEC` §10.1.
18. **A message is not work, and `c64_wants_wake` tested it FIRST.** A paused,
    jammed or ROM-less machine re-posted for the whole five-second life of
    every message with nothing inside the wake but a re-read of the clock:
    ~7,000 round trips, ~4.8 s of the SHARED UI task — SPEC.md §74.1's rule
    inverted, and newly reachable because Alt+P now genuinely stops the
    machine. A machine the user stopped keeps its message until the next
    event, and that is stated rather than silent. `C64-SPEC` §10.1.
19. **`fps` had the `% cpu` defect, one field to the right.** `c64_muldiv`
    answers `$FFFF` on overflow and the cast makes that −1, so `     0.0 fps`
    beside a live `% cpu`. The harness row that "proved" the `% cpu` clamp
    zeroed the frame counter and never evaluated the other half of its own
    scenario. Both clamps are before both casts now, and the row drives both.
    `C64-SPEC` §10.2.
20. **One JAM drew the status row twice, identically, with an erase between.**
    Routed through `c64_say` the line went up as a message; five seconds later
    the deadline cleared it, the selector moved to the permanent state, and
    the row was filled black and re-lettered with the same 22 glyphs at the
    same place — ~21 ms for no pixel, and a visible blank-and-re-letter five
    seconds after the event. A permanent row state does not arrive as a
    message. `C64-SPEC` §10.1.
21. **Preferences > Advance frame did not do what VICE's Advance frame does.**
    `actions-speed.c:72-80` PAUSES a running machine and advances only an
    already-paused one; the port ran a frame from a running machine and then
    paused. The one live item the wave added was the one item whose body had
    not been transcribed (LESSONS.md 1). It is also **greyed again whenever
    there is no machine to advance** — a live black item that is a silent
    no-op is what SPEC.md §47 forbids — and the Alt+Shift+P chord now reads
    the shift level, because the BIOS delivers it identically to Alt+P and it
    was RESUMING a paused machine. `C64-SPEC` §11.1.
22. **The wave's one recurring redraw was unmeasured.** The speed widget is
    the first thing in this program that draws on a TIMER, and §9.7 had no row
    for it. It has two now — **1.7 ms, one `font_run` of one glyph cell**, and
    the whole-row path beside it at **41.7 ms** as the negative control — and
    the harness HOLDS the once-a-second fold for every other cost row, because
    a timer landing inside an unrelated row was pricing the status widget as
    part of it. `C64-SPEC` §9.7 and `C64-SPEC` §10.2.

And one number the glass answered: **under QEMU the core runs at ~2,700 % of
a real C64 and ~1,350 emulated VIC frames a second** (the two figures agree to
the ratio 19,656 / 98,516, which is itself the check that they come from the
same clock). That is a fact about QEMU and **not** a claim about an 8088 —
CLAUDE.md's "exact about how much work the guest does and useless about how
long it takes" — and the target's figure is manual evidence off the 86Box
machines (Decision 21).

### Wave 3 — The commands: reset, exit, pause, warp, fullscreen, copy/paste, joystick, SID

- `c64cmd.c` as `ovl_*`: Reset machine CPU (Alt+F9), Power cycle (menu; Alt+F12
  where the BIOS passes it), Exit emulator (Alt+Q, the worker self-close),
  Pause (Alt+P, shown checked by text swap), Warp (Alt+W: flush every 9
  ticks), Swap
  joysticks (Alt+J), Copy (screen PETSCII → ASCII → `os88_clip_put`), Paste
  (`os88_clip_get` → the `$0277` feeder, 10 characters a jiffy)
  (**Advance frame is NOT here**: it went live in wave 2's fix pass, because
  the fact that greyed it — no raster accumulator — stopped being true the
  moment the alarm model landed. `C64-SPEC` §11.1 is the contract and this
  list used to contradict it)
- **the first `ovl_` call from the first wake** (the `.OVL` cannot load from
  `os88_main`); `Unable to load C64.OVL.` printed in the status row as well as
  toasted (the toast is under a fullscreen window)
- Alt+D fullscreen via `os88_fullscreen` with `c64_band_x2` where the tier
  table allows; 1:1 centred otherwise; border fill of the rest of the screen
- the joystick: port 2 on arrows + Ctrl from the cached key state; the two
  5-dot indicators delta-drawn; the warp and pause lamps `W`/`P`
- SID voice 1 → `os88_snd_tone` per slice on change; the rest greyed

**Files:** `apps/c64/c64cmd.c`, `c64.c`, `c64io.c`, `c64kbd.c`, `c64menu.c`,
`c64scr.c`, `c64about.c`, `c64.asm`, `c64band.inc`, `c64mem.inc`, `build.sh`,
`hosttest/{os88.h, c64uitest.c, c64memtest.asm}`, `docs/C64-SPEC.md` — and,
because the fix pass found the wake's promise broken in the KERNEL rather than
in the port, `kernel/wm.inc` + `kernel/ui.inc` (`wm_wake_sweep`), and the SDK
for the three thunks the port needed and did not have:
`apps/cc/{os88.h, os88thunk.asm}` (`os88_wm_close`, `os88_clip_put_seg`,
`os88_clip_get_seg`)

**Done when:**
screendumps: Alt+D on `VIDEO=cga` (`mouse.py --screen 640x200`) shows the C64
screen **640 pixels wide exactly** — 2× horizontal, which is the whole job on
an adapter whose pixel is already 2:1, with C64-SPEC §9.1's standing clamp giving 21 of
the 25 rows above the status row rather than the "filling 640×200" this line
first promised, because a 200-line screen cannot hold 25 rows AND the status
row (`build/port-shots/w3fix-19-fullscreen-2x-cga.png`) — and on VGA 640×400
centred with all 25 (`w3fix-16-fullscreen-2x-vga.png`); on the `CPU_8086` tier
1:1 centred, which is C64-SPEC §9.8's tier table and a fact `os88_cpu()`
answers rather than a guess about speed;
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

**What wave 3 measured, and what it changed** (2026-08-22). Copy and Paste are
live, warp is the wall slice's cap, the sound capability is established before
the slot is called, and `C64-SPEC` §7.6's ScrollLock hint ships on an
observable the SDK
can actually be asked for. **Size: resident image 37,204 + bss 13,410 = 50,614
of 61,440, `C64.OVL` 2,549, 29 resident shims, largest frame 32** — 4,386
under SPEC.md §73.9's 55,000 trigger. `C64-SPEC` §13.0.1 carries the per-figure
delta.

Six things the plan could not know, each written back into `docs/C64-SPEC.md`
rather than left in the code:

1. **`os88_mouse()` cannot be asked whether a mouse has spoken**, which is
   what `C64-SPEC` §7.6 said to do. `osapi_mouse`
   (`kernel/kernel.asm:3263`) tests `[mou_seen]` to decide whether to poll the
   keyboard mouse and then answers x, y and the button; no slot reports the
   flag, and adding one spends kernel headroom. The package asks a question it
   CAN answer and that has the same answer: `kbm_key`
   (`kernel/mouse.inc:934-941`) intercepts a cursor key exactly when no mouse
   has spoken **and** ScrollLock is off, and an intercepted key never reaches
   `os88_onkey` — so *"an arrow is held and `os88_onkey` has never delivered
   one"* IS *"the kernel is eating them"*, observed. Three consecutive polls,
   because a wake posted before a press is dispatched ahead of the key event
   behind it. `C64-SPEC` §7.6.
2. **A warp that slows the FLUSH down is not a warp.** The plan said *"flush
   every 9 ticks"*; that does not run the machine one cycle faster, it stops
   showing what it does. The user's decision — cap lifted, flush rate
   unchanged — is what shipped, and `C64-SPEC` §4.4 states what it is measured
   to be worth (**2,070 % → 2,182 %** under QEMU, which is the saved wake
   round trips) and where it is worth **nothing**: on a 4.77 MHz target the
   adaptation's halving arm settles the budget near its floor and the cap is
   not what binds. Lifting a ceiling the machine never reaches changes
   nothing, and that is said rather than left to be discovered.
3. **The budget's doubling had to be CLAMPED, not merely stopped below the
   cap.** With one cap of 16,384 the old test landed exactly on it; with
   warp's 30,000 it lands on 32,768, and `int` is sixteen bits — the budget
   arrives as −32,768 and the core expires before its first fetch, so warp
   would have stopped the machine dead. The harness found it; nothing on a
   glass would have. `C64-SPEC` §4.4.
4. **A Copy comes back in LOWER CASE, and that is VICE's answer.**
   `charset_p_toascii` maps PETSCII `$41-$5A` to `'a'-'z'` and `$C1-$DA` to
   `'A'-'Z'` whatever the VIC is drawing, so the boot screen copies as
   `**** commodore 64 basic v2 ****`. Transcribed, not chosen, and written
   down so nobody files it. `C64-SPEC` §7.7.
5. **CRLF is TWO bytes and ONE line end** (`charset.c:49-63`), and without the
   pair test a document written on a DOS machine types two RETURNs a line —
   in BASIC, a listing with a blank line between every statement. The harness
   row is a 40-byte clipboard with one CRLF and one LF that must queue **39**
   PETSCII bytes.
6. **The paste queue and the copy buffer cannot be one buffer.** A Copy taken
   during a long paste would otherwise rewrite what was still being typed.
   They are also **not the same size**, which the first draft got wrong — see
   the fix pass below. `C64-SPEC` §7.7.

**THE REVIEW'S FIX PASS (2026-08-22), and its finding is the one this port
should carry into every later wave.** *A `ovl_*` function is a SEGMENT, and a
loop written inside one crosses the boundary every iteration.* Wave 3 put
Copy's 40×25 walk and Paste's byte map in `c64cmd.c` — so the shipped inner
loop was `call far [cc_ovv_c64_rd]` **and** `call far [cc_ovm_ovl_sc_ascii]`,
two bridge round trips a cell, **2,000 for one Edit > Copy**, every one of
them through `crt0.asm`'s `cc_ovthunk` before any work happened — and all of
it under the gfx lock, which is where `os88_oncmd` runs. It assembled, it
booted, it was demonstrated on the glass twice, and **the cost table reported
it as 25.0 ms** because the model charged one NEAR call per cell and charged
the conversion and the loop body nothing at all. That is exactly
`docs/C-TOOLCHAIN.md`'s *"the counters live in the drawing primitives... the
part no counter is watching"*. What the fix pass changed:

- **the loops came home.** `c64_sc_ascii`/`c64_a_petscii` are resident and are
  now called 384 times AT LAUNCH to build a 128- and a 256-byte table, so
  neither loop makes a call per byte; the matrix comes out **one row per
  `c64_zcopy_out`**; the command shells stay `ovl_*`. **3 bridge crossings for
  a Copy, not 2,000** — and `C64.OVL` got SMALLER (2,549 → 1,717) while the
  resident image grew, which is what a frequency split looks like when it is
  done right.
- **Paste's byte map moved into the FEEDER**, ten bytes a wake with **no lock
  held**, because SmallerC emits ~145 µs a byte and converting a full queue
  under the lock would be ~300 ms of stopped desktop. Nothing about it is
  observable from the machine's side. The command is now **0.9 ms of held
  lock whatever the clipboard holds**, and Copy is **79.4 ms** — stated, and
  under the 100 ms at which it would have had to become a wake-driven request.
- **the cost model learned the boundary.** `C64COST_OVLCALL` (58 µs a
  crossing) plus per-cell and per-byte constants **counted from the emitted
  code** — the loop bodies were extracted from `build/c64.gen.asm`, assembled,
  and priced at the 8088's 4.34 clocks-a-byte fetch floor. The harness now
  fails if a Copy ever reads the matrix a cell at a time again.
- **Copy and Paste are greyed by state.** Paste with no `C64.ROM` and on a
  JAM (nothing drains the queue in either); Copy with no `C64.ROM` (the
  clipboard is kernel-owned and outlives the app — a Copy there would spend
  the user's clipboard on the factory RAM pattern). The chords are guarded
  with the items.
- **a clipboard of exactly 32,768 bytes is not an empty one.**
  `kernel/clip.inc:84` accepts it and `os88_clip_size` answers `0x8000`, which
  a 16-bit `int` reads as −32,768: `sz <= 0` said *"The clipboard is empty."*
  on a full clipboard. The test is `sz == -1`, and the harness stub casts
  through `short` so the host's 32-bit `int` cannot hide it.
- **the paste queue is sized from what a PASTE is** — 2,048 bytes, not the
  1,026 that is Copy's bound — and the limit is in `README.TXT` and in the
  message, which names the number and is checked against the constant.
- **Warp says what it does on the target**: `Warp mode on - no faster on this
  CPU.` on `CPU_8086`, VICE's plain `Warp mode on.` elsewhere.
  `C64-SPEC` §4.4 was honest; the glass was not.
- **`kernel/mouse.inc:1426` was not the citation for `int 16h AH=0`** and had
  been copied out of `C64-SPEC` §7.5 into the shipping source. Both now point
  at `kernel/ui.inc:84-95`, which is `ui_task`'s own peek-and-fetch.

**Size after the fix pass: resident image 38,372 + bss 14,856 = 53,228 of
61,440, `C64.OVL` 1,717, 30 resident shims, largest frame 32** — 1,772 under
SPEC.md §73.9's 55,000 trigger, which is the tightest this port has been and
is called out in `C64-SPEC` §13.0.1 with the one figure that could be given
back.

**THE REVIEW'S SECOND FIX PASS (2026-08-22).** Four majors and six minors,
one of which was refused with its reason. What it changed:

- **the port's own once-per-launch code went out.** `c64_sc_ascii`,
  `c64_a_petscii` and `c64_conv_init` — ~275 emitted instructions, **655
  bytes**, measured off a `nasm -l` listing — ran exactly once, from
  `os88_main`, and sat in the resident half beside the loops that index their
  output a thousand times a Copy. They are now ONE `ovl_conv_init`, called
  from the first wake beside the probe it replaces: three functions would have
  been 384 far calls, because an `ovl_*` calling an `ovl_*` crosses the
  bridge. The tables stay resident and DS-relative, which is what makes it
  legal. `SPEC.md §73.14` splits by FREQUENCY and once-per-launch is the side
  that goes out — the rule read as a fact about this port's own launch path
  and not only about menu commands.
- **`Edit > Copy` substituted `?` where VICE substitutes `.`**, on a reading
  of the reference that was backwards: `edit_copy_action`'s mangle
  (`actions-clipboard.c:69-76`) replaces a byte only when it is neither a line
  ending nor printable, and `ASCII_UNMAPPED` is `'.'` (`charset.c:126`), which
  IS printable — so **VICE's Copy output cannot contain a `?` at all**. The
  cells that reach that arm are the GRAPHICS cells (screen codes `$40`,
  `$5B-$5F`, `$60`, `$7B-$7F`), i.e. what a user copying a game screen hits;
  the port's own font fallback already answered `.` for them. The harness had
  never copied one, and now does.
- **the cost model was charging `os88_clip_put` one far call and nothing for
  its body.** `kernel/clip.inc:70-125` is a `clip_drop`, a `mem_claim` and a
  `rep movsb` of every byte, inside the caller's lock: the copy alone is
  ~3.7 ms for Copy's 1,025 bytes, and the CLAIM can reach `mem_compact`,
  which `kernel/memory.inc` prices at *"a memcpy in tenths of a second"*. The
  copy is charged (Copy: **79.4 → 83.2 ms**, taken on a FULL screen now) and
  the compaction is stated as UNBOUNDED and unmodelled — which is the fact
  that decides synchronous-vs-wake-driven, not the 83.2.
- **warp's render cap came back, as VICE's own number.** The previous pass
  deleted the frame skip on the reading that drawing less is *"a different
  feature with the same name"*; `src/vsync.c:339-340` and `:634-656` say it is
  half of VICE's warp, deliberately (*"makes warp faster"*). The flush is now
  `max(c64_flush_every, 2)` ticks while warping — 18.2 Hz / 10 fps — and on
  `CPU_8086` that changes nothing, because the tier already flushes every
  other tick, which is SLOWER than the cap. So both halves of warp are still
  no-ops on the target, for two separate reasons, and the tier message stands
  — but it is now labelled as this port's own wording, because `Warp mode on.`
  is nowhere in VICE either (VICE has a `warp:` LED and no message).
- **a message stopped erasing the widgets beside it.** The row filled all 42
  cells for any message and put back only the message and the two lamps, so
  `Joysticks:`, both indicators and the drive number were blank for five
  seconds — and wave 3 made that self-defeating, because `ScrollLock for
  joystick` is raised BY joystick use. A message of 25 cells or fewer now
  erases only the cells it needs (**22.8 ms up, 26.0 ms down, against 36.8 +
  42.0**), a longer one still owns the field area, and three flags carry the
  distinction because they are three different facts.
- **Paste greys while PAUSED** — the state the greying was reasoned about and
  missed, because pause is not a state but a flag on `C64_ST_RUN`. Copy's
  four-crossing bridge count, the two `c64_paste_convert` citations to a
  function that does not exist, and a message-length gate that could never
  fail (it measured the clamp, not the literal) were the other minors.

**Size after this pass: resident image 38,278 + bss 14,862 = 53,140 of
61,440, `C64.OVL` 2,375, 30 resident shims, largest frame 32** — 1,860 under
SPEC.md §73.9's 55,000 trigger. The frequency move gave 655 bytes back and the
status row's correctness fix spent about 560 of them; `C64-SPEC` §13.0.1
carries that accounting rather than a headline.

**And one defect found here that turned out to be the KERNEL's, and was
fixed there** (`kernel/wm.inc` + `kernel/ui.inc`, `wm_wake_sweep`, its own
commit `0bc11cc`). **Launching any second package looked like a JAM of the
emulated 6510.** Boot `build/c64.img` with `NOTEPAD.O88` beside the `C64\`
folder, launch C64 (the status row counts up normally), then launch Note Pad
and raise C64 again: the speed figures froze for good, no keystroke echoed,
and Preferences showed **Advance frame greyed**. It reproduced on the wave-2
build with none of wave 3's code exercised (`build/port-shots/wave3-70-w2-a.png`
… `wave3-73-w2-pref.png`), survived File > Reset machine CPU, and the
`Main CPU: JAM at $XXXX` line never reached the row — which was the clue: no
jam line because there was no jam. The package's wake had been drained off the
event ring by a kernel loop and the coalescing flag left set, so nothing was
ever delivered again (`C64-SPEC` §4.5 has the mechanism, SPEC.md §74.1 the
rule). It is why "press Enter to get into BASIC" was ever a thing: the key
posted the wake the launch had lost. RUNCPM had the same defect and the same
fix; the verifier saw both keep running across a second launch and a drag.
The fix crossed an image rung (119 → 120 steps; `KERN_BUDGET` spare 1,536 →
1,024), and `docs/KERNEL-MEMORY.md` has not yet been updated for it.

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
  every greyed item's fact; `docs/plans/completed/C64-PORT-PLAN.md` gains each wave's measured
  paragraph; `README.md` row; `LESSONS.md` "What the C64 port added";
  PERFORMANCE.md's new Set from `tests/c64band`; `checkdocs` clean

**Files:** `apps/c64/c64about.c`, `c64menu.c`, `c64cmd.c`, `Makefile`,
`vm/xt-c64/86box.cfg`, `vm/286-c64/86box.cfg`, `README.md`, `CLAUDE.md`,
`PERFORMANCE.md`, `docs/C64-SPEC.md`, `docs/plans/completed/C64-PORT-PLAN.md`,
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
  `SS != DS`, running Decision 22's twelve rows with a negative control on each.
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
