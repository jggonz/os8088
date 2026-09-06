# INFONES port plan — InfoNES as an os8088 C package

The design record for **SPEC.md §91**, the contract. Produced by
`.claude/skills/port-to-os8088`'s scouting workflow — one scout per reference
repository, three over this tree, a planner, three adversarial reviewers and a
reconciler — and then answered against the Decisions below.
`workflows/implement.js` reads this file one wave at a time.

The reference source is **InfoNES at commit
`fe3295c0a86bf5bbecc22aeab3b3af11f6f47908`**
(https://github.com/jay-kumogata/InfoNES), Apache-2.0, © Jay Kumogata / Jay's
Factory. It lives outside this repository and **nothing of it is vendored**
(`CONTRIBUTING.md` §6). Two further references are read and cited and neither
ships: **agnes** (MIT) for the scroll and sprite specification and as the
host-side behaviour oracle, and **nofrendo** (LGPL-2) for intent only, never
transcribed. Decision 1 is the whole of the licence posture and SPEC.md §91.1
is where it is binding.

---

## Summary

INFONES is a NES emulator for os8088, reimplemented in the strict C subset
(SPEC.md §73) from InfoNES's *behaviour* — not compiled from its source.
Nothing of InfoNES's C survives literally: its flat >64KB pointer model, its
122,880-byte frame buffer, its 32KB static ChrBuf, its 263-byte automatics and
its `DWORD`s are each a hard refusal here. What ports is the SHAPE the
research picked InfoNES for: a per-scanline composer over a pre-decoded
one-byte-per-pixel tile cache, a frame loop of "run 113/114 CPU cycles, call
the mapper's HSync, draw one line", and sprite-0 hit by splitting that line's
CPU budget at the sprite's X. The window is a FRONT PANEL (ROM name, mapper,
PRG/CHR, mirroring, the measured emulated AND rendered rates, the controls
legend, and the sentence for every fact that greys a menu item) with three
FLAT kernel menus — os8088 has no submenus and InfoNES's menu is three POPUPs
deep, so the Options menu is a stated ADAPTATION of the `.rc`, not a trace of
it. The game runs FULLSCREEN through SPEC.md §53's exclusive bracket, entered
from the package's own assembly (fsx is deliberately unwrapped for C), with
the 2A03 core and the scanline composer hand-written 8086 in
`apps/c64/c64cpu.inc`'s shape and the emulated machine in heap claims. The
honest deliverable is stated up front and as TWO numbers, because one of them
was hiding the other: with frame skip the 4.77 MHz 8088 advances the game at
roughly 2 emulated frames a second (~3% of a real NES) and PAINTS about one
picture every two seconds — the un-skippable core alone caps it at 2.94 fps —
so the XT is a demonstration that a NES emulator runs on an 8088 at all and is
not an interactive machine, which the panel and CATALOG.TXT say in those
words; a 386DX-33 is roughly half speed and a 486DX2/66 near full. Every one
of those figures is an estimate until `make nibandbench` replaces it.

---

## Decisions

These replace the scout's `questions` block. They were taken by the
orchestrating session under the user's standing instruction to decide as much
as possible without asking, and they are **binding on every wave**. The text
below is the decision record verbatim.

Taken by the orchestrating session on 2026-09-06 under the user's standing
instruction "make as many decisions as you can on your own"; the user was not
asked. Each answers a question in plan.json's `questions`.

1. LICENCE POSTURE: (a). Derive behaviour from InfoNES at the pinned commit
   fe3295c0a86bf5bbecc22aeab3b3af11f6f47908 under Apache-2.0. Every derived
   source file carries an Apache header naming Jay Kumogata / Jay's Factory,
   the upstream URL https://github.com/jay-kumogata/InfoNES and the commit,
   and the section 4(b) modification notice "derived from InfoNES fe3295c0,
   restructured for 8086 real mode". The attribution is in the About box.
   The Apache-2.0 text ships as LICENSE.TXT on every floppy that carries the
   binary. SPEC.md carries the provenance line "xNES / pNesX -> InfoNES;
   pNesX authorship and terms unestablished" and the note that the Apache
   grant is the 2019 retrofit by the sole author. nofrendo (LGPL-2) is cited
   file:line where its scroll arithmetic was read; agnes (MIT) is cited as
   the harness oracle. Nothing is vendored.
2. SIR ABABOL / NC-SA: (b). Every shipped floppy is free of NC/SA
   conditions: the four Yerrick games everywhere, Mega Mountain (GPL-3 +
   MIT) on 720KB/1.2MB/1.44MB. Sir Ababol Remastered and Super Uwol are
   offered through The Wire only, with per-title licence text in the record.
3. SOUND: (a). Options > Mute is greyed with the fact; the pulse-1 PIT
   arithmetic is recorded in SPEC.md as a costed follow-up. No tone ships.
4. THE XT: (a). The CPU_8086 tier SHIPS as an explicitly NON-INTERACTIVE
   DEMONSTRATION: the bracket runs, the panel prints BOTH rates (emulated
   percent of a NES and rendered seconds per frame), and CATALOG.TXT, the
   SPEC tier table, README and the release/Wire copy say in those words that
   an 8088 shows a NES emulator running and is not a machine to play on.
   vm/xt-infones and the 360KB disk ship. Never refuse on tier.
5. ABOUT LINE: the Linux pairing the plan chose — "InfoNES v0.96J", "A fast
   and portable NES emulator", "Copyright (c) 1999-2005 Jay's Factory
   <jays_factory@excite.co.jp>" — plus the port's own attribution line
   naming the pinned commit.
6. KEYS: as the plan records — arrows D-pad, X = A, Z = B, Enter = Start,
   RShift (Space alias) = Select; inside the bracket C clip, R reset, Page
   Up/Down frame skip, M refused with the fact; F and Esc leave.
7. VGA MODE: mode 13h is the default fullscreen mode on every tier; Mode X
   is a menu choice above the CPU_8086 tier (plan wave 3).
8. Everything else in plan.json (file split, budget, waves, verification,
   greyed/dropped items and their facts, names) stands as written.
