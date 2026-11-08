# apps/os88ui.inc: the standard controls the KERNEL assembles into itself (OS88UI_KERNEL)

Gap review, 2026-11-08. No repository file other than this report was edited;
nothing was assembled or booted. Every byte figure below is a SYMBOL SPAN read
off `build/kernel.map` (kern_big), `build/smallk/kernel.map` (kern_small) and
`build/megak/kernel.map` (kern_mega) at this tree, unless it says ESTIMATED.

## Scope read

| file | lines read | what |
|---|---|---|
| `apps/os88ui.inc` (6,676) | 1-1,400 whole; 1,400-2,120 whole; 2,123-3,015 (the scroll bar, all of it); 6,480-6,676 (the kernel `.bss` and the far-entry tail); every `%if*` arm by grep | the UI_* macro layer, both arms; `OS88UI_BTN_BODY`/`KBTN_BODY`/`BHIT_BODY`; the glyph block and `OS88UI_KGLYPH`; the click machinery; `os88ui_sb*` |
| `kernel/fdlg.inc` | 1-90, 1,036-1,200, 1,225-1,330 | the include site, `fdlg_blabel`, `fdlg_drawbtn`, `fdlg_h_click`/`_up`, the KBTNIMG expansions |
| `kernel/ctrl.inc` | 140-150, 240-335, 984-1,290 (section map), 1,388-1,400, 2,780-2,880, 4,270-4,310, 5,775-5,800 | CPXK/CPXC/CPXN, `cp_kinit`, `cp_gemit`/`cp_bemit`, `cp_krect`, the button tails |
| `kernel/files.inc` | 3,221-3,460, 3,591-3,612, 3,860-3,870 | `fm_brect`/`fm_bhit`/`fm_btn1`, `fm_sbset`, the arm sites |
| `kernel/hiber.inc`, `kernel/shutdown.inc` | 355-365, 1,090-1,200; 230-280 | the other two `os88ui_krect` users |
| `kernel/kernel.asm` | 1,260-1,300, 3,980-3,995, 7,010-7,030, 7,280-7,312 | `OS88UI_SBDRAG`, `apic_gfx_points`, HBGC/KGO_PAIR, include order |
| `kernel/vga12.inc`, `kernel/font.inc`, `kernel/icons.inc` | 4,990-5,060; 1,825-1,840; 60-80 | `gfx_pen_cf`/`_dis`, who reads `[gfx_dis]`, the glyph guard |
| `tools/os88ovlchk.py` | 1-60, 95-207, 229-440, 1,845-1,870 | EXTRA, SECT/LABEL/TLABEL, how labels enter `where` |
| `tests/textsites.txt`, `tests/btnsites.txt`, `tests/unit/t_btnrules.py` | the os88ui rows | the ratchets that cover this file |
| SPEC.md | 13.10-13.10.5.10, 13.14.6, 13.15.2, 20.5.1 (45,157-45,260), 38.0 (108,040-108,055) | the contracts |
| `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md` | grep for os88ui/button/glyph/krect/arm | nothing listed for this file in either |

### Where it lands, per kernel (MEASURED spans)

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| `.cold`, resident | **1,089**: `os88ui_btn` 217, `_bhit` 23, `_arm`/`_fire`/`_armed` 18, scroll bar 459 + its drag 319, `os88ui_kbtn` 49, `os88ui_arm_f` 4 (DB84-DFC5) | **459**: the scroll bar only, no drag (4AF0-4CBB) | 1,089, the same bytes (99AE-9DEF) - but `.cold` sits above 1MB there, so it is not conventional |
| CTRL.DRV `.modu` | **278**: `os88ui_gring` 101, `_gdot` 48, `_glyph` 129 (F84-109A) | **563**: the glyph 278 plus `os88ui_btn` 236 and `os88ui_kbtn` 49 with far primitives (312-545) | 278 (FDC-10F2) |
| FDLG.DRV `.modd` | - (no FDLG.DRV) | **324**: `fdb_btn` 236, `fdb_kbtn` 49, `fdb_bhit` + pad (4FC-640) | - |
| `.bss`, resident | **56**: `os88ui_krect` 8, `_krlbl`/`_krflg` 4, `_krec` 16, `_ksb` 14, `_sbd_*` 12, `_armw` 2 (ADBC-ADF4) | **42**: the same less `_sbd_*` and `_armw` (7ECF-7EF9) | 56 (C43B-C473) |
| `.text` | 0 | 0 | 0 |

