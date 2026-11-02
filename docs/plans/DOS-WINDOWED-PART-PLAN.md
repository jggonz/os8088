# DOS-WINDOWED-PART-PLAN - the windowed layer, natively a part

**Status: a RECORDED OPTION, not taken (owner, 2026-11-01). C' is BUILT
(SPEC.md 96.54.9), and its measured table is the baseline any revival of this
design is weighed against: -3,923 bytes an instance, a glyph +4.2%, `AH=09h`
-1.7%.** Option C'
(DOS-PARTS-PLAN §2's macro translation, ~+0.75 KB of disk against this
design's ~+1.25 KB) is what gets built; the owner had hoped this one would
come out about the same size, and since it did not, the speed loss of C'
(+4.3% a teletype glyph, +14.5% an `AH=09h`, measured below) is accepted
for now. **Reconsider this design if testing makes the virtual `int 10h`'s
speed a concern** - the two cycle prototypes of section 9 were reverted and
not kept as patches, so taking it up again starts from this document.

**Originally: a design study. Nothing here is built.** Two throwaway prototypes
were built to measure one question (section 9) and reverted; the tree this
commit leaves is the tree it started from plus documents. Written on branch
`dos-windowed` at 2026-11-01, against `74d6926f`. Every figure is **M**
(measured on this tree) or **E** (estimated, with what it was estimated
from). An **E** is a promise to measure, not a fact.

It answers the owner's direction on docs/plans/DOS-PARTS-PLAN.md section 2:
the windowed layer (`apps/dos/dosw.inc`, `apps/dos/doswm.inc` and the
`%ifdef DVW` arms in `apps/dos/dos.asm`, SPEC.md 96.54) becomes a lazy part,
fetched when a program is run in a window - *"if we do sacrifice the draw
speed at all to do the part, we should do the best version of it we can ...
reconcept it as NATIVELY a part instead of trying to claw it out of place."*
So the starting point is not DOS-PARTS-PLAN's option C' (move the files,
rewrite their references with four macros). It is what the layer does at run
time, which of those things run per character, per tick and per event, and
where the segment boundary costs nothing on each.

## 0. The answer in one screen

| | today (M) | C', re-costed (section 3) | **native (this plan)** |
|---|---:|---:|---:|
| resident DVW bytes in every DOS instance (image + bss) | 4,107 + 653 = **4,760** | ~554 + 15 (E) | **~554 + 15 (E)** |
| RAM saved per instance never run windowed | - | ~4.1 KB (E) | **~4.1 KB (E)**: 4,191 bytes |
| part claim while windowed | - | 5 KB (E): ~4,500 bytes | **5 KB (E)**: 4,877 bytes in a 5,120 claim |
| RAM while windowed, against today | - | ~+0.9 KB (E) | **~+0.9 KB (E)**: the KB granule decides both |
| `DOS.O88` on the disk | - | ~+0.75 KB (E) | **~+1.25 KB (E)**, 2-3 sectors |
| virtual `int 10h` teletype, a glyph (~3,200 cycles, M) | - | **+137 to +140 cycles, +4.3% (M)** | **+0 (M)** |
| virtual `int 10h AH=09h` (~2,325 cycles, M) | - | **+332 to +338, +14.5% (M)** | **+0 to +6 (M)** |
| `INT 21h` output (`dos_hk_tty`), a character | - | +0: never enters the layer | **+0**: never enters the layer |
| the tick stub (`dvw_irq0`, ~19,000 cycles, M) | - | +20-40 (E) | **+~40 on a foreign-stack tick, 0.2% of the stub (E)** |
| an INT 33h read in the window | - | +~90 (E) | **+~90, ~1.4% (E)** |
| first Run in Window of a window | - | ~1 s on a 5150 floppy (E) | **~1.0-1.2 s, once per window (E)** |

**The design in four sentences.** The part OWNS everything the windowed run
is - the world exchange, the IRQ0 stub, the virtual BIOS, the key ring, the
capture, the mouse translation, the door posting - with its state inside its
own claim and its code entered through a fixed jump table at offset 0. The
console stays in the host, because the prompt and the bracket draw it too,
and the part carries **its own copy of the console's per-character teletype
routines, assembled from the same source** against the host's data, so the
one path that runs per character never leaves the part's segment. Every
host-to-part entry is a six-byte resident thunk; every part-to-host call is a
COLD call through one 31-byte, register-transparent, re-entrant bouncer.
Nothing that runs per character or per tick crosses a segment.

**What decides it against C'** is the measured per-call price of a crossing:
C' puts a far call on the virtual video BIOS's every call into the console,
which prototype B measured at **+111 cycles a crossing** - +4.3% on a glyph
and +14.5% on `AH=09h`, which makes three crossings. In RAM the two are a
wash (section 3.4); the native design costs ~0.5 KB more DISK, which is the
console's per-character routines carried twice, and it is the one that
leaves the draw speed exactly where it is.

## 1. What the layer is at run time

Spans from a `[map all]` of an assembly byte-identical to `build/dosp.bin`
(DOS-PARTS-PLAN section 0's instrument). `DVW` A/B'd by commenting dos.asm's
`%define DVW` in a copy (M):

| | bytes |
|---|---:|
| host image with / without `DVW` | 47,520 / 43,413: **4,107** |
| host bss with / without | 7,309 / 6,656: **653** |
| `dosw.inc` + `doswm.inc` code, `dvw_runs` to the end of `dwm_clamp` | **3,899** |
| ...so the `%ifdef DVW` arms in `dos.asm` | **208** |
| host part packed (LZ4, the codec it ships in), with / without `DVW` | 23,082 / 19,670 |
| the layer's code packed alone | 3,541 |

### 1.1 The contexts it runs in

| context | routines | DS on entry |
|---|---|---|
| **the program's interrupts**, world IN, any stack | `dvw_int10`, `dvw_int13`, `dvw_int16`, `dvw_irq0` (IVT 08h), `dvw_kexit` | the program's |
| **the core's near hooks** | `dvw_fin` (`DHK_SNAP`), `dwm_mouse`, `dwm_txt` (`DHK_MOUSE`/`DHK_TXT`), the 22 posting doors (`dos_bevec`, through the core's `dos_be_go`) | host for the hooks; the program's for a door |
| **the worker**, its own slice | `dvw_worker`, `dvw_runprog`, `dvw_nap`, `dvw_park`, `dvw_post` | host |
| **the UI task** | `dvw_start` (from `dos_run`), `dvw_wake` (from `dos_wake`), `dvw_serve`, `dvw_capt`, `dvw_fsmain` (an `OSAPI_FSX_RUN` proc), `dvw_kput` (from `dos_key`), `dvw_onclose` (`OSAPI_WM_ONCLOSE`) | host |

### 1.2 The hot paths, measured

On MartyPC's `os8088_5150_herc_gla` with `tests/dostrap/dwcell.asm` run in
its window (the `doswcell` row's probe and boot), breakpoint traces with
guest cycles (method in section 9):

| path | cycles | on a 4.77 MHz 8088 |
|---|---:|---:|
| `int 10h AH=0Eh`, a glyph: `dvw_int10` to its `.out` | **3,081-3,088 (M)** | ~0.67 ms with the `INT`/`IRET` (~115 more, E) |
| ...of which `con_write` | 1,974-2,554, median 2,274 (M) | |
| `int 10h AH=0Eh`, a backspace | 2,502-2,522 (M) | |
| `int 10h AH=09h` in place | 2,201-2,216 (M) | |
| `dvw_irq0`, entry to the kernel's tick (`dvw_xpost`'s copies, guard, pace) | 9,717 (M, min; median 9,746-10,856) | |
| `dvw_irq0`, after the kernel's tick to `iret` (`dvw_xin`) | 9,243 (M) | |
| ...so the stub, each tick the program holds the CPU | **~19,000 of a tick's 262,088 (7.2%)** | SPEC.md 96.54.3's "~7% in the copies" |

And the paths the layer does NOT own, which no design here touches:

- **`INT 21h` output** - `AH=02h`, `09h`, `40h` to the console - is the
  core's `DHK_TTY` hook, `dos_hk_tty` in the HOST, straight into `con_write`.
  Its only contact with the windowed layer is one store, `mov byte
  [dvw_dirty], 1`. So the common way a DOS program prints costs nothing in
  any design here, C' included - DOS-PARTS-PLAN's "5-6% per character" was
  the virtual `int 10h`'s alone.
- The band's paint (`con_rows_owed`, `doswcell`'s 11,305 cycles a one-cell
  pass) runs on the UI task in the host, from the wake.