9. AGENTS: every implement.js role runs on Opus (args.models). Builders name
   the paths they `git add`; never `git add -A`/`-u`/`.`; never touch vm/*
   files they did not create; never `git reset --hard`, `checkout --` or
   `stash` on paths they do not own. The worktree is /tmp/nes on branch
   port/nes; build/ is never committed.

---

## Pre-wave-1 review: the Codex findings and their resolutions (binding)

An outside read-only review of plan.json by the Codex CLI (2026-09-06, its
report is `codex-plan-review.md` beside the plan in the session scratchpad;
its verdict was "do not start wave 1 unchanged") found nine things. Each is
resolved here, and the resolution binds every wave. Where a resolution
narrows an accuracy promise, that is deliberate: this port is a
SCANLINE-GRANULAR emulator in InfoNES's own class, and the reason it exists
is speed on an 8088; it does not become a dot-clock PPU to pass a gate.

R1. THE 512-BYTE STACK (blocker 1). Task 0's stack is `STK0_SIZE` = 512
    bytes (kernel/kernel.asm) and the bracket, the frame loop, the core and
    any C register handler called from inside an instruction all run on it,
    under the kernel's own `fsx_run` and `wm_pkgcall` frames, with interrupts
    landing on top. Wave 1 WRITES THE STACK BUDGET as a table in SPEC §91:
    every frame on the chain from the menu callback to the deepest C
    handler, counted from the generated `.raw.asm` and the shim, plus 64
    bytes of interrupt headroom, and it must sum under 512 with the kernel's
    frames included. If it does not fit, PPU/mapper register handlers move
    from C into `nicpu.inc`/`nippu` assembly and every scratch goes static —
    the plan's "every buffer is static" rule already points that way. Wave 2
    adds a HIGH-WATER SENTINEL test: the shim fills the free stack with a
    pattern before entering the bracket and `nisystest` reports the deepest
    scrub after a run that exercises mapper writes, PPU reads, OAM DMA,
    reset, key polling and the present; the SPEC records the number.

R2. THE BUS AND INTERRUPT CONTRACT (blocker 2), written in SPEC §91 by wave
    1 before a handler is translated:
    - The D flag is STATE, not arithmetic: SED/CLD/PLP/RTI/PHP carry it in
      `CH` like V, I and B; ADC/SBC never look at it. (The plan's "drop D"
      meant the arithmetic; nestest's P column needs the bit.)
    - Dummy reads: on the indexed page-cross penalty path and on every
      read-modify-write, if the effective address is in $2000-$3FFF or
      $4000-$401F the core performs the extra read/write through the slow
      path (one compare on a path already priced as the penalty); RAM and
      ROM addresses skip it. That is what `cpu_dummy_reads` tests.
    - Every mapper write re-evaluates the fetch bias: the mapper handler
      returns a flag and the core clears `BLO`/`BOUND` so the next fetch
      re-biases `ES`, even when PC did not leave the cached region.
    - OAM DMA ($4014) costs 513 cycles (514 on an odd cycle: one parity bit
      in scratch) and the debt is SUBTRACTED from the scanline budget and
      carried into following lines without executing instructions; the
      frame loop's "overrun by one instruction" rule is stated as
      "overrun by one instruction or one DMA".
    - Open bus: one scratch byte holds the last value on the data bus;
      unmapped reads ($4000-$401F except $4015/$4016/$4017, $4020-$5FFF,
      $6000-$7FFF without SRAM) answer it; $2002 and $2007 reads mix in the
      documented open-bus bits.
    - NMI is an EDGE: latched when vblank sets with NMI enabled, and when
      $2000 enables NMI while vblank is already set; cleared by the $2002
      read only for the flag, not for a pending edge. IRQ is a LEVEL of
      three sources (mapper, APU frame — see below, none else) OR'd into
      one scratch byte, honoured when I is clear.
    - The silent APU: $4015 reads answer the frame-IRQ flag in bit 6 and
      zero elsewhere; $4017 writes are honoured for the frame-counter mode
      and the IRQ-inhibit bit; the 4-step frame IRQ IS raised every 29,830
      cycles when not inhibited (a counter in scratch, decremented per
      scanline) because games that rely on it hang otherwise; all other APU
      registers are write-only sinks. Stated in the SPEC as a fact.
    - Focused bus traces beside nestest: `nicputest` gains rows for the
      dummy read, the fetch re-bias after a mapper write, the DMA debt, the
      NMI edge on late enable and the frame IRQ, each with a negative
      control.

R3. THE PPU ACCURACY PROMISE IS NARROWED (blocker 3). The PPU is a
    SCANLINE state machine, in InfoNES's class, and the SPEC says so in a
    sentence. What it does: per visible line, at line START copy the
    horizontal bits of `t` into `v` (dot 257 at line granularity), at the
    pre-render line copy the vertical bits; a $2005/$2006 write mid-line
    takes effect from the NEXT line; vblank sets at line 241 and clears at
    the pre-render line; the odd-frame dot skip is NOT modelled (the frame
    is 29,780 or 29,781 cycles by the triple's phase, stated). The wave-4
    gate is therefore NOT "all ten ppu_vbl_nmi singles": it is the subset a
    scanline model can pass, determined by running them — 01-vbl_basic and
    the NMI on/off and timing-independent singles are expected; each single
    that fails is LISTED in SPEC §91 with its reason and no gate rests on
    it. The instr_test-v5 singles, cpu_dummy_reads and instr_timing remain
    gates.

R4. THE HARNESS FIXES (blocker 4), all in wave 1 or 2 as stated:
    - The blargg signature is `$DE $B0 $61` at $6001-$6003 (the plan's
      `$G1` was a documentation typo); the runner is a bounded state
      machine: startup ($6000 not yet $80), running ($80), reset requested
      ($81: perform a CPU reset after ~100 ms and continue), done (< $80),
      timeout (a cycle cap per ROM, printed).
    - The CPU harness's stub bus raises a PERIODIC vblank: $2002 bit 7
      sets every 29,780 cycles and clears on read, because blargg's shell
      waits for vblank repeatedly before it opens its result channel.
    - `palette_ram` (2005) is DROPPED from the gate list (its result
      protocol is `result == 1`, not $6000); the modern singles replace it.
    - The 240→200 row-drop table moves INTO WAVE 2 with the 13h present:
      exactly 200 destination rows, 40 dropped source rows (8 top, 8 bottom,
      then 24 of the middle 224 by a fixed table), and every store bounded
      inside the 64,000-byte screen; `nimemtest` asserts the bounds.
    - `niuitest`'s whole-ROM oracle is NAMED: agnes (`agnes.c`, MIT) is
      compiled into the host harness as the reference machine; our C PPU
      register model is driven with the same inputs and compared per frame
      against agnes's state and framebuffer through `niref.py` (which
      masks the priority-tag bits before comparing NES palette indices and
      receives the per-line scroll/palette history the composer used, not
      only the final PPU snapshot). The stub `os88.h` is diffed against
      `apps/cc/os88.h`'s prototypes by `build.sh` so drift fails the build.
    - Wave 2's `done_when` includes a NON-TRIVIAL `nisystest` run (one
      blargg single end to end through the $6000 protocol on the shipping
      package), not only screendumps.

R5. THE LOADER AND THE CLAIM TABLE (major 5). Wave 1 publishes in SPEC §91
    an OFFSET-AND-SIZE CLAIM TABLE with ownership and lifetime: the RAM/PPU
    claim (2KB RAM, 2KB nametables, 256 OAM, 32 palette, 8KB SRAM window,
    core scratch, the bank tables), the PRG storage claims (16KB banks, up
    to 256KB in four 64KB claims; the mapped window is a 4-entry table of
    8KB segment bases for $8000-$FFFF, so MMC3's 8KB granularity and MMC1's
    16/32KB both fit), the CHR claim (up to 128KB in two 64KB claims, or
    8KB CHR-RAM), the 32KB tile cache claim, and the TRANSIENT staging
    claim used during a load. A ROM ≤ 64KB (every shipped game, every
    harness single, nestest) is read whole by `os88_file_read_seg` into
    the staging claim and copied out with the header skew by `nimem.inc`'s
    mover — that is wave 1, and it is the whole loader wave 1 needs. A ROM
    > 64KB is WAVE 4's: `os88_file_read_at` into a resident cluster buffer
    of 4,096 bytes of bss (media with clusters above 4KB refuse with the
    fact: "INFONES reads ROMs in clusters of up to 4KB; this volume's are
    %u"), streamed into the PRG/CHR claims with the carry across cluster
    boundaries handled by the mover, and the 4,096 is in the budget table.
    The trainer: when the header's trainer bit is set PRG begins at byte
    528 and the 512 trainer bytes are copied to $7000 (SRAM window) — done,
    not refused. Reload: the old ROM's claims are FREED before the new
    load's claims are taken, so the peak is one ROM plus the staging claim.

R6. PACING COUNTS ELAPSED TIME (major 6). The 110/100 accumulator is
    driven by TICKS ELAPSED, not by wait calls: `dt = os88_ticks() - last`
    (18.2065 Hz), `acc += dt * 330` (60.0988/18.2065 = 3.3009), run one
    emulated frame per 100 while `acc >= 100`, with BOUNDED CATCH-UP (at
    most 4 emulated frames per wake; beyond that the debt is DROPPED and an
    overload counter the panel can show is bumped — the XT lives in this
    branch permanently and that is the "demonstration" of decision 4), and
    only when `acc < 100` does the loop wait on `FSXW_FRAME` (with
    `FSXF_FASTTICK`, so the wait is ≤ 18 ms and yields). Mode X page-flip
    blocking is inside the same accounting. `niuitest` tests the pacer
    with an injected clock: work below and above one sub-tick, a double
    frame, tick wraparound at 65,535, and the XT's overload branch. Keys:
    the D-pad sweep keeps BOTH the current held level and the sticky
    press latch; system keys are drained at the same sub-frame interval.

R7. SPRITES COMPOSE INTO THE SPRITE SCRATCH FIRST (major 7). Per line: the
    up-to-eight sprites are painted into the 272-byte sprite scratch in
    ASCENDING OAM order with first-writer-wins (so the lowest index owns a
    pixel), carrying the behind-background bit and the sprite-0 bit as
    tags; then ONE merge pass with the untouched background line resolves
    priority per pixel (opaque background beats a behind-tagged sprite;
    otherwise the sprite). Sprite-0 hit is computed IN THE MERGE as the
    first pixel where sprite 0 is opaque AND the background is opaque (left
    masks respected, X = 255 excluded, transparent prefixes skipped); the
    hit X found on a line is what the NEXT FRAME's same line uses for the
    CPU-budget split (status bars are stable frame to frame, and the SPEC
    records this one-frame latency as the approximation). On skipped
    frames the sprite-0 hit line and X are carried from the last rendered
    frame so the split still happens. `niuitest` adds synthetic cases: a
    hidden lower-index sprite, nine sprites on a line (the eighth wins
    the cap, the overflow flag sets), transparent prefixes, the left
    masks, both flips, and a palette colour shared by a transparent and an
    opaque pattern value.

R8. MMC3 (major 8). The scanline IRQ is InfoNES's HSync approximation —
    counted once per visible line while rendering is enabled — and SPEC
    §91 states it as an approximation of the A12-edge counter (8×16
    sprites and mid-line pattern-table swaps can be off by a line). The
    tile cache's eight 1KB slots are keyed by PHYSICAL bank number AND a
    CHR-RAM write generation, so an A→B→A alternation in a used slot is
    the decode cost it really is; wave 4 BENCHES the used-bank
    alternation, and the recorded fallback if MMC3 titles thrash is a
    per-tile 2bpp decode through a 256-entry unpack table on the MMC3
    path only. A mapper-1 and a mapper-4 TEST ROM (not a screenshot of a
    shipped NROM game) are the wave-4 evidence, and the split-screen
    assertion becomes "the split row is stable across ten frames AND the
    frame counter advanced ten" read from the panel.

R9. LICENCE MANIFEST (major 9). Decision 1 stands with its limit STATED:
    InfoNES descends from pNesX (K6502.cpp:5 says so) whose authorship and
    terms are unestablished; the 2019 Apache-2.0 grant covers what Jay
    Kumogata holds and this port records that it cannot establish more.
    Derivation is tracked PER FILE in each header (which reference file
    each part follows). `nicpu.inc` is written from the 6502 documentation
    and InfoNES's K6502.cpp behaviour in `apps/c64/c64cpu.inc`'s REGISTER
    PLAN — a plan is not code — and copies NO text from c64cpu.inc, which
    is GPL-2-or-later by its VICE ancestry; if any routine is ever copied
    from apps/c64 that file's header says GPL-2+. agnes's MIT notice is
    reproduced verbatim in the harness files that link it. The disks and
    The Wire archive carry: LICENSE.TXT (Apache-2.0 text), LICENSES.TXT
    (the GPL-3 text once, the Zlib and All-Permissive notices, Mega
    Mountain's MIT notice) and README.TXT with, per GPL title, the pinned
    tag/commit of the corresponding source and the sentence that source is
    obtainable from that URL and from os8088.com beside the download; the
    Wire records for the Mojon titles carry their LGPL-3 code obligation
    and CC BY-NC-SA notice in the same way. Disk occupancy is recomputed
    with these files in wave 4.

Also folded from the plan writer's report: `done_when` fields are prose
(as intended); waves 3 AND 4 each re-check the size line (both); the SDK
DOES have a build-time association block (`CC_ASSOC`, apps/cc/crt0.asm),
so `.NES` opens on the first double-click and LESSONS.md §9 is corrected in
wave 5; `vm/xt-infones` SHIPS (decision 4); `nifsx.inc` is resident.


## Authority table — every surface, and the file that defines it

| surface | authority |
|---|---|
| Menu bar, FLATTENED — File (Open ROM, Reset, Stop, Exit), Options (Frame skip: Auto \| Frame skip faster \| Frame skip slower \| Full screen \| Clip top and bottom \| \x01Mute (no APU)), Help (About InfoNES) | ADAPTED, NOT TRACED, and the plan says so where the strings are written. The order and the membership come from /Users/jggonz/Repos/nes-ref/InfoNES/src/win32/InfoNES_Resource_Win.rc IDR_MENU lines 61-105, read through `iconv -f CP932` because the file is Shift-JIS: ﾌｧｲﾙ(&F){開く(&O), ﾘｾｯﾄ(&R), 停止(&S), SEPARATOR, 終了(&X)}; ｵﾌﾟｼｮﾝ(&O){ﾌﾚｰﾑｽｷｯﾌﾟ(&F){ｵｰﾄ(&A) CHECKED, 減らす(&D), 増やす(&I)}, 画面(&V){ｻｲｽﾞ(&S){1倍(&1) CHECKED, 2倍(&2), 3倍(&3)}, ｸﾘｯﾌﾟ(&C){上端・下端(&V)}}, ｻｳﾝﾄﾞ(&S){ﾐｭｰﾄ(&M)}}; ﾍﾙﾌﾟ(&H){ROM情報(&I), ﾊﾞｰｼﾞｮﾝ情報(&A)}. There is NO English original anywhere in the tree, so every English string this port ships is a TRANSLATION performed by the porter, and the .rc is three POPUPs deep where apps/cc/os88.h:236-247 gives a menu {title, items, nitems} and nothing else. Command ids in src/win32/InfoNES_Resource_Win.h. The kernel's bounds the flattening had to meet: MENU_APPMAX 5 menus, MENU_POPMAX 11 items (kernel/menu.inc:208), MENU_MAXCH = (208-16)/8 = 24 glyphs (apps/os88api.inc:1943) — every shipped string is <= 24. MENUITEM SEPARATOR has no kernel primitive and is DROPPED |
| About box text: the tagline, the copyright line and the version claim | /Users/jggonz/Repos/nes-ref/InfoNES/src/linux/InfoNES_System_Linux.cpp:45 and :324-328, quoted from ONE file because the tree carries four incompatible pairings and the draft plan silently invented a fifth. That handler is the only live About in the tree that pairs a VERSION with the tagline and a copyright: VERSION = "InfoNES v0.96J", "A fast and portable NES emulator", "Copyright (c) 1999-2005 Jay's Factory <jays_factory@excite.co.jp>" — lowercase (c), 1999-2005. The Win32 About (src/win32/InfoNES_System_Win.cpp:517-533) CANNOT be the authority for a version line: it prints APP_NAME and `grep -rn APP_NAME .` over the whole tree returns nothing. src/sdl/InfoNES_System_SDL.cpp:32 is "InfoNES v0.97J RC1" but its own banner at :164-166 reads "Copyright (C) 1998-2006 Jay's Factory, SDL Ports by mata" — taking that version obliges naming mata, so it is not taken. src/zaurus is v0.93J RC4. THE PORT'S About therefore quotes the Linux pair verbatim and makes NO version claim of its own beyond naming the reference as `InfoNES @fe3295c0`; the four year-ranges are recorded in the SPEC so no later wave re-picks. The .rc's IDD_DIALOG at :113-124 is dead code (#if 0) and is not the authority either |
| The front panel's ROM-info fields and their ORDER: Mapper, PRG ROM nKB, CHR ROM nKB, Mirroring V/H, SRAM Yes/No, 4 Screen Yes/No, Trainer Yes/No | /Users/jggonz/Repos/nes-ref/InfoNES/src/win32/InfoNES_System_Win.cpp:497-511 (Help > ROM info), identically src/linux/InfoNES_System_Linux.cpp:309-320. Units: PRG in 16KB banks printed as KB, CHR in 8KB banks printed as KB — cross-confirmed by nofrendo src/nes/nes_rom.c:390-415 (`"%s [%d] %dk/%dk %c"`) |
| Controller 1 bit order and the Z/X assignment: bit0 A, bit1 B, bit2 Select, bit3 Start, bit4 Up, bit5 Down, bit6 Left, bit7 Right; X = A, Z = B, arrows = D-pad | /Users/jggonz/Repos/nes-ref/InfoNES/src/sdl/InfoNES_System_SDL.cpp:503-540. CAUTION recorded in the plan: src/linux/InfoNES_System_Linux.cpp:237-247 sets the SAME bits with its /* 'A' */ and /* 'B' */ comments SWAPPED — quote the bit numbers, never the Linux comments |
| Start = Enter and Select = RShift (with Space as an alias) | /Users/jggonz/Repos/nes-ref/agnes/examples/simple_sdl2.c:105-116 (SDL_SCANCODE_RETURN -> start, SDL_SCANCODE_RSHIFT -> select) + /Users/jggonz/Repos/nes-ref/nofrendo/README lines 20-84 (Return = Start). InfoNES's own S/A (SDL:503-540) are DELIBERATELY not used and the plan records why: A and S collide with a WASD reflex, and Enter/Shift are what apps/c64 and apps/runcpm already condition on this OS |
| System keys inside the bracket: C = toggle top/bottom clip, R = reset, Page Up = frame skip +, Page Down = frame skip -, M = mute (refused with the fact) | /Users/jggonz/Repos/nes-ref/InfoNES/src/linux/InfoNES_System_Linux.cpp add_key — 'c'/'C' clip, 'r'/'R' reset_application (:262-266), GDK_Page_Up / GDK_Page_Down frameskip (:290-301, Linux-only: absent from SDL and from README.md's "System"), 'm'/'M' mute (:304). THE DRAFT'S `P = pause` IS DELETED: there is no 'p' or 'P' case in any InfoNES front end (`grep -rn "case 'p'\\|case 'P'" src/` returns nothing), README.md's "System" lists four keys, and it was a string typed from memory. R was missing from the draft and is restored — it is one of add_key's own keys and inside the bracket there is otherwise no way to reset. 'l'/'L' (load, :268-289) and 'i'/'I' (ROM info) are not carried: the file dialog is a menu command and the ROM info is permanently on the panel. F and Esc leave the bracket instead of InfoNES's Q/ESC — SPEC.md §11.2.1's rule, which is about the user and not about which surface the app was built with |
| The mapper refusal, naming the number | /Users/jggonz/Repos/nes-ref/InfoNES/src/InfoNES.cpp:439 — `InfoNES_MessageBox("Mapper #%d is unsupported.\n", MapperNo)`. The bad-magic refusal is src/sdl/InfoNES_System_SDL.cpp:158-167 ("%s isn't a NES format file."); nofrendo's wording (src/nes/nes_rom.c:329, "%s is not a valid ROM image") is the cross-read |
| iNES header semantics: magic, byRomSize/byVRomSize units, byInfo1 bits 0-3, the mapper nibbles, the trainer at $7000 | /Users/jggonz/Repos/nes-ref/InfoNES/src/InfoNES.h:263-274 (NesHeader_tag) + src/InfoNES.cpp:358-380 (the field interpretation, incl. the DiskDude guard at :365-373). The dirty-header rule is cross-read from /Users/jggonz/Repos/nes-ref/nofrendo/src/nes/nes_rom.c:313-383, and agnes's 45-line parse (/Users/jggonz/Repos/nes-ref/agnes/agnes.c:517-561) is the compact statement. NONE of the three validates the file's LENGTH against the sizes the header claims — this port adds that check (SPEC.md §19: every byte off a floppy is hostile) |
| Frame loop: 113/114 cycles, the HSync hook, the vblank/NMI line, and sprite-0 hit by splitting the scanline's CPU budget at SPRRAM[SPR_X] | /Users/jggonz/Repos/nes-ref/InfoNES/src/InfoNES.cpp:556-751 (InfoNES_Cycle, InfoNES_HSync) and :1053-1101 (InfoNES_GetSprHitY). The 3-PPU-dots-per-CPU-cycle strike relation is written out in /Users/jggonz/Repos/nes-ref/nofrendo/src/nes/nes_ppu.c:237-246 (ppu_setstrike) |
| The scanline composer: a BYTE line buffer, a BYTE PalTable[32] of raw NES palette indices, backdrop entries tagged \|0x80, and the priority test as one byte compare | /Users/jggonz/Repos/nes-ref/InfoNES/gba/src/InfoNES_Advance/InfoNES.cpp:682-940 (the BYTE renderer — the structural model, NOT the desktop src/InfoNES.cpp:754-1050 which writes WORD RGB into a 122,880-byte frame buffer) and gba/src/InfoNES_Advance/K6502_rw.h:287 (`PalTable[…] = byData \| 0x80`). The tag-bits idea is corroborated by nofrendo src/nes/nes_ppu.c:44-52 (BG_TRANS/SP_PIXEL) — read for intent, nothing transcribed |
| The DAC mirror that makes the priority tag free at present time (64 entries at 0..63, the same 64 at 0x40, 0x80, 0xC0) — VGA ONLY | /Users/jggonz/Repos/nes-ref/InfoNES/gba/src/InfoNES_Advance/InfoNES_System_GBA.cpp:99-103 (`paletteMem[i]` AND `paletteMem[i + 0x80]`), and nofrendo ppu_buildpalette (src/nes/nes_ppu.c:530-552) for the same reason. THE PORT EXTENDS IT: CGA and Hercules have no DAC to mirror, so their reduction tables are 256 entries filled four times over from the 64 real ones — the identical trick moved from the DAC into the table, at 512 bytes of bss and zero cycles. A 64-entry table indexed by a tagged pixel byte reads up to 192 bytes past its end, silently, as a wrong colour |
| The tile cache and its invalidation: decode a 1KB CHR bank into 64 bytes per tile, a bitmask bit per bank, a previous-bank comparison | /Users/jggonz/Repos/nes-ref/InfoNES/src/InfoNES.cpp:1103-1191 (InfoNES_SetupChr) + src/K6502_rw.h:325 (`ChrBufUpdate \|= 1 << (addr >> 10)` on a CHR-RAM write) |
| The per-line drop TABLE's shape (0xff = this line is not displayed) and the frame-skip counter | /Users/jggonz/Repos/nes-ref/InfoNES/gba/src/InfoNES_Advance/InfoNES.cpp:244-261 (PPU_ScalingTable) — THE SHAPE ONLY, and the plan says so: that table keeps 5 of every 7 lines of a 224-row clipped window, which is 224 x 5/7 = 160, the GBA's screen height. It defines a 224->160 reduction, not a 240->200 one. THIS PORT'S ARITHMETIC IS ITS OWN: crop 8 top and 8 bottom to 224, then drop 24 of 224. The frame-skip gate (the CPU and the mappers run, DrawLine and LoadFrame do not) is src/InfoNES.cpp:660-663, :707-722 |
| 2A03 opcode set, per-opcode cycle costs, page-cross penalties, the JMP () page-wrap bug, BRK/NMI/IRQ sequencing and the partial unofficial opcodes | /Users/jggonz/Repos/nes-ref/InfoNES/src/K6502.cpp (151 cases, CLK(n) each) + src/K6502.h + src/K6502_rw.h's addressing macros. The flag bit values (N 0x80 … C 0x01), the vectors ($FFFA/$FFFC/$FFFE), INT_CYCLES 7 and "decimal mode is OFF on a 2A03" are cross-confirmed at /Users/jggonz/Repos/nes-ref/nofrendo/src/cpu/nes6502.h:26-95. The 8086 REGISTER PLAN is C64-SPEC §4.1, not either of them |
| CPU memory map: switch (addr & 0xE000), four 8KB PRG windows, sixteen 1KB PPU windows, $4014 sprite DMA, $4016/$4017 pad shift | /Users/jggonz/Repos/nes-ref/InfoNES/src/K6502_rw.h:46-186 (read) and :190-470 (write). agnes's cpu_read8 (/Users/jggonz/Repos/nes-ref/agnes/agnes.c:814-831) is the cross-read for WHAT each region does; InfoNES's uniform 8KB switch is the shape the segment table takes |
| Loopy v/t scroll: the coarse/fine X and Y increments, the nametable wrap, the dot-257 horizontal copy and the dots-280..304 vertical copy, and the $2000/$2005/$2006 write decoding | /Users/jggonz/Repos/nes-ref/agnes/agnes.c:1100-1140, :1071-1082, :1312-1380 — MIT, uint16_t throughout, no address-of-local. THIS IS A CHANGE FROM THE BRIEF, taken for a licence reason and recorded in the plan: nofrendo's src/nes/nes_ppu.c:1004-1064 is the same arithmetic in uint32 under an LGPL-2 legend whose last sentence requires any partial reproduction to carry it, and transcribing the expressions is reproduction in part. nofrendo is read for INTENT and cited; nothing is transcribed from it and no LGPL file ships |
| Palette-RAM address mirroring ($3F10/14/18/1C fold onto $3F00/04/08/0C), sprite evaluation with the 8-per-line cap and the overflow flag, the 8x16 tile arithmetic and the sprite-0 x!=255 exclusion | /Users/jggonz/Repos/nes-ref/agnes/agnes.c:946-949 (g_palette_addr_map, 32 bytes), :1142-1167 (eval_sprites), :1218-1272 (get_sprite_color_addr), :1169-1198 (emit_pixel's `sprite_ix == 0 && x != 255`) |
| Mappers 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3 incl. its scanline IRQ), and the 6x4 nametable mirroring table | /Users/jggonz/Repos/nes-ref/InfoNES/src/mapper/InfoNES_Mapper_000.cpp, _001.cpp, _002.cpp, _003.cpp, _004.cpp (1,055 lines total) + src/InfoNES.cpp:166-174 (PPU_MirrorTable) and :513-536. agnes's mapper4_set_offsets (/Users/jggonz/Repos/nes-ref/agnes/agnes.c:2425-2583) is the cleanest statement of MMC3's bank tables and the cross-check for MMC1; agnes has NO mapper 3, so InfoNES is the only source for CNROM. agnes's MMC3 IRQ TRIGGER is explicitly NOT used — it fakes the PA12 edge at a hardcoded dot with its own comment admitting it ("Should be 260 but it caused glitches in Kirby") |
| The 64-colour NES palette (192 bytes RGB888) | NESdev wiki 2C02G_U_wiki.pal, https://www.nesdev.org/w/images/default/e/e2/2C02G_U_wiki.pal — versioned, named for the chip, and the choice /Users/jggonz/Repos/nes-ref/nes-research.md section 5.2 already made. NOT nofrendo's shady_palette (src/nes/nes_pal.c:36-91, LGPL) and NOT agnes's unattributed table |
| NTSC timing: 341 dots/line, 262 lines, 113 2/3 CPU cycles/line, 29,780.5 cycles/frame, 60.0988 Hz, 256x240 visible | https://www.nesdev.org/wiki/Cycle_reference_chart, quoted in /Users/jggonz/Repos/nes-ref/nes-research.md section 5.1. The CANONICAL numbers are taken, not InfoNES's 113/29,828/vblank-at-243 roundings — with the rotating 113/114/114 triple the research recommends, which is exact over three scanlines and needs no fractional accumulator |
| THE WALL CLOCK the bracket paces on — which is NOT 60.0988 Hz and cannot be | apps/os88api.inc:3569-3583 (SPEC.md §53.5/§53.5.1): OSAPI_FSX_WAIT offers exactly three clocks — FSXW_TICK 18.2065 Hz, FSXW_VSYNC the CURRENT MODE'S retrace (70 Hz in mode 13h, 60 in Mode X and CGA 320x200, 50 on Hercules), and FSXW_FRAME which is 18.2065 Hz or 54.6195 Hz (FSX_SUBTICK 3) with FSXF_FASTTICK armed. NOTHING IS 60.0988. Pacing on the retrace, as the draft did, runs the game 16.5% FAST on a 486 in mode 13h and 17% slow on Hercules and makes the panel report 116% — adapter-dependent, and invisible on the target only because the target never reaches the clock. THIS PORT PACES ON AN ACCUMULATOR: FSXW_FRAME with FSXF_FASTTICK, 60.0988/54.6195 = 1.1002 emulated frames per sub-tick, carried as `acc += 110; while (acc >= 100) { frame(); acc -= 100; }` — one word, no long, 0.02% error, the same two-word fold discipline api_gaps already commits to. FSXW_VSYNC is used only to place the present inside the retrace on a machine that is ahead, never as the frame budget |
| The os8088 surfaces the port stands on: the fullscreen-exclusive bracket and its mode ids, the info block, the frame clocks and fsx_surf/fsx_page | SPEC.md §53.1-§53.10 (/tmp/nes/SPEC.md:64552-65200) and apps/os88api.inc:2424-2530 + 3550-3611 (FSXM_*, FSI_*, FSXW_*, FSXF_*). OSAPI_FSX_CAPS's own comment (apps/os88api.inc:2438-2449) is why the caps read is per-window and re-taken at use: "an answer banked in your entry proc describes the window you were LAUNCHED FROM, because yours does not exist yet". Worked consumers: apps/tank/tank.asm:431-475 (fsx_caps -> a mode per adapter) + apps/tank/tkgame.inc:220-350 (THE INPUT MODEL: int 16h for what was TYPED, OSAPI_KEY_DOWN for what is HELD), apps/missile/missile.asm:1002-1100, apps/cyclone/cyclone.asm:8086-8190 |
| The menu contract the flattening had to fit, and MENU_DIS's TWO meanings | apps/os88api.inc:1913-1972 and kernel/menu.inc:208. AM_LIST entries are {AMENU_TITLE, AMENU_ITEMS, AMENU_NITEM} — a flat array of NUL strings, no submenu field, no separator primitive. MENU_DIS (1) is the ONLY per-item decoration and os88api.inc:1945-1972 says the kernel "never highlights or selects it", so a disabled item CANNOT be clicked and has no refusal to answer; SPEC.md §12.2 also uses the same dithering as a RADIO MARK, where the dithered item is the SELECTED one. This package uses MENU_DIS in exactly ONE sense — UNAVAILABLE — and carries every check state in the LABEL instead (os88api.inc:1949-1956's own relabelling idiom, `i_paste0: db MENU_DIS, 'Paste (NoRam)', 0`), so `Frame skip: Auto` is one live item that cycles. Recorded in the SPEC so a later checkable item cannot reintroduce the collision |
| The ROMs that ship, their licences, URLs and SHA-256s, and the source-offer obligation on the GPL binaries | nes-roms.md sections 2.2, 2.3, 3.1, 3.2 and 4 — every hash computed there, not copied. The test ROMs (nes-roms.md section 1.2) are fetched into build/ and ship NOWHERE (nes-roms.md section 1.1: no licence exists for them) |
| The C package's shape: one translation unit, the shim, the four rules, the 61,440 ceiling, the overlay, and the menu/About/greying conventions | docs/C-TOOLCHAIN.md, apps/cc/os88.h (its header comment), SPEC.md §73.5/§73.5.1/§73.7/§73.8/§73.9/§73.14, SPEC.md §12.2 (menus), §47 (greying), §74.1 (the wake). Worked examples: apps/cword/ and apps/c64/ + C64-SPEC §4 (the 6510 core's register plan), C64-SPEC §13.0.1 (the MEASURED budget line — 39,384 image / 13,106 bss / 2,149 overlay / 52,490 resident from 6,913 C lines and 3,318 assembly lines), C64-SPEC §13.2 (the PLANNING table, whose runtime row is "crt0 + thunks (ccsmoke alone is 3,406) ~6,000"), C64-SPEC §13.1 (the file split), C64-SPEC §14 (names, disks, machines, harnesses). apps/c64/c64about.c:29-46 is the About panel's real bound: NINE rows at ~40 cells, `6 + 9*10 + 11 = 107` against the ~122 a framed CGA content box has |