Callers, by census (grep of `kernel/` for `os88ui_` code sites): `files.inc`
(.cold: header buttons kern_big only, `files.inc:3221-3409`; the bar on both;
the arm kern_big only), `fdlg.inc` (.cold on kern_big, FDLG.DRV on kern_small),
`ctrl.inc` and `shutdown.inc` (CTRL.DRV, both kernels), `hiber.inc`
(HIBER.DRV, kern_big only), and `cp_kinit` (`ctrl.inc:324-330`, which is
**`.text`**, map line 6054). `apps.inc` has no reference at all any more.

## Verdict

The shared-source design is sound and its kernel arm is better engineered
than any comment about it now says: the button and glyph are not resident on
kern_small at all, the glyph is resident on neither kernel, and the kernel's
scratch-record discipline (one rect, one record, one arm, all `.bss`, all UI
task) holds by census. Three things need work. **The kernel's button breaks
the file's own drawing rule 1** - it fills the interior and letters the caption
one or two `gfx_frame` calls later, the exact fill-then-draw interval the
scroll thumb was fixed for in SPEC.md 13.10.5.9 - and a reorder costs about ten
bytes. **The tool that guards this split cannot see the split**: `os88ovlchk`
never learns where `os88ui_btn`, `os88ui_kbtn`, `os88ui_bhit` or the `fdb_*`
copies live, and it files the glyph block as `.cold` while it is `.modu`, so
the KBTNIMG/KGLYPH arrangement is correct today by inspection only. And the
file's commentary, the include site's banner and SPEC.md 20.5.1 describe a
kernel copy that no longer exists (one `.cold` blob, `os88ui_btn_f`,
`apps.inc`'s Timer, two kernel scroll bars, a meaningful `OS88UI_FILL`).
About 35 resident bytes are available on kern_big with no ABI change. Turning
the control into API slots is not worth it for the button and is a priced,
non-breaking option for the scroll bar only.

## Strengths

1. **One body, two worlds, done with macros rather than a fork.** The UI_*
   layer (`os88ui.inc:113-274`) lets the kernel arm be strictly cheaper than
   the package arm - `UI_ISMONO` is one `cmp byte [vid_mono], 0` against
   twelve pushes and pops and an `OSAPI_VIDEO`, `UI_INK` a store against a far
   call - while the body stays byte-identical source. `OS88UI_KFAR` gives a
   block that lands in a module image far primitives without a second body.
2. **kern_small pays nothing resident for a button.** `OS88UI_KBTNIMG`
   (`fdlg.inc:1281-1296`) expands the body into the only two images that draw
   one; resident `.cold` holds the scroll bar alone (459 bytes). The cost is
   285 bytes of duplicate module code, on disk and only while loaded.
3. **The `.bss` rule is stated where it bites.** `os88ui_krect`'s comment
   (`os88ui.inc:6531-6548`) records the incident that made it - eight bytes of
   coordinates through `sch_isr` - and every kernel scratch since (`_ksb`,
   `_sbd_*`, `_armw`) follows it.
4. **The scroll bar is geometry, not policy.** `os88ui_sbhit` returns part
   codes (`OS88UI_SBUP` .. `OS88UI_SBTHUMB`, `os88ui.inc:2168-2176`) and leaves
   what a part does to the caller - the Macintosh Control Manager's
   `inUpButton`/`inPageUp`/`inThumb` split, arrived at independently. The
   thumb is bordered before it is filled (`os88ui_sbthdraw`, 2,354-2,380), the
   inverse's divide is proved not to overflow (13.10.5.3), and the degenerate
   track is refused rather than drawn.
5. **Combinations that mean nothing do not assemble.** `OS88UI_BIMG`,
   `_BOWN`, `_NOGLYPH`, `_NOGRADIO`, `_NOGCHECK`, `_NOBTNINIT`, `_NOGEST`
   and `_KGLYPH` each `%error` in the wrong world (`os88ui.inc:434-449,
   560-609, 1326-1342, 1381-1388`).
6. **Text is only ever the opaque run.** `UI_RUN` is the only text macro
   either arm defines; `tests/textsites.txt:57-60` records the file going from
   3 transparent sites to 0, and no kernel arm reintroduces one.
7. **The scratch invariant holds.** Every writer of `os88ui_krect`,
   `os88ui_krec`, `os88ui_ksb` and `os88ui_armw` is a UI-task callback or
   painter: `fdlg_brect`/`fdlg_h_click` (dialog), `fm_brect`/`fm_btn1`/
   `fm_sbset` (Disk window), `cp_krect`/`sd_rect` (CTRL.DRV), `hbm_btnrect`
   (HIBER.DRV), and the gestures. No worker, ISR or package path reaches one,
   and staging is always immediately followed by its one consumer.

## Findings

### F1 - the kernel's button fills, frames, then letters (medium, now)

`OS88UI_BTN_BODY`'s plain body (`os88ui.inc:806-911`) runs `UI_INK` -
`UI_FILL` the whole interior (829) - `UI_PEN` (836) - `UI_FRAME` (849) -
(`UI_FRAME` again for the default ring, 860) - `UI_WIDTH` (862) - `UI_RUN`
(908). Every caption cell is written
twice, and between the two writes there are one or two `gfx_frame` calls -
3.4 ms each on the 5150 for an 11x11 box (CLAUDE.md's table; a 60-120 px
button is more). The file's own DRAWING RULE 1 (`os88ui.inc:62-68`) and
SPEC.md 13.14.6 rule 1 name exactly this; the sibling routine
`os88ui_sbthdraw` was rewritten for the same interval (13.10.5.9) and the
button never was. It is drawn on **every press and every release** of every
kernel button (Disk window header, file dialog column - whose Open is the
DEF case, two frames - Control Panel, shutdown card, hibernate dialog), and
in every package that does not opt into `OS88UI_BIMG`.

The one-write body exists (`OS88UI_BIMG`, 911-1238) but costs up to four
fills a button and ~150 bytes, and is `%error`ed in the kernel (443-445).
The cheap fix is ORDER: pen, frame(s), measure and centre the label, *then*
the interior fill, *then* the run. No call is added; the frame is outside the
fill's rect so neither touches the other; the interval between the two
writes of a caption cell drops from one or two drawing calls to none. The
kernel arm reads the caption ink back from `[gfx_color]` (889-893), so it must
bank that byte before the fill's ink store, and the label's CX/DX must
survive the fill - a push pair each. **ESTIMATED +8 to +12 bytes** in kern_big
`.cold` (and the same in each kern_small module copy and each package).
Prove it with the call-recording shape `tests/radio.py` uses, or
`m.flicker()` on a Disk window header press; the rows to name are
`tests/btnall.py` and the `disp*` set.

### F2 - `os88ovlchk` cannot see the split it is trusted with (medium, now, 0 bytes)

Three blind spots, all in the tool that `fdlg.inc:1257-1260` and
`ctrl.inc:266-273` cite as the guarantee:

1. **The button labels never enter the map.** `OS88UI_BTN_BODY`,
   `OS88UI_KBTN_BODY` and `OS88UI_BHIT_BODY` define their routine as a bare
   `%1:`. `LABEL` (`os88ovlchk.py:164`) needs a letter first and `TLABEL`
   (189) needs `%1 %+ suffix:`, so `os88ui_btn`, `os88ui_kbtn`, `os88ui_bhit`,
   `fdb_btn`, `fdb_kbtn` and `fdb_bhit` are absent from `where` - and a call
   to an unknown label is "untested" (docstring, line 14). On kern_small those
   six live in `.modu`/`.modd`; a resident near call to `os88ui_kbtn` would
   assemble, pass `make`, and run CTRL.DRV's offset in COLD_SEG.
2. **The glyph is filed in the wrong section.** `SECT` (152) requires
   `section .\w+`, so `section OS88UI_KGLYPH` (`os88ui.inc:1330`) is not read
   and the block stays under EXTRA's `.cold` (116). The panel's legal
   `.modu -> .modu` call is hidden with `CPXN` (`ctrl.inc:266-273`) - which
   means a future *illegal* resident call to `os88ui_glyph` is judged
   `.cold -> .cold` and passes.
3. **Two primitives have no far arm.** `UI_HLINE` (`ct_cw_gfx_hline`) and
   `UI_GRAY` have no `OS88UI_KFAR` arm (171-176); `UI_HLINE` is a NEAR call
   to a `.cold` trampoline. Used inside the KGLYPH block it would emit a near
   call from CTRL.DRV into its own image, and blind spot 2 would report it
   clean.

I checked by hand that **no resident caller reaches module-only code today**:
`fm_btn1`/`fm_bhit` are `%ifdef KERN_BIG` (`files.inc:3221-3409`), kern_small's
dialog calls `fdb_*` (`fdlg.inc:1145, 1185`), and `ctrl.inc`/`shutdown.inc`
call their own image's copies. The fix is tooling only: let `TLABEL` take an
empty suffix, resolve `section <macro>` through the `%define` the includer
made (or a `; ovlchk: section .modu` marker), retire `CPXN`, and give
`UI_HLINE`/`UI_GRAY` a KFAR arm or an `%error` under `OS88UI_KFAR`.

### F3 - `os88ui_kbtn` rebuilds a constant record on every call (low, now)

`OS88UI_KBTN_BODY` (`os88ui.inc:6619-6636`) stores five words into
`os88ui_krec` per call. Four are constants (`BT_LABELS = os88ui_krlbl`,
`BT_FLAGS = os88ui_krflg`, `BT_N = 1`, `BT_DOWN = 0`), and the fifth,
`BT_RECTS = BX`, is constant too: all six call sites pass
`BX = os88ui_krect` (`files.inc:3367`, `fdlg.inc:1108`, `ctrl.inc:2799/2801`
via `cp_krect`, `shutdown.inc:237`, `hiber.inc:1105`). `os88ui_btn` reads
only offsets 0, 2, 4, 6 and 10. A 12-byte initialised record in `.text`
replaces the 16-byte `.bss` one and the routine shrinks to two stores, a
`mov bx`, a `mov al, 1` and the call. **ESTIMATED -28 resident on kern_big and
kern_mega** (about -24 code, -4 data), **-4 resident and about -24 in each of
CTRL.DRV and FDLG.DRV on kern_small**; the callers' own
`mov bx, os88ui_krect` before the call becomes dead (-3 at each of two
`.cold` sites).

### F4 - `os88ui_arm_f` is a 4-byte resident shim nobody needs (low, now)

Its two callers are `cp_kinit` (`ctrl.inc:328`, in `.text`) and the panel's
press (`ctrl.inc:1395`, CTRL.DRV). Both run with DS = KERNEL_SEG and
`os88ui_armw` is `.bss`, so each can store the word itself - which
`files.inc:3864` already does (`or word [os88ui_armw], FM_BBAND`). Five-byte
far call -> three-byte `mov [os88ui_armw], ax`: **-4 `.cold`, -2 `.text`,
-2 CTRL.DRV** on kern_big and kern_mega. Pass 13 removed `_fire_f`,
`_armed_f` and `_glyph_f` on exactly this argument (6673-6675); this one was
left because one caller is `.text`, where a direct store is cheaper still.

### F5 - `os88ui_sbmine`'s identity is weaker than its premise (low, now)

The drag record is identified by the bar's `(x1, y1)` because "two bars on
screen at the same time cannot share a top-left corner" (`os88ui.inc:2878-2879`,
SPEC.md 13.10.5.10). The premise came from a dialog-plus-Disk-window world.
Today the file dialog has no bar and there are up to four Disk windows
(`files.inc:3349-3351`) filled by one setter, `fm_sbset`. Two Disk windows
with the same right edge and content top share `x1, y1`; if their heights
differ, a damage repaint of the lower one during a drag on the upper one runs
`fm_sbset -> os88ui_sbfix` and draws the lower window's thumb at the dragged
position, in the strip below the upper window. `fm_btn1` already answers the
same question for the pressed button by WINDOW (`[fm_vp]` against `[fm_dvp]`,
`files.inc:3349-3356`). Either bank `[fm_vp]` at the grab and gate the
`sbfix` call on it in `fm_sbset` (**ESTIMATED +8 `.cold`**), or add `y2` to the
identity (+2 `.bss`, about +10 code), which closes every case where the
collision is visible.