## 2. The boundary, path by path

### 2.1 Who owns what

**The rule: the part owns all of the windowed run's state; the host keeps
only what it reads when the part is ABSENT.** That is five bytes of flags and
two words:

| stays in the host's bss | why |
|---|---|
| `dvw_want` | the menu's ask, read by `dos_run` before anything is fetched |
| `dvw_run` | read by `dos_wake`, `dos_key`, `dos_oncmd`, `dos_go`, and the close guard - it is the "is the part in play" bit |
| `dvw_dirty` | written by `dos_hk_tty` (host) on every character a program prints through INT 21h: a console fact, not a window fact |
| `dvw_cap` (word) | `dos_run`'s arena cap and `dos_err_line`'s refusal |
| `dvw_pseg` (word) | the part's segment; 0 = not fetched |
| the part's row (8) | copied into the host's bss by the loader, as HELP's is (`DSLH_HLPROW`'s shape) |

Everything else in `DOS_B_DVW*` (646 of the 653 bytes: both world blocks,
the sums, the key ring, the posted registers, the scroll window, the flags)
moves INTO the part's image as zeros - a run of zeros costs LZ4 nothing, so
the part needs no bss of its own and no change to the row format.

**The segment convention inside the part**: DS is the HOST wherever the part
touches host data, and the part's own cells are always `[cs:...]`. That is
the convention the ISR code already keeps - 64 of `dosw.inc`'s 113 operands
naming a `dvw_` cell already carry `cs:` - and it is what lets the console's
own routines run unchanged inside the part (section 2.2). The other 49, and
`doswm.inc`'s 20, gain the prefix: **69 bytes (M count)**, all on UI-task or
event paths. UI-task entries arrive with DS = host already; an interrupt
entry loads it once with `push ds / mov ds, [cs:dvw_hseg]`, which replaces
today's `push ds / push cs / pop ds` and was measured at **+0 to +6 cycles**
(prototype A, section 9). `dvw_hseg` is written at every `dvw_start` from
DS, so the part holds no host segment between runs and the host's own
relocation (`dos_reloc`) needs no new line: the host region is pinned for a
run's whole length anyway, by the worker's frame (SPEC.md 66.6.2).