---

## Scope

### Ships

- The FRONT PANEL window: the loaded ROM's name; Mapper, PRG ROM nKB, CHR ROM
  nKB, Mirroring, SRAM, 4 Screen, Trainer — InfoNES's own seven rows in its
  own order; TWO measured rates side by side (`NES 3%` from the emulated frame
  count and `2.2 s/frame` from the rendered one, both against os88_ticks, both
  composed from integers by os88_utoa — there is no printf and no float here);
  the controls legend; the state line (No ROM / Ready / Running / the
  refusal); and a FACT LINE, which is where every greyed item's full sentence
  is read, because a MENU_DIS item cannot be clicked and has nothing to toast.

- Three FLAT kernel menus (3 <= MENU_APPMAX 5; every item <= MENU_MAXCH 24 and
  every list <= MENU_POPMAX 11; AM_NAME = `InfoNES`): File > `Open ROM` /
  `Reset` / `Stop` / `Exit`; Options > `Frame skip: Auto` (one live item that
  cycles Auto/0/1/2/3 — the check state is in the label, never a MENU_DIS
  mark) / `Frame skip faster` / `Frame skip slower` / `Full screen` / `Clip
  top and bottom` / `\x01Mute (no APU)`; Help > `About InfoNES`. This is an
  ADAPTATION of a three-level Win32 menu, recorded as such.

- FULLSCREEN PLAY through SPEC.md §53's bracket, entered from the package's
  own assembly: VGA mode 13h (FSXM_VGA13) by default on every tier, Mode X
  (FSXM_MODEX) as a menu choice above the CPU_8086 tier; CGA and EGA 320x200x4
  (FSXM_CGA320); Hercules 720x348 mono (FSXM_HERC). Paced on FSXW_FRAME with
  FSXF_FASTTICK and a 110/100 accumulator, never on the retrace.

- The 2A03 core in hand-written 8086: all 151 official opcodes with their real
  cycle costs, page-cross and taken-branch penalties, the JMP () page-wrap
  bug, the zero-page wrap, BRK/NMI/IRQ sequencing, and the unofficial opcodes
  nestest exercises (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, SBC $EB, the
  NOP/DOP/TOP family). No decimal mode — a 2A03 has none.

- The PPU as a per-scanline composer in 8086 assembly over a pre-decoded tile
  cache: background with the loopy v/t scroll, attribute quadrants, the
  left-column masks, 8 sprites a line with the overflow flag, 8x8 and 8x16,
  both flips, front/behind priority, and sprite-0 hit by splitting the line's
  CPU budget at the sprite's X.

- CHR-ROM and CHR-RAM (a game that writes its own tiles through $2006/$2007 —
  robotfindskitten and RHDE on the shipped disk are exactly that path).

- Mappers 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3 with its scanline
  IRQ). Everything else refuses at load naming the number (InfoNES's own
  refusal, src/InfoNES.cpp:439).

- Controller 1 on the keyboard: arrows = D-pad, Z = B, X = A, Enter = Start,
  RShift or Space = Select. HELD KEYS ARE POLLED EVERY ~16 SCANLINES, NOT ONCE
  A FRAME: an emulated frame is >= 340 ms of wall time on the target and a
  human press is 80-150 ms, so a once-a-frame poll misses most presses
  entirely and stretches the ones it catches over twenty emulated frames. Each
  sweep ORs into a sticky pad latch that the emulated $4016 strobe clears, so
  a press that happened anywhere in the frame is delivered exactly once.
  os88_key_down takes no lock and touches no port (SPEC.md §9.7), so ~30 extra
  reads a frame is 1.4 ms of a 340 ms frame — 0.4%.