### F6 - `OS88UI_FILL` is a dead flag the kernel still loads (low, now)

The body fills unconditionally (`os88ui.inc:686-698`); only `OS88UI_BOWN`
ORs the bit in, for a package painter (672). The kernel sets it at seven
sites - `ctrl.inc:3011, 4298, 5799`, `fdlg.inc:1073`, `files.inc:3346`,
`hiber.inc:1143`, `shutdown.inc:257` - each a three-byte `mov di, imm16`
where `xor di, di` is two, and `fdlg.inc:1276-1277` `%error`s on a bit
nothing reads. **-7 bytes on kern_big** (2 `.cold`, 4 CTRL.DRV, 1 HIBER.DRV),
and the stale comments go with it (Doc/code mismatches 12). Keep the `equ`
for packages: it is source, not ABI.

### F7 - 28 bytes of kern_small's resident `.bss` serve two modules (low, record)

`os88ui_krect`, `_krlbl`, `_krflg` and `_krec` (7ECF-7EEB, 28 bytes) are read
and written on kern_small only by CTRL.DRV and FDLG.DRV; nothing resident
touches them. They stay resident because the body reads them through DS,
which is KERNEL_SEG in both images (`fdlg.inc:1289-1291`). Moving them into
each image's own data would need an image-relative DS, which the KFAR body
does not have, so this is recorded rather than proposed. After F3 the record
is 12 bytes of `.text` instead of 16 of `.bss`.