### 2.2 The character: zero crossings, by carrying its own copy

The virtual `int 10h` is the only path that runs per character and enters the
layer. Per call it uses the console: `con_write` (teletype), `con_celloff` and
`con_markcells` (`AH=09h`), `con_markcur` (`AH=02h`), `con_mark` (scroll,
capture). The console's BUFFER and CURSOR cannot move into the part: the
prompt (`dosh.inc` through `dos_hk_tty`), the bracket's text-mode renderer
(`con_tx_*`) and the band's renderer all read them, and they are in use in an
instance that is never windowed.

**So the part carries the per-character CODE and borrows the DATA.** The
cone `con_write` reaches is eleven routines:

| `con_write` 106, `con_putc` 78, `con_marks` 66, `con_markcells` 50, `con_markcur` 24, `con_nextrow` 21, `con_celloff` 18, `con_bit` 14, `con_mark` 6, `con_mk1` 5, `con_markc` 3 | **391 bytes (M spans), 378 packed (M)** |
|---|---|

assembled a second time in the part, from the same source, with `DS` the
host's and every `con_*` cell an `equ` out of the host's map (section 5.1).
`con_nextrow`'s one call out, `con_scroll1` at the bottom row (a 4,000-byte
`rep movsw` behind it), and the rare ones - `con_clear` on a mode set,
`con_markall` at a full-screen transition - go through the bouncer of section
2.5. **It is one source and cannot drift**: `apps/os88con.inc` splits the
cone into an include of its own (`apps/os88cont.inc`, E), which the host
includes as today - wave 1's gate is the host assembling byte-identical - and
the part includes under `%define CON_EXTDATA`, which emits no data and names
`con_scroll1` as a bounce.

Measured against C' (section 9): C''s `HCALL con_write` is +137-140 cycles a
glyph, +4.3%; its three crossings on `AH=09h` are +332-338, +14.5%. This
design is **+0** on both: the instructions executed are the same ones.

*The lever this leaves for later*: `con_write` is 2,274 of a glyph's ~3,200
cycles, most of it three `con_markcur` calls each with its own `pushf/cli`
and `con_bit` shifts. Owning a copy makes it possible to specialise it, but
the copy is worth having because it is the SAME source; a specialised copy is
a second teletype with its own bugs, and is not proposed.

### 2.3 The tick: the stub stays whole

`dvw_irq0`, `dvw_tolow`, `dvw_xpost`, `dvw_xin`/`dvw_xout`/`dvw_xflip`/
`dvw_copy`, `dvw_vguard`, `dvw_cpace` and `dvw_park` all move into the part
and all their state with them, so the IVT's 08h points at `part:dvw_irq0`
and the far call to the kernel's tick is `call far [cs:dvw_k08]` exactly as
today. Two host reads remain on the tick:

- `[dos_win]` and `[dvw_dirty]` in `dvw_xpost`: with the DS load of 2.1,
  measured equal to today's `push cs / pop ds`;