- Frame skip, automatic by default and adjustable on Page Up / Page Down
  (InfoNES's own keys): the CPU and the mappers run every frame so game logic
  advances; only the composer and the present are skipped. The panel shows
  what that costs in BOTH directions, because skip buys emulated speed by
  spending rendered frames.

- Top/bottom clip on C (InfoNES's own key) — live in Mode X and on Hercules,
  and GREYED with the fact in any 200-row mode, where 240 NES rows do not fit
  and the clip is forced on.

- Reset on R inside the bracket (InfoNES's own key, Linux add_key :262-266) —
  without it the only reset is leave/menu/re-enter.

- The 64-colour palette written straight to the VGA DAC, mirrored four times
  so the priority/backdrop tag bit in each pixel byte needs no masking pass;
  on CGA and Hercules the same mirror lives in 256-ENTRY reduction tables (4
  levels of luminance / a 2x2 ordered dither), because those adapters have no
  DAC and a 64-entry table indexed by a tagged byte reads past its end.

- The iNES loader with a LENGTH check none of the three references has, into
  heap claims: a bank table of segment bases for $8000-$FFFF, a CHR/VRAM
  claim, a 32KB tile-cache claim, and a RAM claim the core runs with DS
  pointing at. Every claim base 512-aligned.

- tools/nigetroms.py (getstories.py's shape: pinned URL, pinned SHA-256, a
  licence note per entry, a CATALOG.TXT writer, a NESROMS= knob, nothing
  committed) and four floppy geometries carrying Damian Yerrick's four
  mapper-0 games plus Mega Mountain on the larger three, with a CATALOG.TXT
  per geometry saying which games are on THIS disk and why (the `make cpmsw`
  precedent).

- An icon, a build-time .NES association (CC_ASSOC) AND the runtime
  os88_assoc_set, the About panel at NINE rows, and a record in The Wire's
  catalog.

### Present and greyed (SPEC.md §47 — the fact that greys it)

- **Options > `\x01Mute (no APU)`**

  The item carries the short form because 24 glyphs is the item width; the
  SENTENCE is on the panel's fact line, which is where a MENU_DIS item's
  reason has to live because the kernel never lets one be clicked: "No APU in
  this port: the PC speaker plays one square wave and the 2A03 mixes five
  voices." The pulse-1 arithmetic is recorded in the SPEC so the follow-up is
  costed rather than guessed: period t = ((reg3 & 7) << 8) | reg2 from
  $4002/$4003, muted below 8, and the PIT divisor is EXACTLY (t+1)*32/3 —
  16-bit, no table, no long.

- **Controller 2 (the legend's second column)**

  "One controller in this port." No front end in the InfoNES tree fills pad 2
  in from a keyboard either (InfoNES_System_Win.cpp:1016 sets it to 0), so
  this greys what the reference itself never had.

- **Options > `Clip top and bottom`, in any 200-row mode**

  "Clip is forced at 320x200 — 240 NES rows do not fit 200." The predicate is
  the LIVE FSI_H of the mode this package would enter on this window's
  display, read the way the Full screen item's predicate is read. The draft
  shipped this as a live toggle that was structurally inert on two of four
  modes: present, not greyed, not checked and silently ignored — the exact
  shape LESSONS.md §1 names.

- **Options > `Full screen`, on a machine whose adapter offers no mode**

  The greying predicate is OSAPI_FSX_CAPS's mask for THIS WINDOW'S DISPLAY,
  re-asked at use and never banked (apps/os88api.inc:2438-2449 — a banked
  answer describes the window you were launched from) — never OSAPI_VIDEO,
  which answers about the primary. On a two-card desktop the item greys per
  monitor and the refusal names the adapter.

- **File > `Open ROM`, when the mapper is not one of 0/1/2/3/4**

  Refused at load, not greyed — the mapper number is not knowable before the
  header is read, so this is §47's attempted-and-reported half and it CAN
  toast. The refusal is InfoNES's own sentence with the number in it: "Mapper
  #%d is unsupported."

- **File > `Open ROM`, when the ROM is larger than the heap can hold**

  "INFONES wanted %u KB and the largest free block is %u KB" — RunCPM's
  refusal shape, quoting what it asked and what os88_mem_largest_kb answered.
  The hard cap is 256KB PRG + 128KB CHR (the bank table's size), stated on the
  panel.

### Absent — dropped, with the reason

- Options > Screen > Size 1x/2x/3x. DROPPED RATHER THAN GREYED, for two
  independent reasons the SPEC records: there is no submenu to grey them in,
  and MENU_DIS already means UNAVAILABLE in this package's one Options list,
  so shipping a dithered `1x` (the .rc's CHECKED default) beside dithered
  `2x`/`3x` (refused) would be identical pixels with opposite meanings — worse
  on 1bpp, where grey rounds to black. The arithmetic that would have been the
  fact is recorded instead: the NES picture is 256x240 and the largest mode
  this display offers is whatever FSI_W/FSI_H report — 320x240 on a VGA,
  320x200 on CGA and EGA, 720x348 on Hercules.

- MENUITEM SEPARATOR in the File menu. The kernel's item array is NUL strings
  and nothing else; there is no separator primitive. Recorded as an
  adaptation.

- The APU as a sound source of any kind. No stream, ever (SPEC.md §34:
  OSAPI_SND_STREAM is a driver's multi-verb protocol and is not wrapped for
  C). InfoNES_pAPU.cpp's 1,068 lines have no subset that fits: every channel
  carries a 32-bit phase accumulator and the mix is per sample at 11-44 kHz.

- Battery-backed SRAM save/load (.srm). DROPPED RATHER THAN GREYED (C64-SPEC
  §10.3's shape): InfoNES's menu has no item for it — it writes the file on
  exit — so there is no surface to grey. Recorded in the SPEC with the reason:
  none of the shipped games sets the SRAM bit, the format is a hand-rolled RLE
  whose reader and writer each declare an 8,192-byte AUTOMATIC (two cc8086
  refusals at once), and the file would belong in SYSTEM/APPDATA/ per SPEC.md
  §19.9. It is a costed follow-up, not a gap.

- A live windowed picture. Decision 2 makes it optional and lowest-priority
  and this plan does not spend a wave on it: the window is the front panel,
  the game is fullscreen. If it lands later it is the 4bpp os88_gfx_blit4 path
  at a low frame rate over the SAME 256-byte line the composer already
  produces — never a second renderer.

- Save states. InfoNES has none in this tree; agnes's is an 84,560-byte
  host-ABI memmove of one struct (agnes.c:482-484, 572-602) and its screen
  buffer alone is 61,440 — the whole package ceiling. Not planned.

- Help > ROM information as a menu item. DROPPED RATHER THAN GREYED: its seven
  rows are permanently ON the window (decision 2), so the item would open a
  dialog showing what is already on the glass. The SPEC records the
  substitution.

- Help > Version information as a separate item — folded into About InfoNES,
  which is where SPEC.md §12.2 puts it and what os88_about_set() names.

- `P = pause` inside the bracket. It was in the draft and it is in NO InfoNES
  source: there is no 'p' or 'P' case in add_key, none in the SDL front end,
  and README.md's "System" lists four keys. A string typed from memory,
  deleted.

- InfoNES's `L` (load) and `I` (ROM info) in-bracket keys: the file dialog is
  a menu command here and the ROM info is permanently on the panel.

- File > Stop's InfoNES meaning of "halt the emulation thread": there is no
  second task here (the machine runs only inside the bracket), so Stop unloads
  the ROM and returns the panel to its No ROM state. Recorded as an
  adaptation, not a drop.

- `Open ROM` (the original is 開く, "Open", no ellipsis) and `Full screen`
  (InfoNES has no such menu item at all — its fullscreen is Alt+Enter,
  SDL:504-506) are ADAPTATIONS, recorded here beside Stop's rather than
  presented as traced strings.

- PAL timing, iNES 2.0's extended sizes and submappers, expansion audio, the
  Zapper, the four-screen VRAM arrangement (refused at load with the flag
  named), and mappers 5 and above.

- Everything host-side that never crosses onto the machine: agnes's SDL2 front
  ends, its parson/kgflags vendored parsers, nofrendo's overlay GUI, its
  config file and its SNSS savestates.

---

## Files

| file | holds | resident |
|---|---|---|
| `apps/infones/infones.c` | The translation unit's ROOT and the only file the Makefile names as the compiler's input. The Apache-2.0 + InfoNES attribution header with the section 4(b) modification notice; every prototype (clang in the harness is stricter than SmallerC about these); os88_main (claim sizing off os88_mem_largest_kb, the window, the three-menu set, about_set, assoc_set, onwake install, os88_key_down ARMED here per SPEC.md §9.7 — ask once and ignore the answer); os88_paint; os88_onkey; os88_onclick; os88_oncmd (resident: Full screen must work on a disk with no .OVL); os88_onfile; os88_onwake (calls ovl_rom_load off the desktop's lock, announced with a toast before the long walk — RunCPM's lesson, and the FIRST callback is where an overlay may first be reached, never os88_main); and the #includes in dependency order. | yes |
| `apps/infones/nirom.c` | ovl_*: the iNES reader — the magic compare, the LENGTH validation against the sizes the header claims (none of the three references has this and every byte off a floppy is hostile), the DiskDude guard on the high mapper nibble, the four flag bits, the trainer skip, the mapper refusal naming the number, and the load into claims. MOVED TO THE OVERLAY FROM THE FIRST COMMIT: it runs once per Open, from a callback, which is exactly §73.14's frequency test, and it buys back ~1,800 bytes for the mappers, which cannot move. Its refusal path when the .OVL is absent is the ordinary 0-return, printed on the panel's state line. One os88_file_read_seg into a transient staging claim for a ROM <= 64KB (every shipping game and every blargg single), and cluster-window os88_file_read_at plus the 16-byte shift for the larger ones. Builds the PRG bank table of SEGMENT BASES and the CHR claim; the tables themselves are resident bss, because only CODE moves. | no |
| `apps/infones/nimap.c` | Mappers 0/1/2/3/4 as five blocks behind one switch (one translation unit, so InfoNES's 138-file #include scheme collapses), the ROMPAGE/VROMPAGE macros redefined as index arithmetic over the bank table, the 6x4 nametable mirroring table, and MMC3's scanline IRQ counter driven from the HSync hook the frame loop already calls. Every `%` against the bank count is kept, not 'optimised' to a mask — a 6-bank ROM is why. | yes |
| `apps/infones/nippu.c` | The PPU register file $2000-$2007: the control decode, the shared write latch, $2002's read-clears-vblank-and-flipflop, the buffered $2007 read, the palette write with its every-4 mirror rule, the loopy v/t pair and its per-line latch and increment, $4014 OAM DMA (with the fast path for a source wholly inside the 2KB RAM claim), sprite evaluation with the 8-per-line cap, the 64-colour palette table, the DAC image and the TWO 256-entry reduction tables built at load from the 64 real entries. Calls niband.inc once per line and never per pixel. | yes |
| `apps/infones/nirun.c` | The frame loop: the rotating 113/114/114 cycle triple, 262 scanlines, the sprite-0 budget split, the mapper HSync hook, NMI at 241, the frame-skip counter, the adaptive skip estimator, the 110/100 wall-clock accumulator against FSXW_FRAME+FSXF_FASTTICK, the pad sweep every ~16 scanlines into the sticky latch, the two-word frame and cycle counters folded per slice (no long), and BOTH speed measurements — emulated frames and RENDERED frames against os88_ticks, printed as an integer percentage and as tenths of a second per painted frame. Also the pad latch and the $4016/$4017 shift. | yes |
| `apps/infones/nipanel.c` | The front panel: the field table, THE SHADOW of what each field currently shows on the glass, the delta draw (only fields whose text changed are re-lettered, one os88_font_run each), the layout read off the LIVE screen size, the controls legend, the FACT LINE that carries each greyed item's sentence, the two rate fields composed from integers by os88_utoa, and the shadow invalidation after any panel owns the glass (a dialog, the About card, the file dialog, or the bracket's exit repaint). | yes |
| `apps/infones/nimenu.c` | The three FLAT menu tables with every string recorded as a translation of the named .rc line, the OS88_MENU_DIS greying with its FACT in a comment beside each item, the label-swap table that carries `Frame skip: Auto` / `Frame skip: 2` (check state in the LABEL — MENU_DIS is used in exactly one sense in this package), the menu-set struct (declared WITHOUT const — os88_menu_set writes oncmd into it), and the greying predicates (one predicate per item, feeding the greying, the panel's fact line and the keyboard refusal alike). | yes |
| `apps/infones/nicmd.c` | ovl_*: every menu command SHELL — Reset, Stop, frame-skip up/down, the clip toggle, the label swaps, and the Open ROM dialog's setup. NO refusal toasts for greyed items: the kernel never lets a MENU_DIS item be highlighted or selected, so there is no click to answer and code written for one is unreachable. Bodies that a keystroke or a frame touches are NOT here. Full screen is answered in the RESIDENT half for apps/c64's reason: it is the one command that must work on a disk whose .OVL is missing. | no |
| `apps/infones/niabout.c` | ovl_about_show — the About panel, NINE ROWS at ~40 cells, apps/c64/c64about.c:29-46's measured shape (`6 + 9*10 + 11 = 107` against the ~122 a framed CGA content box has), with a comment saying the row count is a 640x200 compatibility constant. The rows: `About InfoNES`; `A fast and portable NES emulator`; `InfoNES v0.96J (@fe3295c0)`; `os8088 port: NTSC, no APU`; `Copyright (c) 1999-2005`; `Jay's Factory`; `Apache-2.0 - see LICENSE.TXT`; `Scroll model from agnes (MIT)`; `Ported by <name>`. The email address, the section 4(b) modification notice and the Nintendo trademark sentence are NOT here — the first is not a licence requirement, and the other two go in the source headers and in README.TXT on the disk, because at 40 cells each of them wraps to two rows and the draft's eight content items reached twelve. | no |
| `apps/infones/nicpu.inc` | The 2A03 core, hand-written 8086 in apps/c64/c64cpu.inc's shape (C64-SPEC §4.1's register plan, minus decimal): A in AL, NZC in AH by the lahf layout, V/I/B in CH, X in CL, Y in DL with DH the memory-data byte, PC in SI, S in DI as the full $0100+S address, BX the dispatch scratch, DS the RAM claim, ES biased so [es:si] is PC's byte with the two-compare boundary guard. A 256-entry `jmp [cs:bx+tab]`, a 256-byte cycle table, P packed and unpacked once per call. | yes |
| `apps/infones/niband.inc` | The scanline composer and the tile-cache decoder: the eight-way-unrolled background run over the cache with its two partial end blocks for the fine-X phase and the nametable flip at the wrap; the sprite pass back-to-front into the same 272-byte line with the transparency test, both flips and the priority/strike tag bits; and the 1KB-bank decoder (two plane bytes to eight one-byte-per-pixel outputs). The line buffer is 272 bytes with the visible window at +8 — nofrendo's overdraw fact, and its live bug: the composer writes up to 7 pixels before the line and 8 after it. Also the serial dump entry the assembly-side reference check reads. | yes |
| `apps/infones/nifsx.inc` | THE BRACKET, in assembly because SPEC.md §53 is deliberately unwrapped for C: ni_fsx_caps (a cdecl helper so nimenu.c can grey the mode and clip items off the LIVE per-window mask, re-asked at use), the OSAPI_FSX_RUN call with FSXF_FASTTICK and the near entry it names, OSAPI_FSX_MODE per adapter, OSAPI_FSX_SURF, OSAPI_FSX_WAIT on FSXW_FRAME as the frame budget (never FSXW_VSYNC, which is the adapter's rate and not the NES's), OSAPI_FSX_PAGE for Mode X's flip, the DAC programming (64 colours mirrored four times), the four PRESENT routines (13h rep movsw, Mode X's four plane selects and stride-4 gather, CGA's 2-bit pack over interleaved banks, Hercules's 1bpp dither over four banks), the int 16h typed-key poll and the os88_key_down sweep, and the frame loop itself. NO ovl_* is called from inside a bracket, ever. | yes |
| `apps/infones/nimem.inc` | The cross-segment movers: claim-to-claim copies, the 16-byte shift the iNES header forces on a cluster-window read, the OAM DMA block move, and the tile-cache stores. The only place ES is loaded, marked `; cc8086:allow` per line with its reason, and gated on a real x86 under SS != DS by hosttest/nimemtest.asm. | yes |
| `apps/infones/infones.asm` | The shim: %define CC_PKG_NAME 'INFONES', CC_HAS_ONKEY / ONCLICK / MENUS / ABOUT / FDLG / ONWAKE / OVL (no WORKER — the machine runs only inside the bracket and File > Exit is os88_wm_close), CC_ICON "infones/icon.inc", CC_ASSOC "infones/niassoc.inc", CC_STACK_CLASS, %include cc/crt0.asm, %include infones.gen.asm, then the four .inc files, CC_IMAGE_END. | yes |
| `apps/infones/icon.inc` | A 16x16 1-bit controller drawn for this port (16 mask words then 16 data words, bit 15 leftmost). InfoNES's own InfoNES.ico is the reference LOOK only, never copied. | yes |
| `apps/infones/niassoc.inc` | The build-time association block: a count byte and one OS88_ASSOC_EXT line for `NES`, so a ROM beside the program opens on the FIRST double-click with no prior run. (LESSONS.md §9's line saying the C SDK has no build-time association block is STALE — apps/cc/crt0.asm:292-321 implements CC_ASSOC; this plan uses it AND os88_assoc_set, which are not alternatives.) | yes |
| `apps/infones/hosttest/niuitest.c` | The host harness: the whole C compiled with clang against a stub os88.h (a second copy, ahead of apps/cc on the include path, so drift is a COMPILE failure), with a PIXEL model of the glass for the panel; it drives the program like a user, asserts field for field that the glass shows what the shadow says, prints the cost table in calls, cells and milliseconds, and replays the agnes-shaped per-frame recording (input byte + frame hash) against goldens agnes generates on the host. It MODELS the assembly composer in C, so tools/niref.py run against ITS output checks the MODEL — which is stated in the wave's done_when, and is exactly why the assembly gets its own gate (C64-SPEC §9.8's all-black 2x screendump is the cautionary case). | no |
| `apps/infones/hosttest/nicputest.asm + .sh` | THE CPU GATE, in apps/c64/hosttest/c64cputest.asm's shape and run by `make nicputest` ALONE — never from build.sh, which is what apps/c64/build.sh's own header says in capitals about c64cputest: it takes minutes and it needs a fetched fixture, and a fresh clone must not stall or fail on either. nicpu.inc assembled standalone against a STUB BUS whose contract is written down beside it ($2002 returns bit 7 set and clears it on read; $2000/$2001/$2005/$2006/$2007 accept writes and discard; $4016/$4017 read open bus; $0000-$07FF and $6000-$7FFF are RAM; $8000+ is the loaded PRG), running nestest.nes from $C000 in automation mode and diffing PC / opcode bytes / A X Y P SP / CYC against nestest.log line by line (the first differing line names the wrong instruction), then blargg's instr_test-v5 rom_singles 01-16 through the $6000/$6001-3/$6004 protocol. Negative controls, one per row. It fetches its own pinned fixture rather than going through the ROM fetcher. | no |
| `apps/infones/hosttest/nisystest.asm + .sh` | THE WHOLE-EMULATOR ROM GATE, and it is a NEW artifact this plan adds because the draft had none: `make nicputest` is nasm-only and by construction contains no PPU, no mapper and no frame loop, so ppu_vbl_nmi and cpu_dummy_reads had nowhere to run. This is the package itself in a headless debug build — a `NITEST=1` build that loads a named ROM on the wake, runs it with no bracket and no present, polls $6000 through the standard protocol and prints the $6004 string with runcpm's rc_say shape (toast AND console), read back over QMP. It carries ppu_vbl_nmi rom_singles 01-10, cpu_dummy_reads, palette_ram and instr_timing. cpu_timing_test6 and the branch_timing_tests family report ONLY on a rendered PPU screen and have no $6000 status byte at all: they are manual screendump evidence in wave 4 and no gate rests on them. | no |
| `apps/infones/hosttest/nimemtest.asm + .sh` | niband.inc's and nimem.inc's entry points on a real x86 under SS != DS with an ES sentinel and four discipline negative controls (ES, DF, BP, DS): the tile-cache decoder against hand-computed tiles, the composer's overdraw bounds, each of the four present routines against hand-computed destination bytes, and the 16-byte shift. It ALSO writes the composed 256x240 palette-index frame out over the serial port to build/niref-frame-asm.bin, which is the only path by which the SHIPPING assembly's output reaches tools/niref.py. Run by build.sh — it is seconds, not minutes. | no |
| `apps/infones/build.sh` | The standalone chain and the FAST host checks that each stop the build, in apps/cword/build.sh's and apps/c64/build.sh's shape: niuitest, `tools/niref.py --selftest` then `--check` against both the model's and the assembly's dumped frames, nimemtest, then smlrcc -tiny -S, tools/cc8086.py, one nasm from the shim, tools/os88ovl.py --trim, tools/os88pkg.py, tools/os88disk.py in four geometries with --verify. `make nicputest` and `make nisystest` are NOT here and their headers say why. | no |
| `tools/nigetroms.py` | tools/getstories.py's shape: a manifest of pinned URL + SHA-256 + a licence/source-offer note per entry, an iNES header parse that verifies mapper and PRG/CHR against the manifest, write_catalog() emitting a PER-GEOMETRY CATALOG.TXT and README.TXT (which games are on THIS disk and why the others are not — the `make cpmsw` / GAMES.TXT precedent — plus the GPL-3 source offers with repository URL and tag, and the Zlib and All-Permissive copyright lines), a NESROMS= knob for the user's own dumps, a separate test-ROM group fetched into build/nesroms/ that ships NOWHERE, and nothing committed. | no |
| `tools/niref.py` | tools/c64ref.py's role: an INDEPENDENT Python reference compositor written from nesdev documentation, rendering the same PPU state to a 256x240 palette-index image and compared bit for bit against BOTH dumps — niuitest's C model and nimemtest's SHIPPING ASSEMBLY frame. It carries `--selftest`, which injects a one-bit defect and requires the compare to fail, because a check that cannot fail is not a check (c64ref.py's own rule). This is what validates scroll, attributes, 8x16 sprites, both flips and priority — a cell-identity glass model provably cannot. | no |
| `tests/niband/` | The icount bench (tests/c64band's shape, `make nibandbench`): the composer priced per pixel and per line for background-only, background+sprites and the worst case; the tile-cache decode priced per 1KB bank; each of the four present routines priced per line; and the CPU core priced per 6502 cycle and per core entry. SPEC.md's tier table is written from these numbers and they become a new Set in PERFORMANCE.md. Registered in tests/suite.py. | no |

---

## Budget — PLANNED

| | planned |
|---|---|
| resident image | **36,000** |
| resident bss | **6,000** |
| `INFONES.OVL` | **4,500** |
| **resident total** | **~42,000** of 61,440 |

**Basis.**

REVISED UPWARD FROM THE DRAFT, which under-counted the runtime and the line
estimate and so claimed 19,500 bytes of headroom that is really about 13,000.
MEASURED BASIS, apps/c64 (C64-SPEC §13.0.1 and `wc -l apps/c64/*.c *.inc`
here): 6,913 lines of C (c64.c 1711, c64io.c 1076, c64kbd.c 1154, c64scr.c
1813, c64menu.c 460, c64cmd.c 456, c64load.c 109, c64about.c 134) and 3,318
lines of assembly (c64cpu.inc 2313, c64band.inc 507, c64mem.inc 498) shipped
39,384 image + 13,106 bss + 2,149 overlay = 52,490 resident. THE RUNTIME IS
~6,000, NOT 3,000: C64-SPEC §13.2's own planning row is "crt0 + thunks
(`ccsmoke` alone is 3,406) ~6,000", and ccsmoke is the MINIMAL package — one
that links menus, a file dialog, key_down, mem_claim, assoc, about and the
wake links MORE thunks. Backing out 3,318 asm lines x 2.32 b/line (7,698) and
6,000 of runtime leaves 25,686 for 6,913 C lines = ~3.7 b/line, and the
2,149-byte overlay over c64cmd+c64about's 590 lines is 3.64 — so ~4.0 b/line
is the honest C figure and 4.3 the conservative one. THIS PORT'S LINE
ESTIMATE, anchored to c64's measured scale rather than 34% under it (the draft
budgeted nimap.c at 700 lines against InfoNES's own five mapper files at
1,055, and nippu.c at 900 against c64io.c's 1,076 for a strictly harder
register file): C — infones.c 900, nirom.c 450, nimap.c 1,000, nippu.c 1,200,
nirun.c 550, nipanel.c 650, nimenu.c 350, nicmd.c 450, niabout.c 130 = 5,680,
of which 1,030 (nirom + nicmd + niabout) are OVERLAY FROM THE FIRST COMMIT;
assembly — nicpu.inc 2,000 (a 2A03 is a 6510 minus decimal, so under
c64cpu.inc's 2,313), niband.inc 1,000, nifsx.inc 800, nimem.inc 300 = 4,100.
SO: resident image = 4,650 C lines x 4.3 (20,000) + 4,100 asm lines x 2.32
(9,500) + runtime (6,000) = ~35,500, called 36,000; overlay = 1,030 x 4.3 =
4,400, called 4,500 for the strings and the dialog table. BSS, BUFFER BY
BUFFER: the composed line 272 + the sprite scratch 272 + the packed present
line 320 (13h's worst) + the 240-entry row-drop table + the panel shadow (14
fields x 40 = 560) + the DAC image 192 + the 64-entry NES palette 192 + TWO
256-ENTRY REDUCTION TABLES (512, and this is a correction: a 64-entry table
indexed by a pixel byte carrying the priority tag reads 192 bytes past its end
on CGA and Hercules, which have no DAC to mirror — the mirror moves into the
table) + the sprite-eval list 32 + the pad, sticky latch and key state 40 +
the bank table (16 words) + the core's boundary and counter words + the
wall-clock accumulator + menu, label-swap and message scratch ~1,200 + slack =
~4,200, rounded UP to 6,000 because the first full build always overshoots
(LESSONS.md §5: cword's first was 63,954). RESIDENT TOTAL ~42,000 of 61,440 —
about 19,400 spare and 13,000 UNDER §73.9's 55,000 trigger, which is the
headroom wave 4's four mappers and the MMC3 IRQ are budgeted against, and it
is tighter than the draft believed. NOTHING BIG IS IN THE PACKAGE: the tile
cache (32,768), PPU/nametable RAM, OAM, the 2KB CPU RAM, the 8KB SRAM, the CHR
bank (8KB) and every PRG bank live in heap claims — ~88KB for an NROM title,
sized from os88_mem_largest_kb at launch, every base 512-ALIGNED, and REFUSED
with the arithmetic if the heap cannot answer (SPEC.md §47, RunCPM's refusal
shape). CC_HAS_OVL is on from the first commit AND THREE FILES ARE ALREADY OUT
(C64-SPEC §13.1's rule: the alternative is discovering at 55,000 that the code
is not the kind that can move), and `os88pkg`'s five-figure line — resident
image, bss, .OVL, resident shims, largest frame — is reported at the end of
every wave.

---

## API gaps — what the SDK does not give, and what is done instead

**Need:** Enter the fullscreen-exclusive bracket, set a mode, read the info
block, ask which display and which depth, wait on a frame clock and flip a
Mode X page.

  **Slot:** OSAPI_FSX_CAPS 0x02C0 / FSX_RUN 0x02C8 / FSX_MODE 0x02D0 /
  FSX_WAIT 0x02D8 / FSX_SURF 0x03F8 / FSX_PAGE 0x04E8 all exist and are
  DELIBERATELY not wrapped for C (apps/cc/os88.h: "the rules are the whole
  feature and none of them is checkable from C").

  **Action:** Write it in the package, not in the SDK: apps/infones/nifsx.inc
  carries the whole bracket plus one cdecl helper (ni_fsx_caps) so nimenu.c
  can grey the mode and clip items before entry — and the helper is CALLED AT
  USE, not banked, because os88api.inc:2438-2449 says a banked caps answer
  describes the window you were launched from. No thunk, no crt0 change, no
  bytes spent in every other C package. apps/tank/tank.asm:431-475 and
  apps/cyclone/cyclone.asm:8086-8190 are the shapes it copies.

**Need:** A 60.0988 Hz frame budget for the emulated machine.

  **Slot:** NONE EXISTS AND NONE CAN. OSAPI_FSX_WAIT offers FSXW_TICK (18.2065
  Hz), FSXW_VSYNC (the CURRENT MODE's retrace — 70 Hz in mode 13h, 60 in Mode
  X and CGA, 50 on Hercules) and FSXW_FRAME (18.2065, or 54.6195 with
  FSXF_FASTTICK armed; FSX_SUBTICK = 3). apps/os88api.inc:3569-3583.

  **Action:** No new slot — an accumulator in nifsx.inc. FSXF_FASTTICK +
  FSXW_FRAME gives 54.6195 Hz, and 60.0988/54.6195 = 1.1002, so `acc += 110;
  while (acc >= 100) { one_frame(); acc -= 100; }` — one word, no long, 0.02%
  error, and FSXW_FRAME waits by YIELDING rather than spinning. The draft
  paced on the retrace, which is adapter-dependent: on a 486 in mode 13h that
  is 70 Hz, the game runs 16.5% fast and the panel reports 116%. Recorded in
  the SPEC's tier table with the residual error stated.

**Need:** Poll what was TYPED inside the bracket (Esc, F, R, C, Page Up/Down),
where no events are dispatched.

  **Slot:** None exists for C — int 16h is legal here because the bracket IS
  the UI task (SPEC.md §53.1), but nothing wraps it.

  **Action:** A ten-line assembly shim in nifsx.inc
  (apps/tank/tkgame.inc:234-246's two-reader model), and the queue is DRAINED
  rather than sampled. Held keys need no shim at all: os88_key_down IS wrapped
  (apps/cc/os88.h:760) and is what drives the D-pad — swept every ~16
  scanlines into a sticky latch, not once a frame, because an emulated frame
  is >= 340 ms of wall time on the target and a 100 ms press falls between two
  frame-rate polls. os88_key_down takes no lock and touches no port (SPEC.md
  §9.7), so the sweep is 0.4% of the frame.

**Need:** Write the 64 NES colours into the VGA DAC, mirrored at
0x40/0x80/0xC0 so the priority tag bit carried in each pixel byte costs no
masking pass.

  **Slot:** None — and none is wanted: SPEC.md §53.7 makes the DAC, the
  sequencer, the CRTC and the graphics controller the app's inside a foreign
  mode, and the exit mode set reprograms everything they touch.

  **Action:** Write it in nifsx.inc. AND EXTEND IT: CGA and Hercules have no
  DAC, so their luminance reduction tables are 256 entries built at load by
  filling the 64 real ones four times over — the identical mirror, moved from
  the hardware into the table, at 512 bytes of bss and zero cycles. A 64-entry
  table indexed by a tagged pixel byte reads up to 192 bytes past its end and
  produces a wrong colour rather than a crash.

**Need:** Read PRG bank n of a ROM file, whose offset is 16 + n*16384 because
of the iNES header.

  **Slot:** OSAPI_FILE_READ_AT is wrapped (os88_file_read_at) but its OFFSET
  AND CAPACITY MUST EACH BE A WHOLE NUMBER OF CLUSTERS or it is FERR_NAME
  (apps/cc/os88.h:827-830). 16 mod any cluster size is not 0, so no PRG bank
  can be read directly.

  **Action:** No new slot. A ROM <= 64KB (every shipping game, every blargg
  single, nestest) is read whole with os88_file_read_seg into a transient
  staging claim and copied out with the 16-byte skew for free; a larger ROM is
  read in cluster windows and shifted down 16 bytes by nimem.inc's mover, one
  extra pass over the ROM at load. Recorded because it is invisible until it
  refuses, and wave 4 writes the gate for the shift path.

**Need:** 32-bit quantities: the emulated cycle count, the frame counter, a
file offset past 65,535.

  **Slot:** None can exist — `long` does not parse and `float` is poisoned
  (SPEC.md §73.7). OSAPI_XMEM_* is not wrapped for exactly this reason.

  **Action:** Two words with an explicit fold per slice, C64-SPEC §4.2's
  discipline: the core's own budget is a SIGNED word and a scanline is 114
  cycles, so the cap is nowhere near. Written in the SPEC so no wave
  rediscovers it.

**Need:** Print a rate as `2.1 fps` or `%3d%% of a NES`.

  **Slot:** None. apps/cc/os88.h:181-182 #defines float and double to error
  tokens and there is no printf family (os88.h's size section); the only
  converters are os88_utoa and os88_itoa.

  **Action:** Hold the rate in TENTHS as a scaled int and compose it —
  os88_utoa(t/10), '.', '0'+(t%10) — and hold the percentage as a plain
  unsigned. `%4.1f` and `%3d%%` are struck from the plan text so no wave
  writes a format string against a runtime that has no formatter.

**Need:** A submenu, a menu separator, and a check mark that is not the
disabled mark.

  **Slot:** NONE, and this is the plan's largest adaptation. AM_LIST is
  {AMENU_TITLE, AMENU_ITEMS, AMENU_NITEM} (apps/os88api.inc:1917-1922) — a
  flat array of NUL strings; apps/cc/os88.h:236-247 mirrors it in 6 bytes with
  a build-time sizeof assertion. MENU_DIS (1) is the only per-item decoration
  and SPEC.md §12.2 already spends it on TWO meanings — unavailable, and the
  radio mark where the dithered item is the SELECTED one.

  **Action:** Flatten in the package. Options becomes six items in one flat
  list, all <= MENU_MAXCH 24 and well inside MENU_POPMAX 11; the Size submenu
  is DROPPED (recorded in `absent` with the arithmetic); the File separator is
  dropped; and check state is carried in the LABEL by pointing the item at a
  different string — os88api.inc:1949-1956's own relabelling idiom — so
  MENU_DIS means exactly one thing here. Recorded in the SPEC so a later
  checkable item cannot reintroduce the collision.

**Need:** A surface on which a greyed item's §47 FACT can be read.

  **Slot:** None from the menu: os88api.inc:1945-1972 has the kernel refuse to
  highlight or select a MENU_DIS item, so there is no click, no command and
  nothing to toast. A refusal handler written for one is unreachable code.

  **Action:** Two places, neither of them a toast. The SHORT form goes in the
  item label within 24 glyphs (`\x01Mute (no APU)`), which is os88api.inc's
  own '(NoRam)' answer; the SENTENCE goes on the panel's permanent FACT LINE,
  beside the ROM info that is already there. A keyboard shortcut still answers
  — os88api.inc:1957-1959 — so pressing M inside the bracket prints the fact
  where the user is looking.

**Need:** A heap claim that can be compacted, so a 256KB PRG claim does not
pin the heap.

  **Slot:** OSAPI_MEM_MOVABLE exists and has NO C thunk (it takes a relocation
  PROC, which a C package cannot name).

  **Action:** None — and this is a decision, not a gap left open: every claim
  here stays PINNED by design, because the core caches segment bases in its
  bank table and in ES, and a compaction would invalidate both silently.
  Recorded in the SPEC beside the claim table.

**Need:** Sound.

  **Slot:** OSAPI_SND_TONE is wrapped (os88_snd_tone); OSAPI_SND_FM /
  SND_STREAM / SND_PLAY are not, and are driver verb protocols apps/cc/os88.h
  declines by name.

  **Action:** Grey Options > Mute with the fact on the panel's fact line. The
  pulse-1 arithmetic is recorded (the PIT divisor is exactly (t+1)*32/3) so
  the follow-up is costed; nothing is added to the SDK.

**Need:** A .NES double-clicked on a cold boot, before the program has ever
run.

  **Slot:** CC_ASSOC exists in apps/cc/crt0.asm:292-321 (the build-time block,
  SPEC.md §54.6) and os88_assoc_set is wrapped (the runtime half).

  **Action:** Use BOTH — they are not alternatives. And correct LESSONS.md
  §9's stale line saying the C SDK has no build-time association block, in the
  same PR.

---

## Waves

Five waves, in order. Each is one `workflows/implement.js` run. The
`done_when` text is the wave's acceptance and is **not to be condensed**: it
is what the independent verifier checks with its own eyes.

### Wave 1 — The package, the flat menus, the front panel, the ROM loader, the 2A03 — and the gate that must pass before a pixel exists

**What it adds**

- The skeleton: shim with CC_HAS_OVL, CC_ICON, CC_ASSOC on from the FIRST
  commit, AND THE SPLIT ALREADY MADE — nirom.c, nicmd.c and niabout.c are
  ovl_* from commit one (C64-SPEC §13.1's rule, and the revised budget's
  ~42,000 resident is what makes it necessary rather than tidy). EVERY FILE IN
  THE PLAN'S TABLE EXISTS FROM THIS WAVE, empty bodies included, #included and
  %included from the shim, AND every one is a written Makefile prerequisite in
  this wave, because make cannot see through #include or %include and the
  symptom is a change that reads as if it did nothing.

- The window and the FRONT PANEL with its shadow and delta draw designed
  before any field is written: the seven InfoNES ROM-info rows in InfoNES's
  own order, the state line, the controls legend, the FACT LINE, and the two
  rate fields showing `--`. Layout read off the LIVE screen size, not off
  640x480.

- THE FLAT MENU TABLES, written HERE and not against the .rc's shape: three
  menus, every string <= MENU_MAXCH 24, every list <= MENU_POPMAX 11, the Size
  submenu dropped, the separator dropped, check state carried in the label,
  MENU_DIS used in exactly one sense. Each string carries a comment naming the
  .rc line it TRANSLATES. AM_NAME = `InfoNES`, about_set.

- The iNES loader as ovl_rom_load: magic, LENGTH validation, the DiskDude
  guard, the flags, the trainer skip, the mapper refusal naming the number,
  the heap claims sized from os88_mem_largest_kb with a refusal quoting both
  numbers, every claim base 512-ALIGNED, the PRG bank table of segment bases,
  and the CHR claim. Called from the WAKE — the first callback, never
  os88_main — announced with a toast first, and its .OVL-missing refusal
  printed on the state line.

- The 2A03 core, whole: 151 official opcodes with their cycle costs, the
  page-cross and taken-branch penalties, the JMP () page wrap, the zero-page
  wrap, BRK/NMI/IRQ sequencing, and the unofficial opcodes nestest exercises.
  No decimal mode.

- tools/nigetroms.py — needed HERE and not in wave 4, for the ROM disks; `make
  nicputest` fetches its OWN pinned nestest/blargg fixture (c64cputest's
  precedent) so a fresh clone's `make infones` needs no network.

- The host harness skeleton and the three assembly gates' skeletons. build.sh
  runs ONLY the fast ones — niuitest, niref --selftest, nimemtest — because
  apps/c64/build.sh's header says in capitals that the CPU gate is minutes and
  does not belong in a build.

**Files it touches**

- `apps/infones/infones.c`
- `apps/infones/nirom.c`
- `apps/infones/nipanel.c`
- `apps/infones/nimenu.c`
- `apps/infones/nicmd.c`
- `apps/infones/niabout.c`
- `apps/infones/nippu.c`
- `apps/infones/nirun.c`
- `apps/infones/nimap.c`
- `apps/infones/nicpu.inc`
- `apps/infones/niband.inc`
- `apps/infones/nifsx.inc`
- `apps/infones/nimem.inc`
- `apps/infones/infones.asm`
- `apps/infones/icon.inc`
- `apps/infones/niassoc.inc`
- `apps/infones/hosttest/niuitest.c`
- `apps/infones/hosttest/nicputest.asm`
- `apps/infones/hosttest/nisystest.asm`
- `apps/infones/hosttest/nimemtest.asm`
- `apps/infones/build.sh`
- `tools/nigetroms.py`
- `tools/niref.py`
- `Makefile`

**Done when**

`make nicputest`, RUN SEPARATELY AND TAKING MINUTES, runs nestest.nes from
$C000 and the trace matches nestest.log for all 8,991 lines — PC, opcode
bytes, A/X/Y/P/SP and CYC — and blargg's instr_test-v5 rom_singles 01-16 all
read 0 at $6000 with the $DE $B0 $G1 signature present; each row has a
negative control that fails when the lowering is broken. (cpu_timing_test6 and
the branch_timing_tests family are NOT here: they predate the $6000 text
protocol and report only on a rendered PPU screen, so in a wave with no PPU
they have no result channel at all. They are wave 4 manual evidence.) The stub
bus's contract is written down beside nicputest.asm and each clause has a
negative control. `make infones` prints the os88pkg five-figure line and it is
under 42,000 resident. A QMP screendump of `make test
TESTAPPS=build/infones.img`, cropped and zoomed, shows the panel with `No ROM`
before an Open and with Concentration Room's seven fields (`Mapper 0`, `PRG
ROM 16KB`, `CHR ROM 8KB`, `Mirroring V`, `SRAM No`, `4 Screen No`, `Trainer
No`) after it — and a second screendump shows the mapper refusal naming the
number for a mapper-66 file. A third shows all three pull-downs open, on
`VIDEO=cga`, with every item inside its box and the dithered Mute item legible
as a checkerboard. niuitest prints the panel's cost table and asserts field
for field that the glass shows what the shadow says.

---

### Wave 2 — The PPU composer, the fullscreen bracket on VGA, mapper 0 — a game visible and playable

**What it adds**

- The tile cache and its decoder in assembly, in its own 32KB claim, with the
  bitmask and previous-bank invalidation.

- The scanline composer: background over the cache with the loopy v/t scroll,
  the attribute walk, the left-column masks, the fine-X partial blocks and the
  nametable flip at the wrap; sprites back-to-front into the same 272-byte
  line with the transparency test, both flips, front/behind priority and the
  sprite-0 strike as tag bits in the pixel byte. Plus the serial frame dump
  nimemtest reads.

- The PPU register file, the $2005/$2006 latch pair, the buffered $2007 read,
  the palette mirror rule, $4014 OAM DMA and sprite evaluation with the
  8-per-line cap.

- The frame loop: the rotating 113/114/114 triple, 262 lines, the sprite-0
  budget split, NMI at 241, the mapper HSync hook, and THE WALL-CLOCK
  ACCUMULATOR — FSXW_FRAME with FSXF_FASTTICK and 110/100, never FSXW_VSYNC as
  the budget.

- THE BRACKET on VGA: nifsx.inc enters OSAPI_FSX_RUN from the resident
  os88_oncmd, sets FSXM_VGA13, programs the DAC with the 64 colours mirrored
  four times, presents each kept line with one rep movsw at 32 columns of
  border, and leaves on F or Esc (SPEC.md §11.2.1).

- Input: the D-pad and buttons through os88_key_down SWEPT EVERY ~16 SCANLINES
  into a sticky latch the emulated $4016 strobe clears (not once a frame — a
  frame is >= 340 ms of wall time here), the system keys through the int 16h
  shim with the queue drained, and R = reset.

- Mapper 0, both shapes — CHR-ROM and CHR-RAM.

- hosttest/nisystest.asm + .sh and `make nisystest` — the NITEST=1 headless
  debug build that runs a whole ROM through the $6000 protocol and prints
  $6004. Built here, not in wave 4, because wave 4's PPU ROMs are what it
  exists for and a harness written the wave it is needed is a harness nobody
  has run.

**Files it touches**

- `apps/infones/nippu.c`
- `apps/infones/nirun.c`
- `apps/infones/nimap.c`
- `apps/infones/niband.inc`
- `apps/infones/nifsx.inc`
- `apps/infones/infones.c`
- `apps/infones/infones.asm`
- `apps/infones/hosttest/niuitest.c`
- `apps/infones/hosttest/nimemtest.asm`
- `apps/infones/hosttest/nisystest.asm`
- `tools/niref.py`
- `Makefile`

**Done when**

A QMP screendump taken INSIDE the bracket (mode 13h is a real BIOS mode, so
QEMU's screendump follows the CRTC — SPEC.md §53.9) shows Concentration Room's
title screen, and a scripted run of arrow/Z/X/Enter keys over QMP moves its
cursor and starts a game, with a second screendump proving it.
robotfindskitten renders — which is the CHR-RAM path, so a blank screen there
is the specific failure this proves absent. `tools/niref.py --selftest` fails
as designed on an injected one-bit defect; `--check` then matches BIT FOR BIT
on TWO dumps and the done_when names both: niuitest's C MODEL frame (which is
what c64ref.py checks, and it is stated here that this half does not touch the
assembly at all) and nimemtest's SHIPPING-ASSEMBLY frame written out over the
serial port. niuitest replays the agnes-generated per-frame recordings for
both ROMs and every frame hash matches. Leaving with F and with Esc each
returns to a repainted panel. RESTORE EQUALITY, SCOPED: a desktop screendump
before and after the bracket is byte-identical OUTSIDE the panel's field rect
(shot.py --crop), and the done_when names which fields are expected to differ
— the state line goes Running -> Ready and the rate fields go from `--` to
numbers, so an unscoped whole-screen comparison must fail the moment the panel
does its job, and weakening it later would throw away §53.9's one strong
check. nimemtest passes the composer, the decoder and the 13h present against
hand-computed bytes under SS != DS.

---

### Wave 3 — The other three adapters, Mode X, the bench, the tier table, and the size re-check

**What it adds**

- The CGA/EGA present: FSXM_CGA320, a 256-ENTRY NES-index-to-2-bit-luminance
  table (built at load by filling 64 real entries four times, because there is
  no DAC to mirror and a tagged pixel byte indexes past a 64-entry table), the
  4-pixels-per-byte pack, and the two interleaved banks.

- The Hercules present: FSXM_HERC, a 256-entry luminance threshold with a 2x2
  ordered dither, 256x240 centred 1:1 in 720x348 (Hercules is the one adapter
  that needs NO row drop — 348 >= 240), four interleaved banks.

- Mode X: FSXM_MODEX, all 240 rows at 1:1, the four plane selects and the
  stride-4 gather per line, and OSAPI_FSX_PAGE's flip (SPEC.md §53.10) —
  offered as a menu choice above the CPU_8086 tier and never as the default.

- The 240 -> 200 row reduction for 13h and CGA: crop 8 top and 8 bottom, then
  a 24-of-224 drop table — THIS PORT'S ARITHMETIC, not the GBA table's
  224->160. The Clip item GREYS in those modes with the fact, because the clip
  is forced there and a live-but-inert toggle is the shape LESSONS.md §1
  names.

- tests/niband: the composer priced per pixel and per line, the decoder per
  1KB bank, each present per line, the core per 6502 cycle and per core entry.

- Adaptive frame skip written from those numbers, with Auto / Page Up / Page
  Down, and BOTH SPEED READOUTS on the panel — the emulated percentage and the
  RENDERED seconds-per-frame, composed from integers.

- THE SIZE RE-CHECK, which is this wave's own job rather than wave 4's: the
  os88pkg line against 55,000, and if it is over, the next file moves out by
  FREQUENCY before wave 4's mappers are written.

**Files it touches**

- `apps/infones/nifsx.inc`
- `apps/infones/niband.inc`
- `apps/infones/nippu.c`
- `apps/infones/nirun.c`
- `apps/infones/nipanel.c`
- `apps/infones/nimenu.c`
- `tests/niband/`
- `tests/suite.py`
- `PERFORMANCE.md`
- `Makefile`

**Done when**

`make nibandbench` prints the per-pixel, per-line and per-present numbers,
they become a new Set in PERFORMANCE.md, and SPEC.md's tier table is written
from them rather than from this plan's estimates. A screendump of the same ROM
inside the bracket on each of three adapters — VGA (13h and Mode X),
`VIDEO=cga` with `tools/mouse.py --screen 640x200`, and `VIDEO=herc` through
`tools/hercshot.py` — shows the picture, and the CGA and Hercules crops are
looked at at zoom before the wave is called done (SPEC.md §39.4), specifically
for a wrong colour or dither cell from a tagged pixel byte. THE SPEED ROW IS A
WORK ASSERTION, NOT A TIME ONE: the panel prints both figures, both are
non-zero, and the CALL and CYCLE counts the panel derives them from match
tests/niband's counts exactly — because the bench runs under icount, the
panel's os88_ticks reflect the HOST under QEMU, and CLAUDE.md's opening rule
is that QEMU is exact about work and useless about time. The wall-clock
agreement is a manual 86Box/5150 reading recorded in PERFORMANCE.md as part of
the same Set, with no gate on it. The Clip item is greyed on a screendump in
mode 13h and live in Mode X. tests/niband is registered in tests/suite.py.

---

### Wave 4 — Mappers 1-4, the MMC3 IRQ, the whole-emulator ROM gate, and the disks

**What it adds**

- MMC1: the 5-bit serial latch with the bit-7 reset, the four registers, both
  PRG modes, both CHR modes, all four mirroring arrangements.

- UxROM and CNROM.

- MMC3: the $8000/$8001 command pair, both PRG modes, the CHR A12 inversion,
  the mirroring register, and the SCANLINE IRQ counted in the HSync hook the
  frame loop already calls — never agnes's faked PA12 edge.

- The large-ROM path: cluster-window os88_file_read_at with the 16-byte shift,
  the 256KB PRG / 128KB CHR cap, and the refusal quoting what it asked and
  what the heap answered — with a gate for the shift, which is the one path
  the shipped ROMs never take.

- THE MMC3 CACHE MEASUREMENT AND ITS FALLBACK (closed here by arithmetic
  rather than by a rewrite): MMC3 switches CHR banks per scanline, so the
  eager decode can re-decode the whole cache twice a frame — about 65,000
  stores, ~136 ms on a 4.77 MHz 8088. The bench measures it; if it exceeds the
  frame budget, the fix is a per-bank LAZY decode (decode a 1KB bank the first
  time a scanline actually reads it in a frame) and NOT a second renderer.

- The About panel at NINE rows, counted at 40 cells before it is written, and
  every greyed item and the panel's fact line looked at on `VIDEO=cga`.

- The four disk geometries with the PER-GEOMETRY ROM set, a CATALOG.TXT on
  each saying which games are on THIS disk and why the others are not (the
  `make cpmsw` / GAMES.TXT precedent), README.TXT (licences and the GPL-3
  source offers) and LICENSE.TXT (the Apache text, which travels with the
  binary the way apps/c64's COPYING does), and the three 86Box machines.

**Files it touches**

- `apps/infones/nimap.c`
- `apps/infones/nirom.c`
- `apps/infones/nicmd.c`
- `apps/infones/niabout.c`
- `apps/infones/nifsx.inc`
- `apps/infones/hosttest/nisystest.asm`
- `tools/nigetroms.py`
- `Makefile`
- `vm/xt-infones/86box.cfg`
- `vm/286-infones/86box.cfg`
- `vm/386-infones/86box.cfg`

**Done when**

`make nisystest` — the whole-emulator harness, NOT `make nicputest`, which is
nasm-only and contains no PPU — passes blargg's ppu_vbl_nmi rom_singles 01-10,
cpu_dummy_reads (mapper 3, so it gates CHR bank switching too), palette_ram
and instr_timing through the $6000 protocol. cpu_timing_test6 and the
branch_timing_tests are screendumped and read by eye, and no gate rests on
them. Screendumps show Mega Mountain (mapper 0 CHR-ROM) and a mapper-1 and a
mapper-4 test ROM rendering, and a status-bar split holds still across ten
frames — the sprite-0 and MMC3-IRQ assertion, taken as two screendumps a
second apart that are identical in the split rows. The 16-byte shift path has
its own row against a >64KB ROM. `make infonesdisk` builds all four
geometries, each `os88disk.py --verify`'d in the recipe, and the 360KB
arithmetic is printed: package + overlay + four Yerrick ROMs (114,752 B) +
README/CATALOG/LICENSE is ~150 of 354 clusters. `make 386-infones` boots 86Box
to the panel (manual evidence, and no gate rests on it). The os88pkg line is
under the 55,000 trigger or the next move by FREQUENCY has already happened.

---

### Wave 5 — The documents, allapps, and The Wire record

**What it adds**

- SPEC.md's new section: the authority table (surface -> file, with every MENU
  string marked as a TRANSLATION and the flattening recorded as an
  adaptation), the four InfoNES year-ranges and which file the About quotes,
  the memory model and its claims, the core's register plan, the composer, the
  mode table per adapter, THE WALL CLOCK and its accumulator, the tier table
  off wave 3's bench with BOTH rates, the single meaning this package gives
  MENU_DIS, what is greyed with its fact and where that fact is read, what is
  dropped rather than greyed with its reason, the names, the disks and the
  machines — written and reconciled against the LAST build's measured numbers,
  not against this plan's estimates (LESSONS.md §11: 37,078 -> 37,084 ->
  37,062 was re-edited three times in one session).

- docs/INFONES-PORT-PLAN.md's 'What shipped' paragraph per wave, and the
  correction to LESSONS.md §9's stale CC_ASSOC line.

- README.md's command list and machine table, CLAUDE.md's machine list and
  Makefile knob table.

- `make allapps`: an INFONES\ folder of its own with the package, the overlay,
  the ROMs, README.TXT, CATALOG.TXT and LICENSE.TXT — five-plus files that
  must resolve in ONE directory (SPEC.md §19.9/§19.10, §73.14).

- The Wire record in ../os8088-web: data/wire.json plus tools/wire.py, a .WPK
  archive on the RUNCPM precedent carrying INFONES.O88, INFONES.OVL, the ROMs
  and the licence files, with the tier and need-KB taken from wave 3's
  measurements and BOTH rates stated honestly. SEPARATE REPO, SEPARATE PR.

**Files it touches**

- `SPEC.md`
- `docs/INFONES-PORT-PLAN.md`
- `README.md`
- `CLAUDE.md`
- `.claude/skills/port-to-os8088/LESSONS.md`
- `Makefile`
- `../os8088-web/data/wire.json`
- `../os8088-web/tools/wire.py`

**Done when**

`make` passes with the fast tier green, `tools/checkdocs.py` accepts every §
citation, `tools/os88index.py` is regenerated, and `make test-full` passes.
`make allapps` builds build/apps-all.img with INFONES\ on it and `os88disk.py
--verify` walks it. Every number in SPEC.md, in the source headers and in the
README is re-measured against the final build in one closing pass, and every
user-visible string in the SPEC's authority table is re-read out of the
reference file it cites rather than out of the plan. The Wire PR is open on
../os8088-web with the record rendering in a local preview and the tier,
need-KB and both rate figures matching the measured ones.

---

## Verification

- THE CPU GATE COMES FIRST AND NEEDS NO PIXELS, AND IT IS NOT IN build.sh.
  `make nicputest`, run on its own and taking minutes, in `make c64cputest`'s
  shape (C64-SPEC §4.6) — apps/c64/build.sh's header says in capitals why the
  equivalent is not a build step, and it needs a fetched fixture besides, so a
  fresh clone's `make infones` must not stall or fail on it. nicpu.inc
  assembled standalone against a stub bus WHOSE CONTRACT IS WRITTEN DOWN AND
  NEGATIVE-CONTROLLED ($2002 returns bit 7 set and clears it on read —
  blargg's shell polls it during init; $2000/$2001/$2005/$2006/$2007 accept
  and discard; $4016/$4017 open bus; $0000-$07FF and $6000-$7FFF RAM), booted
  in raw QEMU, running nestest.nes from $C000 in automation mode and diffing
  PC, the opcode bytes, A/X/Y/P/SP and CYC against nestest.log's 8,991 lines —
  the first differing line names the exact instruction that is wrong, which no
  other test ROM does. Then blargg's instr_test-v5 rom_singles 01-16 through
  the $6000 protocol (run until $6000 leaves $80, check the $DE $B0 $G1
  signature at $6001-3, read $6000, print the NUL-terminated ASCII at $6004 on
  failure). THE SINGLES ARE THE POINT: every rom_singles member is MAPPER 0 at
  40,976 bytes, so the whole CPU suite passes with NROM only and mapper 1 is
  not on the critical path (nes-roms.md section 1.2).

- THE WHOLE-EMULATOR ROM GATE IS A SEPARATE HARNESS, AND THE DRAFT HAD NONE.
  `make nicputest` is nasm-only: nippu.c, nirun.c and nimap.c are compiled C
  and cannot be in that binary, so ppu_vbl_nmi — a test of exactly the code it
  excludes — had nowhere to run. `make nisystest` is a NITEST=1 headless debug
  build of the PACKAGE that loads a named ROM on the wake, runs it with no
  bracket and no present, polls $6000 and prints the $6004 string with
  runcpm's rc_say shape (toast AND console) read back over QMP. It carries
  ppu_vbl_nmi rom_singles 01-10, cpu_dummy_reads, palette_ram and
  instr_timing. cpu_timing_test6 and the branch_timing_tests family have NO
  $6000 status byte — they predate the protocol and report on a rendered
  screen — so they are screendumped and read by eye, and no gate rests on
  them.

- THE HOST HARNESS, planned in wave 1 and grown with the program:
  apps/infones/hosttest/niuitest.c compiles the same C with clang against a
  SECOND copy of os88.h placed ahead of apps/cc on the include path (so drift
  is a compile failure, which is the failure you want), models the glass at
  PIXEL level for the panel, drives the program like a user, asserts field for
  field that the glass shows what the shadow says, and prints the cost table
  in calls, cells and milliseconds. Two traps taken from LESSONS.md §7: a stub
  that always REFUSES measures the fallback path, so os88_gfx_blit1 and
  os88_font_run must model what the machine does; and a new assembly shim is a
  new host stub IN THE SAME EDIT.

- THE INDEPENDENT COMPOSITOR CHECKS BOTH HALVES, AND THE done_when SAYS WHICH
  IS WHICH. tools/niref.py (tools/c64ref.py's role) renders the same PPU state
  to a 256x240 palette-index image from nesdev documentation — NOT from
  niband.inc. Run against niuitest's dump it checks the C MODEL, which is what
  c64uitest.c:1800's dump_for_ref() does and which touches no assembly at all;
  run against build/niref-frame-asm.bin, which nimemtest.asm writes out over
  the serial port from the SHIPPING TEXT, it checks the routine that ships.
  Both rows are in build.sh, and `--selftest` injects a one-bit defect and
  requires the compare to fail, because a check that cannot fail is not a
  check.

- THE ASSEMBLY GETS ITS OWN GATE BECAUSE THE HARNESS MODELS IT. C64-SPEC
  §9.8's all-black 2x screendump is the cautionary case: the C harness
  transcribed the doubler correctly and that is exactly what made the real
  routine's two independent bugs invisible.
  apps/infones/hosttest/nimemtest.asm runs the SHIPPING TEXT on a real x86
  under SS != DS with an ES sentinel and four discipline negative controls
  (ES, DF, BP, DS): the tile-cache decoder against hand-computed tiles, the
  composer's 272-byte overdraw bounds, each of the four present routines
  against hand-computed destination bytes, and the 16-byte iNES shift.
  Hand-computed bytes AND a structural identity over the whole output, because
  either alone passes something wrong — the identity alone passes an all-zero
  table.

- THE BEHAVIOUR ORACLE, adopted from agnes and re-recorded: agnes's recording
  format (a djb2 hash of each frame's pixels paired with that frame's
  controller byte, tests/recorder.c + tests/player.c + --mode verify/update)
  driven from a SCRIPTED input list rather than a human at an SDL window, with
  goldens agnes generates on the host for the four shipped Yerrick ROMs. agnes
  is host-side only; nothing of it crosses onto the machine.

- THE BENCH WRITES THE TIER TABLE, AND THE PANEL IS CHECKED AGAINST IT IN
  WORK, NOT IN TIME: tests/niband (tests/c64band's shape, `make nibandbench`)
  prices the composer per pixel and per line, the decoder per 1KB bank, each
  present per line and the core per 6502 cycle and per core entry, under
  icount. The panel's own figures are asserted by comparing the CALL and CYCLE
  counts it derives them from against the bench's — because the bench is
  icount and the panel's os88_ticks under QEMU reflect the host, so a
  wall-clock agreement row would be asserting the host's speed. The wall-clock
  reading is manual, on 86Box or the 5150, recorded in PERFORMANCE.md with no
  gate on it. It arms the clip on its rerun callbacks, saves ES around every
  blit (a callback returns ES = KERNEL_SEG), and preflights the slot so a
  kernel that refuses prints REFUSED rather than timing a call that draws
  nothing. Registered in tests/suite.py — the fast tier fails an unregistered
  test directory.

- QMP SCREENDUMPS, CROPPED AND ZOOMED, PER CLAIM. Fullscreen frames ARE
  visible under QEMU because 13h, Mode X and the CGA modes are real BIOS modes
  and screendump follows the CRTC (SPEC.md §53.9). `python3 tools/mouse.py
  build/qmp.sock ...` with `--screen` MATCHING the adapter, `tools/qmp.py
  sendkey`, `tools/shot.py --crop --zoom 8`. `VIDEO=cga` with `--screen
  640x200`; `VIDEO=herc HERCSEG=0x7000` with `tools/hercshot.py build/qmp.sock
  0x70000` (screendump is a black image there). A stale QEMU answering the
  socket with the OLD image is the standing trap: quit and rm the socket
  before every boot, and knob builds go in their own BUILD= directory.

- RESTORE EQUALITY, SCOPED SO IT CAN BOTH PASS AND MEAN SOMETHING: a desktop
  screendump before entering the bracket and after leaving it — through F and
  again through Esc, and after cycling every mode the adapter offers — is
  byte-identical OUTSIDE the panel's field rect, and the done_when names the
  fields expected to differ (the state line and the two rate fields). An
  unscoped whole-screen comparison must fail the moment the panel does its
  job, and the likely reaction — weakening it — would throw away SPEC.md
  §53.9's one strong check.

- THE THREE 86Box MACHINES, each a copy of a machine that has booted with only
  the B: image and the uuid changed: vm/xt-infones (from vm/xt-c64, IBM XT
  8088 at 4.77 MHz, B: = build/infones360.img), vm/286-infones (from
  vm/286-c64, B: = build/infones720.img), vm/386-infones (from vm/386-c64, B:
  = build/infones.img). THESE ARE MANUAL EVIDENCE and no gate rests on them;
  the XT machine ships in wave 4 and not before, because an XT target before
  anyone has MEASURED the port there is a claim rather than a machine — and
  what wave 3 measures decides whether it ships as a demonstration or not at
  all (question 4). `git checkout` the cfg before committing and never commit
  nvr/.

- THE THREE EMULATOR-INVISIBLE DEFECTS, addressed by construction rather than
  by looking: a visible redraw (the panel is shadowed and delta-drawn, and the
  cost table says how many calls and cells a field update is), a double-draw
  flash (one decision per field, one os88_font_run per changed field, never
  erase-then-letter), and input overrun (the int 16h queue is DRAINED not
  sampled, and the pad is swept every ~16 scanlines into a sticky latch the
  $4016 strobe clears — the draft's once-per-frame sweep is a ~3 Hz poll on
  the target and would lose most human presses outright while stretching the
  ones it caught over twenty emulated frames). None of the three shows in a
  screendump.

- ONE PREFIX PER LAYER, checked before wave 1 (LESSONS.md §1's name check, and
  apps/c64 uses one prefix throughout): user-facing names are INFONES — the
  package, `build/infones*.img`, `vm/{xt,286,386}-infones`, `INFONES/` on
  apps-all.img; every engineering artifact is `ni` —
  `apps/infones/ni*.c/.inc`, `make nicputest / nisystest / nibandbench /
  infonesdisk`, `tests/niband/`, `tools/niref.py`, `tools/nigetroms.py`. Two
  prefixes for two audiences, stated as a rule, is what stops a grep missing a
  file. Check every one against apps/, vm/, the Makefile and build/ before the
  first commit.

---

## Risks

- THE SPEED IS THE RISK, IT IS KNOWN BEFORE THE FIRST LINE, AND IT IS TWO
  NUMBERS RATHER THAN ONE. No NES emulator has ever run on an 8088, a 286 or a
  bare 68000; the field's floor is 486DX2/66 for C and a Pentium for
  hand-written real-mode assembly (nes-research.md section 6). The arithmetic:
  the composer is ~34 cycles a background pixel through the cache plus the
  sprite pass plus ~3,300 cycles a line to present, about 14,600 cycles a line
  and ~690 ms a RENDERED frame on a 4.77 MHz 8088; the core is ~13,500 6502
  instructions a frame at ~120 8086 cycles each, about 340 ms, and the core is
  NOT skippable. So an emulated frame costs 340 + 690/k ms at skip k, which is
  0.97 emulated fps at k=1, 1.95 at k=4 and 2.94 in the limit — and the
  picture then repaints 1.95/4 = 0.49 times a second, one frame every ~2.2
  seconds. THE DRAFT QUOTED THE 2 fps AND NOT THE 0.5: there is no k at which
  the target both advances plausibly and paints more than once a second. The
  panel prints both, the SPEC's tier table carries both, and question 4 is
  whether the XT ships as a labelled demonstration or refuses at the CPU_8086
  tier. It also weakens the bracket's justification on that machine — at 0.46
  present/s the tear-free mode switch, the Mode X flip and retrace pacing buy
  nothing a windowed blit would not — which is an argument for the deferred
  windowed picture, not against the bracket, since the bracket is what makes a
  386 and a 486 worth having.

- THE MENU IS AN ADAPTATION AND THE AUTHORITY ROW NO LONGER CLAIMS OTHERWISE.
  InfoNES's menu is CP932 Japanese and three POPUPs deep; os8088's is one flat
  list of NUL strings with no submenu, no separator and one item mark. Every
  English string this port ships is a translation the porter performed, the
  Size family is dropped rather than greyed, and the check state moved into
  the label. The residual risk is that a reader of the SPEC takes the English
  for a trace: the mitigation is that each string carries the Japanese and the
  .rc line beside it in nimenu.c and in the SPEC's authority table.

- MENU_DIS ALREADY MEANS TWO THINGS AND THIS PACKAGE PICKS ONE. SPEC.md §12.2
  uses the same dithering for UNAVAILABLE and as a RADIO MARK where the
  dithered item is the SELECTED one, and grey rounds to black on 1bpp so the
  two are indistinguishable there. This package uses it only for UNAVAILABLE
  and carries every check in the label; a later wave adding a checkable item
  with the mark would reintroduce the collision silently, so the rule is
  written in the SPEC rather than only in a comment.

- A GREYED ITEM HAS NO CLICK, SO A FACT NEEDS A SURFACE. The kernel never
  highlights or selects a MENU_DIS item, so refusal-toast code written for one
  is unreachable and the draft budgeted for exactly that. The facts live on
  the panel's permanent fact line and in the item's own 24-glyph short form;
  the risk is a later wave adding a greyed item and forgetting the fact line
  entry, which no build step can catch.

- THE ABOUT LINE IS A JUDGEMENT BETWEEN TWO FILES AND IT IS RECORDED AS ONE.
  The tree carries four version/copyright pairings — Win32 (APP_NAME, which is
  UNDEFINED tree-wide, + 1999-2004), Linux (v0.96J + 1999-2005), SDL (v0.97J
  RC1 + 1998-2006 + mata), Zaurus (v0.93J RC4) — and the draft invented a
  fifth by pairing SDL's version with Win32's copyright. This port quotes the
  Linux About verbatim because it is the only live handler pairing all three,
  which means shipping `v0.96J` while the pinned commit's SDL front end says
  v0.97J RC1. If that reads wrong to the user, the alternative is SDL's
  pairing WITH mata credited, and it is a one-line change.

- THE MMC3 CACHE STORM. The tile cache is the single most valuable structural
  idea in the survey and it is also the one that can lose: MMC3 switches CHR
  banks PER SCANLINE, so a bank-pointer comparison that misses re-decodes
  4,096 output bytes a switch (~41,000 8088 cycles) and a status-bar split can
  cost the whole cache twice a frame, ~136 ms. Wave 4 measures it with the
  bench. The fallback is named in advance and is NOT a second renderer: a
  per-bank lazy decode, deferred to the first scanline that actually reads the
  bank in a frame.

- TWO REFERENCES DISAGREE ABOUT THE SCROLL AND ONE OF THEM IS LGPL. nofrendo's
  ppu_renderbg() is the correct model and its file legend requires any partial
  reproduction to carry it — transcribing the arithmetic is reproduction in
  part, and a file that carried it would be an LGPL-2 file inside an
  Apache-2.0 package. The plan takes the SPECIFICATION from agnes (MIT,
  uint16_t throughout, agnes.c:1100-1140/:1071-1082/:1312-1380) and reads
  nofrendo for intent only, citing it in comments. If a routine ever really is
  transcribed from nofrendo it goes in its own file under nofrendo's legend
  with COPYING.LGPL on the disk — and that is a decision to take deliberately,
  not to drift into.

- THE HARNESS MODELS THE ASSEMBLY, AND A CORRECT TRANSCRIPTION IS WHAT HIDES
  THE REAL ROUTINE'S DEFECT. This cost the C64 port an entirely black 2x
  screendump from a table of 256 zeros that had been in the tree for two waves
  with nothing calling it — and the draft's own PPU-correctness gate compared
  niref.py against the C MODEL only, which is the same trap one level up.
  Mitigation: niref.py checks BOTH dumps, the assembly's arriving over the
  serial port from the shipping text, and it carries --selftest.

- THE nisystest HARNESS IS NEW, UNBUILT WORK. It is a debug build of the
  package driven headless through QMP with a console read-back, and nothing in
  this tree has that exact shape — runcpm's rc_say is the nearest precedent
  and it is a toast, not a test channel. If it proves harder than a wave
  allows, the fallback is the c64cputest shape (a raw-QEMU image linking the
  cc8086 output beside the assembly), which is a bigger build-graph change;
  either way the PPU test ROMs need a home and wave 2 is where that is found
  out, not wave 4.

- A STATIC IS NOT RE-ENTRANT AND HALF THE API IS OUT-PARAMETERS. Rule 1 makes
  every addressable buffer static, and a static is per package INSTANCE. The
  specific bite recorded in LESSONS.md §13 is a shared name scratch aliasing
  across a directory refill; here the shape to watch is a line buffer or a
  scratch shared between the panel's paint and the bracket's frame loop.
  Mitigation: the bracket owns its buffers exclusively, there is no worker at
  all, and the harness asserts the panel's shadow after every step.

- THE iNES HEADER'S 16 BYTES DEFEAT os88_file_read_at's CLUSTER RULE,
  silently, until a large ROM refuses with FERR_NAME. The staging-claim read
  covers every ROM this port ships and every test single; the shift path is
  wave 4's and has its own row, because no shipped ROM exercises it.

- A 512-BYTE-ALIGNED CLAIM BASE IS NOT OPTIONAL AND QEMU NEVER SHOWS THE
  FAILURE. int 13h answers a transfer straddling a 64KB physical boundary with
  error 09h; RunCPM saw it on ~13% of ZEXDOC launches on real hardware. Every
  file read into a claim lands at a 512-aligned offset, never at paragraph
  10h.

- THE PRIORITY TAG BITS ARE FREE ONLY ON VGA. The whole composer carries tags
  in the pixel byte and makes them free by mirroring 64 DAC entries four times
  — which works, and only where there is a DAC. On CGA and Hercules a 64-entry
  reduction table indexed by a tagged byte reads up to 192 bytes past its end:
  a wrong colour or dither cell, not a crash, and not visible in a full-screen
  dump. The 256-entry tables are the fix and wave 3's zoomed crops are where
  it is checked.

- THE 640x200 ADAPTERS BOUND EVERY DIALOG TO TWELVE ROWS AT ~40 CELLS, and
  grey rounds to black there. The draft's About listed eight content items
  including a 64-character copyright line, a section 4(b) notice and a
  trademark sentence — each of which wraps to two rows at 40 cells, reaching
  twelve content rows before the title and the OK button, which is LESSONS.md
  §8's exact failure. Nine rows is the shipped count, it is a compatibility
  constant with a comment saying so, and it is looked at on `VIDEO=cga` before
  wave 4 is called done.

- A DISK WITHOUT THE .OVL IS A PROGRAM WHOSE EVERY MENU REFUSES, POLITELY, and
  this port has MORE in the overlay than the draft did — the ROM loader itself
  is out there. So package, overlay, ROMs and the licence files are ONE FOLDER
  on every disk they share, including apps-all.img; Full screen is answered in
  the resident half; and the loader's refusal is printed on the panel's state
  line, not toasted into a window nobody is looking at.

- NO ovl_* MAY BE CALLED FROM INSIDE THE BRACKET, and now that ovl_rom_load
  exists this has teeth. It would be legal by the SP gate (the bracket runs on
  the UI task) and it would read a floppy while the machine is in a foreign
  video mode, with the refusal toasted onto a bar the user cannot see. The
  rule is written in the SPEC and the fsx entry is resident code only.

- SIZE: the revised estimate is ~42,000 resident of 61,440, not the draft's
  35,500, and the trigger is 13,000 away rather than 19,500. Wave 4's four
  mappers and the MMC3 IRQ are where growth lands and the first full build
  always overshoots. Mitigation: CC_HAS_OVL and THREE files already out from
  the first commit, the os88pkg five-figure line at the end of every wave,
  wave 3 as an explicit size re-check, and 55,000 as the trigger at which the
  next wave's first job is another move — by FREQUENCY, never by size.

- THE TEST ROMS HAVE NO LICENCE AT ALL (nes-roms.md section 1.1: no LICENSE in
  christopherpow/nes-test-roms, no permission statement in any readme). They
  are fetched into build/nesroms/ for the harnesses and SHIP NOWHERE — not on
  a floppy, not through The Wire, not in the repo. The risk is a later wave
  putting one on a disk for convenience.

---

## How to run a wave

One `Workflow` run per wave, in order, from the repository root. Every role
runs on Opus (Decision 9), which is passed explicitly rather than inherited:

```
Workflow({
  scriptPath: "<abs repo>/.claude/skills/port-to-os8088/workflows/implement.js",
  args: {
    repo:      "<abs repo>",
    plan:      "<abs repo>/docs/INFONES-PORT-PLAN.md",
    wave:      n,
    name:      "INFONES",
    dir:       "apps/infones",
    sources:   ["<abs path to the InfoNES checkout>", "<agnes>", "<nofrendo>"],
    decisions: "<the Decisions section above, verbatim>",
    rounds:    2,
    models:    { implementer: "opus", reviewer: "opus", fixer: "opus", verifier: "opus" }
  }
})
```

Between waves the tree always builds and boots; if it does not, that is the
thing to fix before anything else. Watch the `os88pkg` five-figure line at the
end of every wave: the moment resident image + bss passes **55,000 of
61,440**, the next wave's first job is another overlay move by FREQUENCY, not
a feature (SPEC.md §73.9, §73.14).

Builders name the paths they `git add`. Never `git add -A`, `-u` or `.`; never
touch a `vm/*` file they did not create; never `git reset --hard`, `git
checkout --` or `git stash` on paths they do not own; `build/` is never
committed (Decision 9).