### F8 - the full scroll-bar paint greys under the thumb first (low, record)

`os88ui_sbar` lays `UI_GRAY` over the whole track (2,288-2,297) and then
draws the thumb on top, so the thumb's pixels are written twice on every
full bar paint. The interval is `os88ui_sbthumb`'s arithmetic plus a pen
call - well under one drawing call - and the incremental path
(`os88ui_sbmove`) already avoids it. Splitting the grey into two strips around
the thumb would add a call (~0.76 ms) to every full paint to remove a
sub-millisecond grey. Recorded so that nobody re-derives it; not worth doing.

## Rethinks

### R1 - the control as API slots instead of 70 copies (record)

There are 72 includers (`grep -l os88ui.inc` over `apps/` and `drivers/`).
Package copies MEASURED off `build/*.map`: button and gesture 299-1,030
bytes, scroll bar 495-838 (Note Pad, Word, Task Manager, The Wire, Scribe and
Browser each 838), glyph 248-284.

- **The button is not a slot candidate.** Its gesture calls the package's
  own NEAR procs (`BT_ONCLK`, `BT_PLACE`, `OS88UI_OWN`'s `call si`,
  `os88ui.inc:722-728`), which a kernel body cannot reach without the
  dispatcher, and kern_small has no resident button to export - a slot would
  put about 240 bytes back on the 64KB kernel. The file's own argument
  (`os88ui.inc:23-28`: the contract is the cost) stands.