- **`[dos_dstk]` in `dvw_tolow`**, the core gate's frame top, which the core
  moves on a child's EXEC (`dos.asm`:4868) and so cannot be mirrored. At that
  instruction SS:SP is half-switched and AX holds the return, so it is read
  through ES saved in a part cell: `mov [cs:dvw_tes], es / mov es, [cs:dvw_hseg]
  / mov sp, [es:dos_dstk] / mov es, [cs:dvw_tes]`, **~+40 cycles and +12
  bytes (E, Intel's 8088 timings)**, only on a tick that lands on a foreign
  stack. Against the stub's measured ~19,000 that is 0.2% of the stub and
  0.015% of the machine: `doswspin`'s 88.8% does not move at its resolution.

`dos_win` itself is copied into a part cell (`dvw_win`) at `dvw_start`, so
`dvw_post`, `dvw_shunt` and `dvw_nap` need no host segment at all.

### 2.4 Host to part: a jump table and six-byte thunks

The part assembles `org 0` and begins with a table of `jmp near` entries at
fixed offsets, then `dvw_hseg` at a fixed offset, both named in a shared
`apps/dos/doswabi.inc` that the host and the part include and the part
asserts. The host reaches entry `n` through a resident thunk:

```
dvw_t_wake:  db 0x9A            ; call far PART:3*n  - the segment word is
             dw DVWE_WAKE * 3   ; patched when the part is fetched and when
             dw 0               ; it moves (2.6); never while a run is in
             ret
```

`call far imm` is 36 cycles on an 8088 against an indirect far call's ~61,
and a patched segment costs no bss. Patching is safe for the reason
`apps/tracker/trkplay.inc` gives for its own immediates: the write is on the
UI task, between runs, thousands of instructions before any thunk executes,
and no thunk can be mid-flight while the part moves because the part only
moves between runs. kern_mega, where CS is a read-only code selector, has no
windowed layer at all (section 5.3). One loop patches every thunk (~12 bytes).

| entry | reached from | host bytes |
|---|---|---:|
| `WAKE` | `dos_wake`'s `jmp dvw_wake` becomes `jmp dvw_t_wake` | 6 |
| `KEY` | `dos_key`'s arm. The Alt+Enter, frozen and ring logic moves INTO the part, so the arm shrinks to a test of `dvw_run` and a call | 6 |
| `START` | `dos_run`, after the fetch | 6 |
| `ONCLOSE` | `OSAPI_WM_ONCLOSE`'s callback, which must be a near proc in the host: `cmp byte [dvw_run], 0 / je .ok / call far` | 13 |
| `PREP` | the worker, section 2.6 | 6 |
| `FIN` | `DHK_SNAP`: `add sp, 2 / call far / ret` - the stack trick of SPEC.md 96.54, unchanged | 9 |
| `MOUSE`, `TXT` | `DHK_MOUSE`, `DHK_TXT`, bound only for a windowed run | 12 |
| `FSMAIN` | `OSAPI_FSX_RUN`'s proc, which the kernel calls in the instance's own segment | 6 |
| `POST` | the doors, below | 5 |

**The 22 posting doors.** The core near-calls `[dos_bevec + ordinal]` from
`dos_be_go` with every register an argument, and its ordinal is spent before
the call (`dos.asm`:5664-5666), so the window needs 22 distinct host
addresses. They are `call dvw_t_post` - **three bytes each, 66 (E)**, against
today's 158 of five-byte stubs and a table - and the part recovers the
ordinal from the stub's near return, `(ret - stubs - 3) / 3`. The door's
answer is then returned with **`retf 4`** after the part has written the
core's return address over its own far return's IP: one instruction, no flag
touched, and it lands back in the core with CS = host and the stack balanced.
`DBE_HERE` and `DBE_VKIND` stay direct, as today. The target is read out of
the BRACKET's own resident table, `dos_betab`, by ordinal - so the window's
`dvw_betab` table disappears rather than moving.

*An option, not recommended*: four core bytes (`dos_be_go` banks the ordinal
before it resolves it) would let all 22 entries be one address, saving 62
resident bytes - but the core is the slot `kern_dos` shares (SPEC.md
96.44.5), byte-identical today, and the core is touched only for a reason of
its own.

`dvw_fsmain` also rebinds `DHK_MOUSE`/`DHK_TXT` to the bracket's own hooks on
the way onto the full screen and back on the way off, instead of `dwm_desk`
asking on every read and tail-jumping back into the host: on the full screen
an INT 33h read then costs nothing extra, and in the window it costs one
thunk (~+90 cycles of a ~6,500-cycle read, E: the 361-367 polls a guest second
of SPEC.md 96.54.4).

### 2.5 Part to host: one bouncer, for the cold calls only

What the part still calls in the host, after sections 2.2 and 2.6 took the
per-character and worker-entry calls out:

| call | when |
|---|---|
| each posted door's target (`dos_k_*`, by `dvw_serve`) | per file operation |
| `dos_m33_evt` (`dwm_kick`) | per wake out of `dvw_nap`, at most 18 a second |
| `dos_con_draw` | per paint wake, at most 18 a second |
| `dos_fsx_ae`, `dos_fsx_owed`, `con_tx_ice`, `con_markall` | the full-screen loop and its transitions |
| `con_scroll1`, `con_clear` | a scroll at the bottom row; a text mode set |
| `dos_snap`, `dos_be_bind`, `dos_swap`, `dos_con_draw`, `dn_shut`, `dos_pkt_shut`, `dos_drv_back`, `dos_cache_free`, `dos_con_ended`, `dos_repaint` | once per run |

~20 sites. All of them go through ONE resident routine that takes its target
from the word after the call, preserves every register and the flags in both
directions, and uses only the stack, so the worker and the UI task can both
be inside it at once:

```
; dos_hb - far-called from a part: `call far [cs:dvw_hb]` / `dw target`
dos_hb:
    push ax                     ; slot B
    push ax                     ; slot A, a copy of AX
    push bp
    mov bp, sp                  ; [bp+2] A, [bp+4] B, [bp+6] IP, [bp+8] CS
    push ds
    push si
    lds si, [bp+6]              ; DS:SI -> the inline target
    mov ax, [si]
    lea si, [si+2]              ; (no flag touched, and no DF read)
    mov [bp+6], si              ; return past the word
    mov word [bp+4], .back
    xchg ax, [bp+2]             ; A = the target, AX = the caller's again
    pop si
    pop ds
    pop bp
    ret                         ; into the target, whose ret comes to...
.back:
    retf                        ; ...here, and back to the part
```

**~31 bytes (E), ~390 cycles a call more than a near call (E)** - which is why
nothing per character uses it. `dvw_serve`'s target varies per door, so its
call site's inline word is patched with the target before the call (UI task
only, so never concurrent). Stack: +12 bytes transient before the target
runs and +4 while it runs - which matters at exactly one place, `dwm_kick`
into `dos_m33_evt` into the program's own INT 33h event handler, and is a
`t_stkclass` question to settle in wave 2.