- **The scroll bar is.** It is pure geometry over a seven-word block, already
  resident on all three kernels (459 bytes, plus 319 of drag on kern_big),
  and it calls no package code. An X-cell family (`SBAR`, `SBHIT`, `SBMOVE`,
  `SBTHUMB`, appended, so **no bump** - an A-section row, not a flag-day one)
  would cost the kernel an `es:` override on each block read (ESTIMATED
  +35) and the cells (+25): **about +60 resident on every kernel**, for
  **about -500 to -840 image bytes in each bar-using package** (roughly half
  that on an lz4 disk). Against the 64KB rule that is a real price for a
  disk-and-load-time saving. Record it as an A row so a future 360KB squeeze
  can price it without re-deriving.

### R2 - make the one-write button the only button (record)

Win 3.x's BUTTON and the Mac's CDEF each draw their control once, in final
colours. Here that is `OS88UI_BIMG`'s ring body. It costs up to three extra
fills a button (about 2.3 ms on the 5150 at 756 us a call) and ~150 bytes per
copy, so F1's reorder is the proportionate answer; record the full one-write
body as the rethink, for the day a button flash is measured as still visible
after F1.

## Prior art

- **Windows 3.0/3.1 USER**: BUTTON, SCROLLBAR and LISTBOX are system window
  classes - one resident copy, notifications to the parent through
  WM_COMMAND/WM_VSCROLL, and BS_OWNERDRAW for a face the system cannot draw.
  `OS88UI_OWN` is the owner-draw idea; the difference is that USER owned the
  gesture and the hit test centrally, which os8088 does in source.