The same bouncer serves DOS-PARTS-PLAN section 3's network part, which has
the same shape of cold calls; whichever part lands first builds it.

C' instead resolves each of ~30 host routines through a dword in the part and
a four-byte resident thunk (`call x / retf`): ~120 resident bytes against 31,
for ~110 cycles a call instead of ~390. On calls that are per file operation
or per paint, the cheaper resident bytes win.

### 2.6 The worker: split where the program is entered

`dvw_runprog` calls six host routines (`dos_m33_wall`, `dos_build_psp`,
`dos_hook_vectors`, `dos_drv_bank`, `dos_date_init`, `dos_prog_enter`) and its
end is the stack trick that makes `dvw_fin`'s `ret` return to the worker. So
the worker loop STAYS in the host (`OSAPI_TASK_SPAWN`'s entry is a near
offset in the instance's region anyway, and so is
`OS88_WORKER_RESTARTABLE_NOKEEP`'s), and it does the host calls itself around
one part entry:

```
host:  dos_m33_wall, dos_build_psp
part:  PREP - dwm_start, the doors bound, the world primed, the capture
       detected, the world IN and the four vectors written (IF = 0 from
       here to the program, exactly as today)
host:  dos_hook_vectors, the DTA and folder, dos_drv_bank, dos_date_init,
       [dos_sv_ss]/[dos_sv_sp]/[dos_dstk] banked, call dos_prog_enter
       ...and FIN's `ret` comes back here, to set [dvw_done] and wake
```

That moves today's ~50-byte worker loop and the ~55 bytes of `dvw_runprog`
from `call dos_hook_vectors` to `call dos_prog_enter` (M: 0x5574-0x55A5 plus
the two calls before) into the resident host: **~105 bytes**, in exchange for
no crossing and no change to the stack trick.

### 2.7 Load, pin, move and drop

- **Fetched at the first windowed Run** of a window: in `dos_run`'s windowed
  branch, before the arena claim (so the 128 KB arena is placed with the part
  already in the heap, and a refusal has nothing to unwind), through the
  shared `op_self` reader (DOS-PARTS-PLAN 1.1) and a `MEMP_REGROW` that
  shrinks the claim from `op_self`'s read size to the image (~14 bytes, the
  `.keep` of DOS-PARTS-PLAN 1.1). `dos_run` re-enters on a compaction wake,
  so the fetch is idempotent (`dvw_pseg` != 0). The Setup default the owner
  named - windowed for every Run - reaches the same branch, so it needs
  nothing of its own here.
- **A refused fetch** (no RAM, or the disk DOS.O88 came from is out of the
  drive) refuses the RUN with a line in the window rather than falling back to
  the bracket: a user who asked for a window and is given the whole screen has
  not been told why. Today's no-worker fallback to the bracket stays.
- **Pinned while a run is in**, by `OSAPI_MEM_MOVABLE` with BX = 0 at
  `dvw_start` - the IVT's 08h, 10h, 13h and 16h, the worker's interrupted
  frames and the program's own interrupted `CS:IP` (the close's `dvw_kexit`)
  all name the part's segment then, and the host region is pinned by the
  worker's frame for the same span.
- **Movable between runs**, declared at the end of `dvw_wake` with a host
  relocation proc that writes the new segment into `dvw_pseg` and re-patches
  the thunks. Nothing else names the part between runs: the world is out and
  its blocks are discarded, and `dvw_hseg` is rewritten at the next start.
  This keeps the claim off `tests/pins.txt` (docs/plans/NO-PINNING-PLAN.md);
  a window's part is a wall only while its program runs, which is when the
  host region is one too.