- **Macintosh Control Manager**: one copy in ROM, per-type CDEF code
  resources, `FindControl` part codes and `TrackControl`'s action proc. The
  scroll bar's part codes match it one for one, and so does the
  geometry/policy split. `TrackControl` polled, which §13.7 forbids; the
  os88ui gesture is the event-driven form of the same thing.
- **GEM AES and OS/2 PM**: object trees and `form_do`, and PM's
  WC_BUTTON/WC_SCROLLBAR - the same "system owns the control" answer.

Every peer paid for one resident copy and thin applications. os8088 chose
source reuse because its smallest kernel lives in 64KB and a published
contract is hard to change. That was right for the button. The scroll bar is
the one place where the peers' answer would cost os8088 little (R1).

## Doc/code mismatches

Listed in full in the structured result; summarised here:

1. `os88ui.inc:39-44` - "Included ONCE, into `.cold` ... (fdlg.inc and ctrl.inc are cold in their entirety)": the glyph is in CTRL.DRV's `.modu` on both kernels, the button is in `.modu`/`.modd` on kern_small, and ctrl.inc is CTRL.DRV.
2. `os88ui.inc:6531-6537` - `os88ui_krect` "calls straight into os88ui_btn ... both Control Panel pages and the Timer": the callers go through `os88ui_kbtn`, also hit-test with the rect, and are dialog, Disk window, CTRL.DRV and HIBER.DRV; the Timer is a package now.
3. `os88ui.inc:6641-6658` - the "os88ui_btn_f / os88ui_glyph_f - the far entries" block describes two entries that no longer exist.
4. `fdlg.inc:1254-1260` - "apps.inc stages os88ui_krect ... far-calls in through fdx_go (and os88ui_btn_f)".
5. `fdlg.inc:40-46` - "OVERSTATES IT BY ~1,600 BYTES ... resident on BOTH kernels".
6. SPEC.md 20.5.1 (45,163-45,174) - "six primitive calls ... `cw_font_str` ... `apps.inc` is `.text` and crosses through `os88ui_btn_f`".
7. SPEC.md 20.5.1's table - `os88ui_btn` "optional white interior".
8. SPEC.md 13.14.6 - "§13.15 does not follow rule 1", which 13.15.2 corrects.
9. SPEC.md 13.14.6 and `os88ui.inc:78-81` - "two of the three things an emulator cannot show" (CLAUDE.md retired this; `m.flicker()` measures both).
10. `os88ui.inc:1697-1703` - "kern_small's dialogs still fire on the press" (`fdlg_h_click`/`fdlg_h_up` fire on the release).
11. `os88ui.inc:2868-2879`, SPEC.md 13.10.1/13.10.4/13.10.5/13.10.5.10 - "two kernel bars", "fdlg.inc as nothing" (the dialog has no bar).
12. `os88ui.inc:278-294`, `files.inc:3346-3348`, `ctrl.inc:4298`, `ctrl.inc:5775-5778` - `OS88UI_FILL` described as live.
13. `os88ui.inc:1426-1430` - the glyph's box is unconditional "where the button's is a flag".
14. `os88ui.inc:1404-1414` and `icons.inc:64-78` - "os88ui_glyph hands icon_draw_x the constant record `db 1, OS88UI_GW`" (it draws with fills now).
15. `os88ui.inc:1575-1576` - "the kernel's .cold and twenty-two package images".
16. `os88ui.inc:1739` - "(fdlg_bhit, fm_bhit)": there is no `fdlg_bhit`.
17. `tests/btnsites.txt:125-128` - fdlg.inc "far-calls os88ui_btn_f".
18. SPEC.md 38.0 (108,048) - "`os88ui_btn_f` does not exist on `kern_small`" (it exists on no kernel).

## Open questions

- F1's interval was taken from CLAUDE.md's frame cost, not measured on this
  button. A MartyPC `flicker` of a Disk window header press, before and after
  the reorder, is the number; it is a design-deciding measurement, so take it
  once, in the final pass.
- kern_mega: the `.cold` copy goes above 1MB, so it is not conventional
  there. I did not check whether `apic_gfx_points` called from `os88ui_gring`
  with DS = SS (`os88ui.inc:1527-1533`) behaves the same under the LDT
  selectors; the comment says it was designed for that.
- Should `tests/unit/` gain a ratchet listing the writers of `os88ui_krect`,
  `os88ui_ksb` and `os88ui_armw`? Their "UI task only" invariant holds by
  census today and nothing enforces it.