- **Dropped when the same window starts a BRACKETED Run**: the bracket's
  arena is `OSAPI_MEM_AVAIL` (SPEC.md 96.3), so a part left standing would
  take 5 KB from the program. ~10 bytes. The next windowed Run fetches again.
- **Freed with the window.** It is one of the instance's eight owner claims
  (`MEM_OWNER_MAX`); a windowed run holds the carve, the arena and the part,
  plus the packet driver's buffers when wired - four of eight (E).

Costed and NOT proposed: dropping the part at every run's end (a second Run
in Window pays ~1 s again), and declaring it purgeable between runs so a
short heap can shed it (a notice the host would need, for ~5 KB a user who
runs windowed is likely to want back).

### 2.8 What stays exactly where it is

The console and both renderers; `dos_hk_tty`; the core and its hooks'
calling convention; `kern_dos`; the part ROW format and `os88pkg.py`'s
layout - the part is an `OP_ASSET | OP_COMP | OP_LAZY | OP_SELF` row exactly
like HELP's, assembled as `build/doswin.bin` and appended with `--part`.

## 3. Bytes

### 3.1 Resident in every DOS instance, after (E unless marked)

| | bytes |
|---|---:|
| the `dos.asm` arms that stay (`dos_run`'s cap and branch, the menu item, the refusal string, `dos_oncmd`, `dos_go`, `dos_wake`), less `dos_key`'s logic moving out, plus the fetch call and the bracket's drop | ~207 (M 208 today) |
| the worker loop and `dvw_runprog`'s host tail | ~105 |
| entry thunks (section 2.4) | ~69 |
| door stubs | ~66 |
| the bouncer | ~31 |
| fetch glue (with HELP's stand-there-and-back factored into one shared `dos_pfetch`), the regrow, the patch loop, pin/unpin, the relocation proc | ~76 |
| **image** | **~554** |
| bss: `dvw_want`, `dvw_run`, `dvw_dirty`, `dvw_cap`, `dvw_pseg`, the row | **15** |

**RAM saved per instance never run windowed: 4,760 (M) - ~569 = 4,191 bytes,
~4.1 KB (E).** On kern_small, whose 128 KB desktop has ~50 KB of heap, that
is ~8% of the arena back for every DOS window that is never run windowed.

### 3.2 The part (E unless marked)

| | bytes |
|---|---:|
| the layer's code today | 3,899 (M) |
| less the worker and its host tail; less `dvw_betab` | -263 |
| plus the door ordinal and the bind loop | +40 |
| plus the console cone | +391 (M) |
| plus `cs:` on the 69 plain operands | +69 (M count) |
| plus ~20 bounce sites at +4 each, the jump table, `dvw_hseg`, `tolow`'s ES, the host-segment loads, `retf 4`'s surgery | ~+235 |
| **image** | **~4,231** |
| state, as zeros in the image | 646 (M) |
| **claim** | **4,877 of a 5,120-byte claim** |

### 3.3 Disk

The host part packs 3,412 bytes smaller without the layer (M: 23,082 ->
19,670); the layer and the cone pack to 3,903 alone (M), ~4,100 with the
prefixes and sites (E); the resident additions pack to ~300 (E); plus the
8-byte row and a part's sector padding (~256 on average, HELP's part sits in
a sector of its own). **`DOS.O88` +~1.25 KB, 2-3 sectors (E).** C' by the same
accounting is ~+0.75 KB: the difference is the cone's 378 packed bytes and a
larger split stream.

### 3.4 C', re-costed on this tree

DOS-PARTS-PLAN section 2 costed C' against 4,620 + 653 and counted the
`dos.asm` arms as moving. On this tree the layer is 4,107 + 653 (M) of which
208 are arms that stay (M); C''s ~+285 resident (thunks, entry stubs,
`dvw_betab`) needs the same fetch glue and pin as this design (~+76), so it
is ~569 resident too and saves the same ~4.1 KB; its part is ~4,500 bytes
against this one's 4,877, and both take a 5 KB claim. **The RAM is a wash,
within the estimates' error.** What separates them is section 4.

## 4. Cycles on the hot paths

| path | today | C' | native |
|---|---:|---:|---:|
| `int 10h AH=0Eh`, a glyph | 3,081-3,088 + ~115 (M) | **+137-140 (M)** | **+0 (M)** |
| `int 10h AH=0Eh`, a backspace | 2,502-2,522 (M) | +115-135 (M) | +0 (M) |
| `int 10h AH=09h` in place | 2,201-2,216 (M) | **+332-338 (M)** | +0 to +6 (M) |
| `int 10h AH=02h` | (two `con_markcur`) | +~230 (E: two crossings at the measured ~111) | +0 (E) |
| `INT 21h` output | unchanged | +0 | +0 |
| the tick stub, program on a foreign stack | ~19,000 (M) | +20-40 (E) | +~40 (E) |
| an INT 33h read in the window | ~6,500 (E, from SPEC.md 96.54.4) | +~90 (E) | +~90 (E) |
| a posted door | a task round trip (2 x 693 us) and the file call | +~120 (E) | +~540 (E: the thunk, the ordinal's divide, the bouncer) - ~0.1 ms against a door whose floor is ~1.4 ms and whose disk call is ~400 ms |
| a paint wake | `doswcell`'s 11,305-12,429 (M, SPEC.md 96.33.12) | +~110 (E) | +~390 (E), ~3% of a one-cell paint, at most 18 a second |
| the first windowed Run of a window | - | ~1 s (E) | ~1.0-1.2 s (E): ~4.1 KB packed, ~8 sectors, by HELP's measured 985 ms for 605 bytes (two calls, two seeks, DOS-PARTS-PLAN 1.1) plus ~41 ms of decode |

## 5. Builds

### 5.1 The two-pass build

The host is assembled first, with `[map all]`; a small host tool
(`tools/os88pexp.py`, E) turns the map into `build/doswexp.inc`: every host
DATA label (bss and core cells - the `con_*`, `dos_*`, `dvw_want` family) as
an `equ` under its own name, and every host CODE label the part bounces to as
`H_name`, so the part's copy of `con_write` and the host's never collide. The
part (`apps/dos/doswin.asm`, which `%include`s the moved `dosw.inc` and
`doswm.inc` and the console cone) assembles against it. The dependency runs
one way only: the host needs nothing of the part but `doswabi.inc`'s fixed
offsets, which the part asserts.

**The checker is what makes a hand rewrite safe** (`tools/os88dswchk.py`, E,
in the fast tier): in the part's sources, any memory operand naming a `dvw_`
or `dwm_` cell without `cs:`, any `push cs` followed by `pop ds`/`pop es`
(which in the part never means the host), and any near `call`/`jmp` to an
`H_` name fail the build. Ownership is the prefix, which is why the rule can
be mechanical.

### 5.2 The one build with no loader

`build/dos.bin`, the plain package that the icon directory builds, has no
loader and cannot fetch. Under `%ifndef DOSKPART` the part's source is
`%include`d inline, as HELP's text is: the thunks become near calls, the
bouncer a near call, `[cs:dvw_hseg]` reads CS. That inline arm is also a
sanity build: it must behave identically, which a row can check by booting
it.

### 5.3 kern_small, kern_mega, kern_dos

- **kern_small** has the windowed layer today (`DVW` is defined wherever
  `DOS_MEGA` is not) and gets the part the same way; `op_self` already handles
  its whole-cluster `READ_AT`. Its compactor is absent (`OS88_COMPACT` is
  kern_big's), so the pin and movable declarations are inert there and cost
  only their bytes. It is where the 4.1 KB is worth most.
- **kern_mega** has NO windowed layer (dos.asm's `%ifndef DOS_MEGA`,
  KERN-MEGA-DOS-PLAN 2.1), so its `DOS.O88` carries no part and no row. Were
  it ever given one, code in a claim is a DATA selector there and needs an
  executable alias (`OSAPI_SEG`, docs/plans/KERN-MEGA-PLAN.md), and the
  patched thunks would have to become a dword table, CS being read-only.
- **kern_dos** is the bracket and never windowed. The core is byte-identical
  under this design.

## 6. Risks

1. **The segment convention, by hand** (medium). 69 operands gain `cs:`, ~8
   `push cs / pop ds` become host loads, and a wrong one is a silent wrong
   segment. The checker of 5.1 turns the naming rule into a build failure;
   what it cannot see is a computed address (`dvw_csview`, the scroll's
   `.addr`), and those few are decided by hand and named in the wave.
2. **A vector into a claim** (medium). The IVT names the part while a program
   runs; a pin dropped early is a jump into memory that moved. The pin and the
   movable declaration are two calls at two places, and wave 3 adds the gate
   that compacts between runs.
3. **The door path's arithmetic** (low): the ordinal from a return address
   and `retf 4`'s surgery. Every door is exercised by `doswlive`'s file
   legs; a wrong ordinal is a wrong file call, loudly.
4. **Self-patched thunks** (low): written only between runs; Tracker's
   immediates are the precedent.
5. **Stack**: +4 bytes under the INT 33h event callback (2.5), +4 on a door
   (the far frame); the ISR paths add nothing. `t_stkapps`/`t_stkclass` and
   `doswmouse` decide.
6. **A disk the user has removed** at the first windowed Run: refused with a
   line (2.7). New behaviour, needs a leg.
7. **Concurrent work** (section 8).

## 7. Waves

Each wave verifies on the host and NAMES its rows; the rows run once, in the
final pass (CLAUDE.md, Testing).

| wave | what | gate |
|---|---|---|
| **0** | Shared infrastructure, no behaviour change: `dos_pfetch` (HELP's stand-there-and-back factored out), the bouncer `dos_hb`, the part row and its `DSLH` copy in the loader, an empty part. | host builds; `dosopen` (HELP), `dosaltenter`, `win16nedos` (the `DSLH` block), `kerndos`, `kdos`, `dosbss` |
| **1** | Split `os88con.inc`'s teletype cone into `os88cont.inc`. **`dosp.bin`, `telnet.bin` and every other includer byte-identical** - the proof that the copy and the host are one source. | `cmp` of every includer; fast tier |
| **2** | The part: `doswin.asm`, the export tool and the checker, the jump table, thunks, door stubs, the worker split, the DHK rebinding, `cs:` and host loads, the fetch in `dos_run`, pin at start, movable at end. The layer leaves the host. | `doswlive`, `doswfs`, `doswtwo`, `doswspin` (its 85% floor; expect 88.x unchanged), `doswcap`, `doswcell` (its 20,000-cycle budget; expect unchanged), `doswmouse`, `dosaltenter`, `win16nedos`; `small128` (kern_small's DOS leg), `megadosp` and `megadosroom` (kern_mega must build with no part); `t_stkapps`, `t_stkclass`, `t_movable`, `pins`, `pinsrt` |
| **3** | The heap behaviour: the bracket drops the part; the claim moves between runs; a refused fetch says so. New legs: `doswtwo` (or a `doswpart` row) asserts a never-windowed instance holds no part claim, the first windowed Run makes one pinned 5 KB claim, a compaction between two windowed Runs moves it and the second Run still works, a bracketed Run in the same window has the part's KB back, and Run in Window with DOS.O88's disk out says so. **Break it on purpose**: skip the re-patch after a move, and the second Run must go red. | the new legs; `doswtwo`, `dosaltenter` |
| **4** | The numbers, in the final pass: `doswspin`'s ratio, `doswcell`'s pass, and a new `doswcell` leg pricing the virtual `int 10h` per call (section 9's trace, kept), so this plan's "+0" becomes a row that fails at +100. The first-Run fetch time on `os8088_5150_cga_gla`, HELP's instrument. | `doswcell`, `doswspin` |

## 8. What would conflict with the network part

Another agent is moving `dosnet.inc` and the packet driver into a part of
their own in `dos.asm` (DOS-PARTS-PLAN section 3). This study prototyped only
in `dosw.inc` and reverted it, so nothing here conflicts today. Building it
would meet that work at four places:

- **the `DSLH` block and the loader's row copy** (`dosload.asm`): both append
  a row after `DSLH_HLPROW`. Serialise them; the second rebases on the first.
- **`dos_run`**: both fetch from it - the network part at `dos_pkt_bufs`, this
  one in the windowed branch before the arena claim - and the bracket's drop
  of this part sits beside the network part's own teardown in `.out`.
- **the shared infrastructure**: `dos_pfetch` and the bouncer `dos_hb` are
  wanted by both. Whichever lands first builds them; the other reuses.
- **`dos_reloc`** and the owner-claim count: both add a claim per instance
  (four or five of eight in a wired windowed run, E).

## 9. What was measured, and how

**The base.** `tests/dostrap/dwcell.asm` run in its window exactly as the
`doswcell` row runs it (bracket first, then Program > Run in Window), on
`os8088_5150_herc_gla`; `os88marty.bp_trace` with guest cycles on
`dvw_int10`, `dvw_int10.out`, `con_write`, `dvw_cdone`, `dvw_irq0`, the
instruction after `dvw_irq0`'s `call dvw_xpost`, the one after its far call,
and `dvw_irq0.ret`. Phase 1 is the probe's `AH=09h`, phase 2 its teletype
digit and backspace. Three runs on three builds, one variable each:

| build | `AH=09h` | glyph | backspace |
|---|---:|---:|---:|
| the tree as it is | 2,201-2,216 | 3,081-3,088 | 2,502-2,522 |
| **A**: `.virt`'s `push cs / pop ds` replaced by `mov ds, [cs:dvw_csview]` - the native design's only change to this path | 2,203-2,216 | 3,081-3,088 | 2,502-2,522 |
| **B**: A, plus `con_write`, `con_celloff` and `con_markcells` reached through `call far [cs:dword]` to a `call x / retf` thunk - exactly C''s `HCALL` | 2,548-2,566 | 3,221-3,225 | 2,611-2,637 |

B against A is three crossings on `AH=09h` for +332 to +345 - **~111 cycles a
crossing (M)**, which agrees with DOS-PARTS-PLAN's ~110-130 estimate - and one
on a glyph for +137 to +140. A against the tree is within 6 cycles: the
8088's prefetch hides the longer encoding. Both prototypes were reverted and
the tree rebuilt; `make`'s fast tier passed on each (68 passed).

**Not measured, and named as such**: every E above, in particular the tick's
+~40, the bouncer's ~390, the INT 33h thunk's ~90 and the first-Run fetch
time. Waves 2 and 4 measure them.
