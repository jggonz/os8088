# The UI task, toasts, clipboard, About, spinner

Area key: `ui`. Code read first, SPEC second; every claim carries a `file:line`
and names its build arm where a line belongs to one. Nothing was assembled or
booted.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/ui.inc` | 3,408 | all of it: `ui_task` (every step of the pass, both arms), `ui_bill`, `ui_arm_trk`/`ui_lit_*`, `ui_lcall`/`ui_activate`/`ui_raise`, `ui_rdown`/`ui_rcmd`, `ui_tdbl`, `ui_track`/`ui_drag`/`ui_grow`/`ui_grow_clamp`, `ui_loc_gate`, `ui_dispatch`, `ui_sys_open`/`ui_tm_back`, `ui_cmd`/`ui_cmd_reboot`, `ui_reboot_post`, `.bss`, `ui_volume_service`, `ui_clkcell` |
| `kernel/toast.inc` | 748 | all: `toast_show`, `toast_say`, `toast_now`, `toast_arm`, `toast_place`, `toast_up`, `toast_pass`, `toast_kill`, `toast_owner_gone`, both `TOAST_Q` arms |
| `kernel/apps.inc` | 148 | all (About) |
| `kernel/clip.inc` | 336 | all, both `OS88_COMPACT`/`KERN_MEGA` arms |
| `kernel/spinner.inc` | 275 | all (the `OS88_SPINNER` macro, both `.boot2` and module instances) |
| SPEC.md | | §13 intro and steps 1-5, 13.4, 13.5, 13.5.1, 13.7 (head), 13.9.2, 13.11, 13.12, 13.13; §14 intro; §55 whole; §59.0-59.5, 59.8 (part), 59.8.1 (head), 59.11; §75.0-75.2.1, 75.3.2; §39.29.4; §120.5; §32 head |
| context | | `kernel/menu.inc` 55-112, 380-470, 480-540, 818-826, 2583-2595; `kernel/events.inc` 180-300; `kernel/wm.inc` 147-165, 396-460, 2727-2765; `kernel/instance.inc` 1921-1950; `kernel/mouse.inc` 3190-3225; `kernel/memory.inc` 2540-2590; `kernel/fprog.inc` 161-171; `kernel/shutdown.inc` 1-130, 300-340; `kernel/kernel.asm` 325-370, 849-870, 1290-1320, 1568-1600; `kernel/viddet.inc` 82-130; `apps/os88api.inc` 4072-4080, 5660-5700, 5895-5935 |

Also read: `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md`,
`docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (nothing on this area),
and the `docs/plans/` hits for toast/clipboard/UI (`LAST-DROP-BYTES.md` 7.7.4,
`KERN-SMALL-CUT-PLAN.md` B5/C8). Listed rows are named where they apply.

## Verdict

The UI task is in good structural shape. Its model is sound and in places is
better than the period systems it imitates: one dispatch environment for every
window callback (`ui_bill`), a posted-work channel that a lock-holding caller
can always use, gated by one byte that the tick re-sets so a forgotten post is
late rather than lost, an idle that really sleeps with the lost-wakeup closed
by one `cli` span, and chrome boxes that fire on the release with the identity
re-checked. Toasts and the clipboard are small, honest designs whose hostile-
input bounds are real and whose length cap is enforced by the assembler.

The defects are narrow but real. The worst is that the About box indexes its
adapter table past the end on a VESA desktop (`kern_big`), a defect SPEC.md
39.29.4 named and nobody fixed. Next is a race the timer path was fixed for in
SPEC.md 13.9.2 that is still open on the content-click, right-click and drag
paths: the handler word is read before `gfx_lock` and never re-tested after
it. There are also smaller identity and latch defects in `ui_dispatch` and the
Alt+Enter path.

The largest weakness is prose. `ui.inc` is about one part code to five parts
comment, much of it a history of size passes, and this review found more than
forty statements in the area's comments, SPEC sections and SDK text that the
code no longer does. SPEC.md 13's numbered loop in particular describes a
`ui_task` from several redesigns ago.

## Strengths

- **One dispatch environment, `ui_bill`** (`ui.inc:1219-1247`). W_ONKEY,
  W_ONCLICK, W_ONMOUSEUP, W_ONDRAG, W_ONRCLICK, W_ONWH, W_ONTIMER, AM_ONCMD and
  the kernel's own `ui_rcmd` all reach a package through one body. It stamps
  the sound owner, bills cycles and calls `wm_pkgcall`, so their environments
  cannot drift. The payload is banked on SS and read back through BP with
  `xchg`, which also stores the start stamp into the slots it frees. That is
  the one place in the kernel where `[bp]` addressing SS is the intent.
- **Posted work that degrades to latency, never to loss** (`ui.inc:730-752`,
  `1081-1088`). The rule is "cleared before the walk, over-setting is free".
  `xchg al,[ui_post]` tests and clears in one instruction that an ISR cannot
  split, and the tick sweep turns a forgotten setter into a 55 ms delay. This
  is the Win16 lesson about WM_PAINT and WM_TIMER (synthesize from flags; do
  not queue) applied to kernel deferred work. It is worth copying wherever a
  new deferred flag appears.
- **The idle really halts, and the lost wakeup is closed properly**
  (`ui.inc:1156-1184`). The test of `[sch_uiwake]` and `task_sleep`'s state
  store sit inside one IF=0 span, and the byte is spent on the far side either
  way. The comment explains why `[ui_post]` must not double as the wake byte.
  Mac System 6's `SystemTask` spun and Windows 3.x idled only under Enhanced
  mode's int 2Fh 1680h, so this is ahead of both.
- **Release-edge chrome with identity re-checked** (`ui.inc:330-403`,
  `667-682`). The stale arm is dropped only when the button is up *and* the
  queue is empty (`ui.inc:773-783`), the case that keeps keyboard-mouse closes
  working. This is Mac `TrackGoAway` done as a once-per-pass sample instead of
  a modal loop (`ui_arm_trk`, `ui.inc:1326-1371`). `ui_lit_*` draws only on a
  state change, and `wm_chrome_relit` keeps the glass and `[ui_armlit]` in
  step.
- **Layout invariants the assembler holds, not prose**: `ui_armr` after
  `ui_post` (`ui.inc:3258`), the fall-through pairs `ui_arm_trk`→`ui_lit_on`
  (`1401`) and `ui_activate`→`ui_lcall` (`1469`), the toast flag-byte order
  (`toast.inc:137`), the queued and staged toast records of one shape
  (`toast.inc:743`), and the x/y word pairs (`ui.inc:3309-3320`). This is the
  right way to make a byte-saving trick safe.
- **Toast hostile-input handling** (`toast.inc:245-266`). Two independent
  bounds (characters kept, bytes examined), printable ASCII only, `cld`
  against a package running with `std`, TTL clamped to the signed-compare
  range (`toast.inc:214-217`). The length cap is the assembler's
  (`TOASTSTR`/`TOASTRUN`, `kernel.asm:340-367`), checked at the line that
  wrote the string.
- **`toast_now` decides on a fact** (`toast.inc:392-410`): the lock's owner
  against the running task, read as one word, with the torn-read case argued.
- **Clipboard claim sized to its content, kernel-tagged, source pinned across
  the claim** (`clip.inc:98-145`), with a header that says honestly why the
  burst is safe today and what would break it (`clip.inc:36-56`).
- **`ui_drag_phase`'s interval argument** (`ui.inc:2049-2063`): the snap moves
  the drop toward the origin, so it needs no clamp of its own. The two-display
  case, where the argument fails, keeps its guard. A model of how to write a
  safety argument down.

## Findings

Ranked. `phase`: **now** = no ABI break, **flagday** = needs one, **record** =
worth writing down, not recommended.

### ui-1 - bug, medium, now - About on a VESA desktop letters from a garbage pointer

`app_about_vid` has three entries, or four with `GFX_VGA`
(`apps.inc:57-60`). `app_about_paint_x` indexes it with `[vid_kind]` and no
bound (`apps.inc:113-118`). On `kern_big` with `VID_EXT`, `[vid_kind]` can be
`VID_VESA` = 4 (`viddet.inc:110`, `118-119`; `shutdown.inc:55` tests it). The
fifth word read is then the first two bytes of `app_about_v0`, `'VG'` =
0x4756, and the fourth line is lettered from `DS:4756h` up to the next NUL.
SPEC.md 39.29.4 lists exactly this (*"`app_about_vid` ... four pointers;
same"*, `SPEC.md:82191`). `fsx_capstab` and `cp_vidnam` were fixed
(`ctrl.inc:7463` carries `cp_vvesa_buf`); this table was not, and SPEC.md
120.5's table does not list it. The comment *"cbw is safe because [vid_kind]
is 0, 1 or 2"* (`apps.inc:117-118`) was already wrong for EGA.

**Recommendation.** Add a `%ifdef VID_EXT` row and a build-time bound:

```nasm
%if ($ - app_about_vid) / 2 != VID_KIND_MAX + 1
  %error "app_about_vid needs one row per [vid_kind] (SPEC.md 39.29.4)"
%endif
```

The assert is the systemic half. It makes the next kind fail to assemble here
rather than letter garbage. Alternatively ui-10 removes the table.

**Bytes:** about +18 `.text` resident on `kern_big`/`kern_emu`/`kern_mega`
(a `dw` and a short `'VESA - packed pixels'`-style string), 0 on `kern_small`,
0 for the assert.

### ui-2 - hazard, medium, now - three dispatch paths read the handler before `gfx_lock` and never re-test

SPEC.md 13.9.2 fixed a race in the timer pass: `gfx_lock` can block on a dying
**worker** that destroys the record (`inst_task_die`: lock, `inst_unwin`,
unlock, `mem_free_rec` - `instance.inc:1921-1946`). The pass then called
through a `W_SEG` that names freed memory. The fix was to re-test `W_FLAGS`
under the lock (`ui.inc:823-831`). Three other paths have the same shape and
no re-test:

- **content click**: `mov bp,[si+W_ONCLICK]` at `ui.inc:579`, before
  `gfx_lock` at `592`, with no test after it;
- **right click**: `mov bp,[bx+W_ONRC]` at `ui.inc:1569`, before
  `ui_rdown.lock`'s `gfx_lock` at `1578`;
- **W_ONDRAG**: `ui_arm_trk` tests `W_FLAGS` bit 1 at `ui.inc:1343` and reads
  `W_ONDRAG` at `1352`, both before the lock taken at `ui_rdown.lock`.

`wm_pkgcall` has no liveness check (`wm.inc:396-460`). The code's own
comments treat "a package worker reaching `inst_task_die`" in that gap as a
live path (`ui.inc:365-366`, `1343-1345`, `2562-2564`). It is narrow: it
needs a worker that dies on its own (a kern_mega fault catch, `sch_stkdie`,
`inst_pkg_alive`) while the UI dispatches to its window. When it hits, the
result is a wild far call, or a #GP on kern_mega through a freed LDT
selector.

**Recommendation.** Put the test once, in `ui_bill`, which every one of these
calls with the lock held: `test byte [si+W_FLAGS],1 / jz .out` at its head.
`wm_destroy` writes `W_FLAGS = 0` (`ui.inc:1296-1297`), and only the UI task
creates windows (`ui.inc:2566-2568`), so a zero is the whole test. That makes
`.tp_locked`'s own `W_FLAGS` re-test (`ui.inc:823-824`) redundant. Keep its
`W_ONTIMER` re-read.

**Bytes:** +6 `.text` resident in `ui_bill`; -6 on `kern_big` from
`.tp_locked`. Net 0 on `kern_big`, +6 on `kern_small` (no timer pass). The
`timerrace` row (`tests/timerrace.py`) is the template for a `clickrace` row.

### ui-3 - hazard, low, now - `ui_dispatch` identifies Locator by `AM_ONCMD = 0`, read outside the lock

`.notab` sends any set whose `AM_ONCMD` is 0 down the Locator route
(`ui.inc:2512-2526` → `.kernel` `2527-2536`). The test reads through the
owner's segment with no lock held. Two consequences follow:

1. A **package** set with `AM_ONCMD = 0` dispatches kernel commands. Cell 1
   is `CMD_CLOSE + item`, so its first menu's item 0 closes the front window
   and item 1 is `CMD_SCDEL` under `OS88_SHORTCUTS` (Remove Shortcut,
   `menu.inc:80`) or `CMD_FILES`. The SDK does not reserve 0
   (`os88api.inc:4072`). Only `menu_loc_set` uses it (`menu.inc:2590`).
2. `.handler` re-reads `[menu_set]` under the lock (`ui.inc:2586`), but the
   `AM_ONCMD` arm reads `[es:bx+AM_ONCMD]` with no `BX = 0` test
   (`ui.inc:2593-2600`). `menu_win_set` takes no lock (`menu.inc:448-457`). A
   set withdrawn to 0 in the gap after `.notab`'s check therefore calls the
   word at offset 2 of the package image. `jcxz` guards only a zero word.

**Recommendation.** Identify Locator the way `menu_relayout` defines it,
`[menu_win] = 0` (`menu.inc:497-503`): `cmp word [menu_win],0 / jne .app`,
falling into `.kernel`. Drop the cross-segment read (`push es`/`KES`/
`wm_strseg`/`cmp`/`pop es`). Add `or bx,bx / jz .app_done` in `.oncmd`. A
package set with a 0 handler then has its picks dropped by the existing
`jcxz`.

**Bytes:** about -19 `.text` resident, both kernels (30 bytes out, 11 in).

### ui-4 - hazard, low, now - `ui_cmd`'s jump table has no length assert

`ui_cmd` bounds the index by `CMD_LAST` (`ui.inc:3023-3028`) and jumps
through `.tab` (`3039-3045`). Nothing asserts that `.tab` has `CMD_LAST`
entries. A `CMD_*` added in `menu.inc` without a row assembles cleanly and
jumps through whatever word follows the table: the code of `.hiber` or
`.ctrl`. The comment's own count is already wrong (*"11 or 12 on big with
shortcuts"*; the truth is 10, or 11 with shortcuts: `menu.inc:75-88`).

**Recommendation.** Put `.tab_end:` after the `dw` lines and add
`%if (.tab_end - .tab) / 2 != CMD_LAST / %error ... / %endif`.

**Bytes:** 0.

### ui-5 - bug, low, now - a typed plain Enter matches the Alt+Enter latch

The latch holds the **scancode** 0x1C (`mouse.inc:3214`), and the dedupe test
is `cmp ah,[kbd_ae]` (`ui.inc:195-198`). Plain Enter (1C0Dh), Shift+Enter and
Ctrl+Enter (1C0Ah) all have AH = 1Ch. So an Enter still in the BIOS buffer
when Alt+Enter is latched is **replaced** by the Alt+Enter. On an enhanced
ROM that also enqueues 1C00h, the next pass dispatches that 1C00h as well
(`.typed`, latch now empty), so Enter-then-Alt+Enter inside one pass delivers
two Alt+Enters and no Enter. Two fullscreen toggles cancel out, and the Enter
is lost. On an XT ROM the Enter alone is lost. The window is one UI pass. A
keystroke wakes the UI (`mouse.inc:2897`), so it is short, but auto-repeat or
fast typing can land in it.

**Recommendation.** The two delivered spellings are 1C00h and 1CF0h, and both
have AL's low nibble 0, while the Enters (0Dh, 0Ah) do not. Add
`test al,0x0F / jnz .notae` before the `cmp`. That also covers the
Alt+numpad case the `or ah,ah` exists for.

**Bytes:** +4 `.text` resident, both kernels.

### ui-6 - bug, low, now - a toast raised under a fullscreen window expires unseen

`toast_pass` arms first and draws second (`.up`, `toast.inc:637-639`):
`toast_arm` sets `[toast_die] = ticks + ttl` (`toast.inc:445-447`), and only
`menu_draw_bar` checks `wm_fs_vis` (`menu.inc:825`). `toast_now` does the
same (`toast.inc:434-436`). A message raised while a fullscreen window or a
saver session (`blk_sv`, `ui.inc:1131-1132`) covers the bar therefore runs
out its three seconds invisibly. The comment says the opposite: *"a
fullscreen window is over these pixels and the message waits for it to end"*
(`toast.inc:632-633`). What waits is the drawing, not the message.

**Recommendation.** Either hold the staged message in `.up` while
`wm_fs_vis` answers CF = 1 (leave `[toast_want]` set and skip the arm), or
correct the comment and SPEC.md 59.3 to say a covered toast is lost. The
first is about +6 bytes and is what the comment promises.

**Bytes:** +6 `.text` resident, or 0 for the documentation route.

### ui-7 - weakness, low, now - DF is defended in `toast_show` and trusted in `clip_put`, the destructive one

`toast_show` does `cld` because *"OSAPI_SLOT does NOT cld - a package running
with `std` would walk its string backwards"* (`toast.inc:204-209`).
`clip_put` and `clip_get` trust SPEC.md 1 rule 3 instead (`clip.inc:150-151`,
`196-198`). The asymmetry is backwards. A `std` package that calls `clip_put`
makes the `rep movsb` write backwards from `clip_seg:0` into the arena below
it, which is kernel heap corruption from a package bug. A backwards toast copy
reads at most 256 bytes and stores into `toast_buf` downward (also bad, but
bounded).

**Recommendation.** Add `cld` in `clip_put` (and in `clip_get` for the same
reason), or remove `toast_show`'s and state one rule for the whole slot
surface. Defending at the slot that writes kernel-owned memory is the one that
matters.

**Bytes:** +1 (`clip_put`), +2 for both.

### ui-8 - hazard, medium, now - the SDK invites workers to call the clipboard; the code says they must not

`apps/os88api.inc:5669-5670` says *"legal from a window callback AND from a
worker (SPEC.md 20.6 rule 7)"*, and SPEC.md 55.4 says *"Any context ... so a
worker task may call them"*. The code's header says the opposite, citing
SPEC.md 55.4 for it (`clip.inc:36-56`). `clip_put` calls `mem_claim`, which a
worker may not do. SPEC.md 20.6 rule 7's list (`SPEC.md:46081-46087`) does
not include the clipboard. The copy is also a `rep movsb` of up to 32 KB
between two segments that may both move, and the header explains why that is
safe only on the UI task. A package author following the SDK is led into the
race the header describes.

**Recommendation.** Correct both texts (see Doc/code mismatches). If worker
access is ever wanted, the header already prices it: a `sch_lock` bracket
round both bursts, +16 bytes measured.

**Bytes:** 0 (documentation).

### ui-9 - weakness, low, now - "the kernel guarantees delivery" of W_ONMOUSEUP is not true when the ring dropped it

`ui.inc:325-327` and SPEC.md 13.7 promise a package that its release is
delivered. `.arm_done` clears a region-0 arm when the button is up and the
ring is empty (`ui.inc:773-783`). For the chrome this is right; for a package
it means that an `EVT_MUP` dropped by a full ring (16 records; wakes from
twelve windows plus presses can fill it) is never delivered. The package keeps
its pressed state drawn, which is the exact thing SPEC.md 13.7 exists to
prevent.

**Recommendation.** At the drop, when `[ui_armr]` is 0, dispatch the
window's `W_ONMOUSEUP` at `[mouse_x]`/`[mouse_y]` through `.mup_lk`'s body:
a synthesized release at the last known point. Or weaken the contract text
to "unless the event ring overflowed".

**Bytes:** about +12 `.text` resident (both kernels), or 0.

### ui-10 - size, low, now - About's fourth line as composed geometry, not a per-kind table

The four adapter strings and their table are about 107 bytes of resident
`.text` on `kern_big` and 81 on `kern_small` (`apps.inc:57-66`). They have to
be resident because DS reads them (`apps.inc:69-72`). Every new kind needs a
row, and the missing row is ui-1. Composing `"<name> - <w>x<h> - <colours>"`
from `[vid_w]`, `[vid_h]` and the depth, with a short name table, fixes ui-1
structurally (a 1024x768 VESA desktop then reads true) and shrinks.
`clock.inc`'s formatters (`clk_put4` and friends) are already resident.

**Bytes:** ESTIMATED -30 to -40 resident (name table about 26, `.cold`
formatter call about 30, against 107), if a formatter that takes a word can
be reused. Measure the span before committing.

### ui-11 - prior-art, low, now - one system double-click interval

The kernel has four independent detectors: `UI_TDBLT` (`ui.inc:61-66`),
`DESK_DBLT`, `FM_DBLCLK` and `FD_DBLCLK`. They are "deliberately the same
number rather than a shared constant". Packages carry their own:
`apps/syscheck/choose.inc:13` `SC_DBLT`, WIN16's `LC_DBL`
(`apps/win16/w16dlg.inc:180`), Lemmings. Mac (`GetDblTime`, the Mouse control
panel) and Windows (`GetDoubleClickTime`, `SetDoubleClickTime`) made this one
user setting, because the user's hand is the variable. On an XT with a serial
mouse and a beginner that is not hypothetical.

**Recommendation.** Use one `.text` byte with a resting value of 9, read by
the four detectors, and set it from the Control Panel's Mouse page (in the
module). Publish it as an inline cell (`mov al,[cs:x] / retf`) appended at
the end, so packages can use it.

**Bytes:** about +5 resident (a byte, and four `cmp ax,imm8` becoming memory
compares at +1 each), plus a 4-byte inline cell. The CP UI is in `CTRL.DRV`.

### ui-12 - prior-art, low, now - a clipboard generation count

The Mac Scrap Manager's `scrapCount`, bumped by `ZeroScrap`, is how
TextEdit's private scrap and the desk scrap agreed which was newer
(`TEFromScrap` on resume). os8088 has the same two-clipboard situation by
design: Paint's bitmap clipboard stays Paint's own (SPEC.md 55.1). It has no
way to tell an app whether the system text changed since it last looked, so
it cannot grey Paste cheaply when another app emptied the clipboard, and it
cannot arbitrate "which paste is newer".

**Recommendation.** Add `inc word [clip_gen]` in `clip_put`'s common exit,
and publish it as an appended inline cell, or answer it in DX from
`clip_size`, which is a contract widening and so needs care.

**Bytes:** about +4 `.text`, +2 `.bss`, +4 cell. Phase now (appended cell).

### ui-13 - rethink, low, flagday - a format byte on the clipboard slots

SPEC.md 55.1's refusal of a typed clipboard is right about a registry and
negotiation. But the cheapest future-proofing is a format byte in AL
(0 = TEXT) on `clip_put`/`clip_get`/`clip_size`, with any other value refused
today. Then a second format (Paint's bitmap, which 55.1 names) can arrive
without three new slots. Callers do not set AL today, so this has to ride a
flag day.

**Bytes:** about +8 resident (a compare and a refusal on each slot).

### ui-14 - perf, low, record - clipboard copies are `rep movsb`

A 32 KB put or get is about 117 ms at 17 clocks a byte (`clip.inc:39-40`).
`shr cx,1 / rep movsw / adc cx,cx / rep movsb` is about 12.5 clocks a byte on
the 8088's 8-bit bus, so roughly 87 ms. The question is whether to spend
about 6 resident bytes per site (12 total) to save 30 ms on the rarest
operation in the area. Answer: no. Recorded so nobody re-derives it.

### ui-15 - weakness, medium, now - history in comments is where the drift lives

`ui.inc` is about 3,400 lines, of which perhaps 550 are instructions. Most of
the rest is size-pass history ("kernel size pass 10, -5") and arguments with
earlier versions. Of the forty-plus mismatches listed below, about fifteen are
in `ui.inc`'s own comments, and almost every one is a historical statement
that went stale when the code moved: `ui_post_cmd`, `.close_box`'s lock, the
back buffer, Timer/Bounce, `STK0_SIZE` 1,024, `ui_lit_set`. The invariant
comments, by contrast (the `%if` blocks and the "binding" notes), are
accurate.

**Recommendation.** Adopt a convention for this file and its peers. A comment
states the invariant and the reason. Pass history and byte deltas go in the
commit message, where `git log -L` finds them. Sweep `ui.inc` once when the
mismatches below are fixed. 0 bytes, and a 4,915-line area that is easier to
re-read.

### ui-16 - consistency, low, now - under `TOAST_Q` a retire from one raiser takes down another's message

`toast_show`'s empty-string path calls `toast_kill` whatever is up
(`toast.inc:274`, `291-292`), and the queue logic is built on raiser identity
(`toast.inc:224-229`). So instance B's "take my message down" retires A's
message and promotes B's own queued one (or nothing). SPEC.md 59.11
documents it (*"A retire still kills the current toast"*), so this is not a
bug against the contract. It is a contract that contradicts its own
ownership model.

**Recommendation.** Retire only when `DH = [toast_inst]` (or when the caller
is the kernel, `0FFh`). That is `cmp dh,[toast_inst] / jne` before
`.kill`'s `call`.

**Bytes:** about +4 resident, `kern_big`.

### ui-17 - hazard, low, now - `ui_sys_open` indexes `fm_stattab` without the bound `toast_say` exists to carry

`ui.inc:2950-2952` does `mov bx,ax / shl bx,1 / mov si,[bx+fm_stattab]` on an
`LD_*` code with no bound. `toast_say` was written precisely because *"an
out-of-range code here would letter whatever the word after the table happens
to be"* (`toast.inc:324-327`), and `LD_NSTAT` is its bound
(`files.inc:2375`). Today `ldf_ld_run_name` answers in range, so this is
latent.

**Recommendation.** Use `ct_toast_say` with `BX = fm_stattab` and
`CX = LD_NSTAT`. Its own `cbw` and zero test replace this site's `cbw`, shift
and the `toast_show` setup. Note that `ui_tm_back` has to run before it, so
the order stays.

**Bytes:** about -3 resident (estimated).

### ui-18 - weakness, low, record - a drag starves background drawing to one yield a tick

`ui_track` lingers with the gfx lock **held**, yielding until the tick
(`ui.inc:1753-1761`). The lock drops only between the erase and the re-lock
(`1746-1750`). So background tasks run but cannot draw except in one yield
slot a tick. SPEC.md 13's *"background tasks stay live here"* is true of
their CPU and not of their pixels. This is a deliberate trade (an outline that
is lit more often than dark), and Win3.x and the Mac froze all drawing during
a drag outright. Record the truth in SPEC.md 13 rather than change it.

### ui-19 - rethink, low, now - a menu "about to drop" moment for packages

The kernel grants itself this moment: `ui_loc_gate`, `fm_bar_gate_x` and
`hb_ok_x` run on the press, before `menu_track` (`ui.inc:495-512`,
`2465-2485`). A package has no such moment, so to obey SPEC.md 47 rule 5
(grey a fact) it must re-point `AMENU_ITEMS` at every state change that could
alter an item, including changes it does not see, such as another app
emptying the clipboard. Mac apps' `AdjustMenus`-before-`MenuSelect` and
Win16's `WM_INITMENUPOPUP` are this moment. WIN16 already synthesizes it
(`SPEC.md:235269`).

**Recommendation.** Add a `W_ONMENU` record word installed by an appended
slot (`W_ONRC`'s shape, SPEC.md 13.11), called through `ui_bill` with the
lock held, after `ui_loc_gate` and before `menu_track`, only when the window
owning the bar installed one.

**Bytes:** about +14 `.text` resident, +2 per window record (24 bytes of
`.bss` on `kern_big`), a 6-byte cell. No `.o88` rebuilds.

### ui-20 - prior-art, low, record - one synchronous input path

Every callback runs on task 0, so a slow handler freezes the desktop: the
pointer stays live (ISR-drawn), but drags, menus and other windows do not.
`toast_now` exists because of this (`toast.inc:374-379`). OS/2 1.x Presentation
Manager had the same single synchronous input queue and the same "one hung
app hangs PM" property. Windows 3.x had it by being cooperative. GEOS on the
8088 separated the UI thread from each application's process thread, and
AmigaOS's Intuition ran input and window dragging in `input.device`'s task, so
both stayed responsive under a busy app. Adopting either is a different
system, not a change, so this is recorded. os8088's mitigations (cycle
billing per callback, the Task Manager naming the cost, worker tasks for long
jobs) are the right cheap answer.

### ui-21 - perf, low, record - the spinner recomputes the same eight endpoints four times a frame

`_rowmk` runs four times per frame (once for the plain row, three times
through `_mkput`, `spinner.inc:112-122`). Each run does 8 `_xform`s, each an
`imul` (`spinner.inc:187-216`, `262-273`), with the same `[_cos]`. Caching
the eight transformed x values per frame would save about 24 `imul`s (about
3.5k cycles, about 0.75 ms) per frame. At about 2 frames a second during boot
it is invisible, and the boot blob is full to the byte (`spinner.inc:134-136`).
Recorded, not recommended.

## Rethinks

- **Locator identity by window, not by data** (ui-3). The kernel decides "is
  this the kernel's bar?" by reading a word in a possibly-foreign segment
  without a lock, when the fact is already in `[menu_win]`. The general rule:
  identity decisions read kernel state, never a package's bytes.
- **Liveness at the dispatch funnel, not at each caller** (ui-2). SPEC.md
  13.9.2 fixed one caller. `ui_bill` is the funnel every callback passes
  through with the lock held, and it is where the test belongs. Every future
  callback kind then inherits it.
- **About from geometry** (ui-10). A table indexed by an enum that grows is a
  defect waiting for the next enum value. SPEC.md 39.29.4 found four such
  tables. Composing from `[vid_w]`/`[vid_h]` removes the class here; for the
  tables that must stay, a `%error` on their length against `VID_KIND_MAX`
  (ui-1) is the cheap guard.
- **Per-event handler words versus one window procedure.** os8088 has eleven
  record words (`W_ONKEY`, `W_ONCLICK`, `W_ONMOUSEUP`, `W_ONDRAG`, `W_ONRC`,
  `W_ONWH`, `W_ONTIMER`, `W_ONCL`, `W_ONWK`, `W_ONSZ`, `W_ABOUT`). Win16 and
  GEOS use one procedure plus a message number. On an 8088 the per-event
  vector is the better trade: a package pays nothing for events it never
  asked for, and the kernel needs no dispatch in the package. A flag-day
  rewrite would buy perhaps 200 bytes of `.bss` and cost every package a
  switch. **Recorded, not recommended.**

## Prior art

| subsystem | DOS / Win 3.x / OS/2 1.x / Mac 5-7 / GEOS / Amiga | os8088 | adopt? |
|---|---|---|---|
| deferred work | Win16 synthesizes WM_PAINT and WM_TIMER from flags when the queue is empty; DOS TSRs defer to int 28h (idle, InDOS = 0) | `[ui_post]` OR-byte over posted flags, collected where nothing is held; tick sweep | os8088 is the cleaner form. Keep it |
| idle | Mac System 6 `SystemTask` spins; Win3.x idles only via int 2Fh 1680h in Enhanced mode | `task_sleep` with the lost-wakeup closed: about 97% halted | os8088 better |
| close negotiation | Win16 `WM_CLOSE` → app may refuse, `DestroyWindow` itself; Mac apps ask in their own `DoCloseWindow` | `W_ONCLOSE` (refuse) + deferred `OSAPI_WM_CLOSE` with the negotiator retired | same model; the deferral (so the caller is not standing in its own freed segment) is os8088's own and right for real mode |
| chrome boxes | Mac `TrackGoAway`/`TrackBox`: a modal loop with the box highlighted while inside | arm on press, sample once per pass, fire on release over the same box | os8088 better (no loop holding the machine) |
| double-click | Mac `GetDblTime`, Win `GetDoubleClickTime`: one user setting | four kernel constants plus per-package copies | **adopt** (ui-11) |
| transient message | AmigaOS apps wrote the **screen title bar** (`SetWindowTitles(w, -1, msg)`); Mac System 7's Notification Manager flashed an icon over the Apple menu and queued requests | a strip in the menu bar's clock field, depth-1 queue (`TOAST_Q`) | the menu bar placement has real precedent; Mac queues properly where os8088's latest-wins slot drops. Not worth bytes on an 8088 |
| clipboard | Win16: formats, delayed rendering (`WM_RENDERFORMAT`), viewer chain; Mac: typed scrap + `scrapCount`, spilled to the Clipboard file when memory was short; GEOS: clipboard is a VM file of transfer items | one text claim, kernel-tagged, copied | delayed rendering is wrong for "copy, close, paste" on 128 KB; **adopt** the count (ui-12) and reserve a format byte at the flag day (ui-13) |
| menu enable | Mac `AdjustMenus` before `MenuSelect`; Win16 `WM_INITMENU`/`WM_INITMENUPOPUP` | the kernel gates its own items on the press; packages get no moment | **adopt** (ui-19) |
| input model | PM SIQ and Win3.x: a hung app freezes input; GEOS and Amiga run UI/input in their own thread or task | one UI task, billed callbacks | record (ui-20) |

## Doc/code mismatches

Each row: where, the current text, what the code does, and the proposed
text. **Code comments**:

1. `kernel/ui.inc:5` - *"drains one event from the system queue"* - it drains
   the ring, up to `EVQ_CAP` dispatches a pass (`ui.inc:300-312`, `707-713`).
   → "drains the system event ring (up to EVQ_CAP records a pass, SPEC.md
   10.2)".
2. `kernel/ui.inc:12` - *"ui_post_cmd for the kernel's own CMD_*"* - there is
   no `ui_post_cmd`. Its one caller writes `[ui_pcmd]` and bumps `[ui_post]`
   itself (`ui.inc:2640-2642`, `stikmenu.inc:1752`). → "a [ui_pcmd] store
   (and a [ui_post] bump) for the kernel's own CMD_*".
3. `kernel/ui.inc:14-15` - *"clk_tick every pass - it is"* - `clk_tick` runs
   only on a pass that sees `[ticks]` move (`ui.inc:1081-1085`, `1124`).
   → "clk_tick on every pass that sees the tick move (SPEC.md 13.12)".
4. `kernel/ui.inc:26-27` - *"on two things only - the frontmost"* - also a
   window's own `W_ONRCLICK` (`ui.inc:1569-1583`) and, under
   `OS88_SHORTCUTS`, a desktop shortcut's menu (`ui.inc:1611`). → "on a
   window that installed W_ONRCLICK (which takes it whole), the frontmost
   file-manager window's content, a DOCK TILE and, with shortcuts, a
   desktop shortcut".
5. `kernel/ui.inc:32-33` - *"and QUIT (close box ->"* - the press only arms;
   the close fires on the release over the same box (`ui.inc:671-682`,
   `386-392`). → "and arm the chrome boxes, which QUIT or minimize on the
   matching release (SPEC.md 13.5)".
6. `kernel/ui.inc:1201-1202` - *"ui_bill, which was already two of them"* -
   the predecessor was `ui_ptcall` (SPEC.md 13.7). → "it was ui_ptcall,
   which was already two of them".
7. `kernel/ui.inc:1216` - *"STK0_SIZE 1,024 and a measured 274-byte watermark"*
   - `STK0_SIZE` is 512 and the measured mark is 246 (`kernel.asm:1568-1578`).
   → "STK0_SIZE 512 and SPEC.md 15.1's measured 246-byte watermark".
8. `kernel/ui.inc:1682-1684` - *"and a full repaint IF it moved"* - it is
   `wm_paint_dmg` over the union (`ui.inc:2004-2033`); also *"clobbers: AX,
   BX, CX, DX, DI"* omits SI (`ui.inc:1786`, `1900`). → "and the union of the
   old and new rects repainted (wm_paint_dmg) IF it moved ... clobbers: AX,
   BX, CX, DX, SI, DI". The same SI omission is in `ui_grow`'s banner
   (`ui.inc:2151`).
9. `kernel/ui.inc:1733-1734` and `2139-2141` - *"a transient overlay never
   enters the back"* / *"rect and flush it twice per pass."* - the back
   buffer is gone (SPEC.md 32's opening; no `gfx_flush` in the kernel). →
   drop the back-buffer clauses: "VRAM-direct: a transient overlay, always
   XOR-erased before the lock drops".
10. `kernel/ui.inc:2455` - *"The tables-full refusals below (Timer and Bounce
    at their cap of ten)"* - Timer and Bounce are packages (SPEC.md 14.8). No
    such refusal follows. → delete the sentence, or "Per-kind caps (SPEC.md
    29.4.1) stay beeps rather than greying, because the predicate is
    per-kind".
11. `kernel/ui.inc:2575` - *"which is exactly why .close_box can take its own
    lock too"* - `.close_box` takes no lock; it arms (`ui.inc:671-682`). →
    "which is exactly why the close box's release (.mup) takes its own lock
    too".
12. `kernel/ui.inc:2704` - *"fits the 288px notice window, which a path"* -
    there is no notice window; it is a toast (`TOASTRUN`, `ui.inc:2695`;
    `toast.inc:9-13`). → "The message is a toast (TOASTRUN holds it to
    TOAST_MAX), which a path spelled out in full would not fit".
13. `kernel/ui.inc:2618-2639` - the banner *"ui_post_cmd - defer one of the
    kernel's own menu commands"* with *"in: AX = CMD_*"* documents a routine
    that does not exist (`ui.inc:2640-2642`). → retitle it "[ui_pcmd] - the
    posted kernel command (SPEC.md 13)", with "a poster stores the CMD_* and
    bumps [ui_post]", keeping the lock rationale.
14. `kernel/ui.inc:3024` - *"11 or 12 on big with shortcuts"* - `CMD_LAST`
    is 10 on `kern_big`, 11 with `OS88_SHORTCUTS` (`menu.inc:75-88`). → "10
    on big, 11 with shortcuts".
15. `kernel/ui.inc:3293` - *"ui_lit_set is the only"* - there is no
    `ui_lit_set`; the drawing body is `ui_lit_go` (`ui.inc:1407`), also named
    `ui_lit_set` in the banner at `1374`. → "ui_lit_go (ui_lit_on/off's body)
    is the only".
16. `kernel/ui.inc:1449` - *"KERN_BIG - SPEC.md 13.8.1/13.8.2/13.9"* - the
    13.9 timer pass is no longer in this block (it is inline at
    `ui.inc:785-844`). → "KERN_BIG - SPEC.md 13.8.1/13.8.2".
17. `kernel/menu.inc:69-71` - *"ui_cmd is a compare ladder and an id it"* -
    `ui_cmd` is a jump table whose entry for `CMD_SEP` is `.done`
    (`ui.inc:3026-3045`). → "ui_cmd's table sends it to .done".
18. `kernel/toast.inc:61` - *"toast_show STAGES and never draws, toast_pass"*
    - `toast_show` ends in `toast_now`, which draws when the caller holds the
    lock (`toast.inc:288-290`, `392-432`); so there are two drawing sites, not
    *"ONE drawing site"* (`toast.inc:64`). → "toast_show STAGES, and draws on
    the spot only for a caller that provably holds the lock (toast_now,
    SPEC.md 59.4); toast_pass draws everything else and is the ONE expiry
    site".
19. `kernel/toast.inc:117-118` - *"the buffer keeps 23"* - it keeps
    `TOAST_MAX` = 24 (`kernel.asm:340`). → "the buffer keeps TOAST_MAX".
20. `kernel/toast.inc:194-195` - *"23 bytes of rep-less movsb is ~475
    clocks"* - the copy is a filtered `es lodsb` loop of up to 24 kept and
    256 examined bytes (`toast.inc:245-266`). → "Up to 24 kept bytes of a
    filtered lodsb loop is ~1,000 clocks, ~0.2 ms; a hostile 256-byte scan is
    ~2 ms interrupts-off".
21. `kernel/toast.inc:421-424` - *"carry it to VRAM does not come until the"* -
    nothing carries pixels to VRAM later (no back buffer); the reason is that
    the UI task's pass does not come round. → "Done now because the idle pass
    that would draw it does not come until the operation this announces is
    over".
22. `kernel/toast.inc:632-633` - *"the message waits for it to end"* - the
    TTL runs while covered (ui-6). → "the strip is not drawn while it is
    covered; its seconds still run" (or fix the code).
23. `kernel/toast.inc:652-653` - *"callers hold the gfx lock (fpg_begin,"* -
    `fpg_begin` no longer retires a toast (`toast.inc:176-183`). The callers
    are `toast_show`'s retire and `toast_owner_gone` (grep). → "Its callers
    are toast_show's retire (inside its cli window) and toast_owner_gone
    (instance teardown, lock held or not)".
24. `kernel/toast.inc:329` (banner) - `toast_say` ends in `retf`
    (`toast.inc:364`) and is reached as `ct_toast_say`; the banner does not
    say FAR. → add "FAR (retf) - call it as ct_toast_say".
25. `kernel/apps.inc:117-118` - *"cbw is safe because [vid_kind] is"* ... *"0,
    1 or 2"* - it is 0..3 with `GFX_VGA` and 0..4 with `VID_EXT` (ui-1). →
    "[vid_kind] is 0..VID_KIND_MAX and the table has a row for each (the %if
    below it)".
26. `kernel/spinner.inc:259-260` - *"AX = 320 + (AL - SPL_HALFW) * cos / 64"*
    - it adds `[%1_l_cx]`, the layout's centre (`spinner.inc:270`). → "AX =
    [prefix_l_cx] + (AL - SPL_HALFW) * cos / 64".

**SPEC.md**:

27. §13 step 2 (`SPEC.md:28261`) - *"`[wm_fs]` non-zero (§11.2) → skip both
    menu-bar branches"* - the test is the `wm_fs_vis` predicate, not the
    latch (`ui.inc:475-481`; §39.17.1). Same at step 4 (`SPEC.md:28416`). → "`wm_fs_vis` answers CF = 1 (a visible fullscreen window,
    §39.17.1) → skip ...".
28. §13 step 2 (`SPEC.md:28265`) - *"`app_launch` KIND_CTRL (§31.5)"* - the
    cell calls `cpf_cp_open` (the module load), or `ui_clkcell` with a sound
    card, whose speaker goes to SOUND.DRV (`ui.inc:517-527`, `3392-3406`). →
    "...store CP_ITIME into [cp_sel] and open the Control Panel through
    cpf_cp_open (§2.8.4), which owns the refusal; with OS88_SNDCARD the
    speaker's 24px go to SOUND.DRV first (ui_clkcell, §34.15)".
29. §13 step 2 (`SPEC.md:28270`) - *"close box → **quit**: gfx_lock,
    `app_close_win` BX"* (and the minimize box) - both arm on the press and
    act on the release over the same box (§13.5; `ui.inc:330-403`). → "close
    box / minimize box → ARM (§13.5); the matching EVT_MUP runs app_close_win
    / inst_minimize under the lock".
30. §13 step 2 grow (`SPEC.md:28310-28316`) - *"the origin is fixed, so that
    is the origin plus the larger"* and *"clamp w to WMIN_W..`[vid_w]`−W_X"* -
    the floor is the window's own (`wm_min_axd`, `ui.inc:2397-2413`), the
    window is asked (`wm_ask_size`), snapped, and x is unioned over both rects
    because `wm_snap_win` can move the origin (`ui.inc:2348-2366`). → "clamp
    each axis between the window's own minimum (wm_min_axd, §11.100.2) and the
    screen edge, ask the window (wm_ask_size), snap, and repaint the union of
    both rects in x and the larger extent in y".
31. §13 step 3 (`SPEC.md:28386-28389`) - *"if `[clk_dirty]` is non-zero (§37):
    zero it and call `clk_rtc_write`"* - gone (§37.94; `ui.inc:1075-1080`).
    → delete the sentence.
32. §13 step 3 (`SPEC.md:28393`) - the *"ui_post_cmd  in:  AX = CMD_*"*
    contract block - no such routine (`ui.inc:2640-2642`). → "A poster
    stores AX = CMD_* into [ui_pcmd] and bumps [ui_post]; legal from any
    lock-held callback".
33. §13 step 5 (`SPEC.md:28435`) - *"5. `task_yield`."* - it is `task_sleep`
    for one tick inside one IF=0 span with `[sch_uiwake]` (`ui.inc:1156-1184`;
    `task_yield` only under `NOUIBLOCK`). → "5. Sleep until the next tick or
    an ISR's wake (task_sleep, §8.1.2.2/8.1.2.3)".
34. §13 menu dispatch (`SPEC.md:28444`) - *"three-word table {CMD_ABOUT,
    CMD_TIMER, CMD_REBOOT}"* - two words, `{CMD_ABOUT, CMD_CLOSE}`
    (`ui.inc:2614-2616`). The app-name cell (§12.7) is also a route not
    listed. → "the two-word table {CMD_ABOUT, CMD_CLOSE}", plus a route 0:
    "the app-name cell (§12.7) - its About handler, or the kernel's Close for
    its last item".
35. §13 `ui_cmd` (`SPEC.md:28455-28468`) - *"CMD_ABOUT/CTRL/CLOCK/BOUNCE/TASKS
    → `call app_launch`"*, Timer/Bounce instances, *"`files_open`"*, and
    *"CMD_REBOOT → gfx_lock (never released), `vid_text`"* - only About is an
    `app_launch`; Ctrl is `cpf_cp_open`, Tasks is `ui_tm_open`, Files is
    `fmf_files_open`, plus Hibernate, Shut Down, Remove Shortcut and Sticky
    Apps; Restart flushes the panel, detaches drivers, then `gfx_lock`,
    `vid_reboot`, `sched_unhook` (`ui.inc:3039-3174`). → rewrite the paragraph
    from `ui_cmd`'s table.
36. §13 RDOWN (`SPEC.md:28361-28362`) - *"`fm_rclick` then `fm_rcmd` (§22)"* -
    the command runs as `ui_rcmd` through `ui_bill` (`ui.inc:1591-1603`,
    `1632-1635`), and with `OS88_SHORTCUTS` the bare desktop opens a
    shortcut's menu (`ui.inc:1607-1612`), against *"that is still
    **nothing**"*. → "fm_rclick, then ui_rcmd billed through ui_bill ... and
    under OS88_SHORTCUTS a press on a shortcut opens its menu (sc_rclick,
    §26.8.5)".
37. §13 drag loop (`SPEC.md:28287-28292`) - *"while `mouse_btn` bit0 set"* -
    the loop ends on the `EVT_MUP` edge (`evq_mup`), with the level as the
    fallback, and lingers with the outline lit until the tick
    (`ui.inc:1735-1761`); background tasks run but can draw only in one slot
    a tick (ui-18). → "until evq_mup returns the release (the level is the
    fallback for a dropped one) ... each pass lingers, lock held, to the next
    tick".
38. §13.4 (`SPEC.md:28488`) - *"the count is a single word the producer only
    increments"* - `[evq_count]` is a byte (`events.inc:352`; the cell is `mov
    al,[cs:evq_count] / cbw`). → "the count is a single byte, which cannot
    tear".
39. §13.5.1 (`SPEC.md:28561`) - *"Both keep their own test."* - neither is a
    routine with a test any more: `ui_arm_chk` is written into `.arm_done` and
    `ui_arm_trk` opens with no test (`ui.inc:1278`, `1326`). → "Neither keeps
    a test of its own: ui_arm_chk is written into the ladder's step, and
    ui_arm_trk's banner says its caller tests".
40. §13.13 (`SPEC.md:30533-30536`) - *"Two of the thirteen should not be here
    at all"* ... `[clk_dirty]` - `[clk_dirty]` left this ladder (§13.12's own
    last paragraph, §37.94). → "One of them should not be here at all ...
    `[cp_dirty]`'s collection belongs where the click callback returns".
41. §13.11 (`SPEC.md:30629-30632`) - *"Cost in `.bss`: `MAX_WIN` words, 24
    bytes. It is therefore in `kern_small` too, unlike §13.8.2 and §13.9 —
    those are `kern_big`'s because they *grew the record*"* - `W_ONRC` is a
    record word now (the same section's opening; `wm.inc`), so it grew the
    record too. → "Cost: one record word, 2 x MAX_WIN bytes; it is in
    kern_small too, because a window that never installs one pays one read".
42. §13.9.2 (`SPEC.md:29367-29369`) - *"stopped at `ui_timer_pass.locked`"* /
    *"reach `.gone`"* - the labels are `ui_task.tp_locked` and `.tp_gone`
    (`ui.inc:822`, `832`; `tests/timerrace.py:59`). → use those names.
43. §14 (`SPEC.md:31729-31730`) - *"or "CGA - 640x200 - mono""* - the table
    also has `"EGA - 640x350 - 16 colors"` on `GFX_VGA` builds
    (`apps.inc:58-66`), and kind 4 is ui-1. → add the EGA line (and the VESA
    line once ui-1 is fixed).
44. §55.4 (`SPEC.md:110111`, *"**Any context.**"*) - the code requires the UI
    task or a window callback (`clip.inc:36-56`); SPEC.md 20.6 rule 7 does
    not list the clipboard; `clip_put` claims. → "**The UI task or a window
    callback, never a worker.** None of the three takes the gfx lock, touches
    VRAM or does I/O, but clip_put claims (which a worker may not, §20.6 rule
    7) and both copies are bursts between segments that may move; clip.inc's
    header gives the argument and the +16-byte sch_lock bracket that would
    lift it". Also §55's opening *"three routines and three words"* /
    55.2's *"clears the three words"* (`SPEC.md:110054`): `clip_kb` is a byte
    (`clip.inc:328`) → "two words and a byte".
45. §59.2 (`SPEC.md:111097-111101`) and §59.8 (`SPEC.md:111521-111524`) -
    *"It carries **one inverted space of padding at each end**"* and
    *"`Saved` is 7 cells"* - §59.8.1 replaced that with a one-cell inset and a
    4px lead; `toast_place` counts the message plus one cell
    (`toast.inc:516-519`, `92-95`). → "one cell for the 4px gap and 4px lead
    at its left (§59.8.1); `Saved` is 6 cells".
46. §59.3 (`SPEC.md:111147`) - *"**`toast_show` never draws.**"* - see item
    18. → "**`toast_show` stages; it draws only for a caller that provably
    holds the lock (§59.4)**".
47. §59.3 (`SPEC.md:111183-111184`) - *"`toast_kill` does not draw ...
    Two of its three callers hold the gfx lock."* - see item 23.
48. §59.3 (`SPEC.md:111191-111193`) - *"strip is in the clock's field now,
    wholly right of `[vid_clk_hx]`, so the two cannot touch"* - §59.9 gave it
    the gap as a first home, and it and the widget are kept apart by
    `toast_place` and §12.8.7's precedence (`toast.inc:185-191`, `501-505`).
    → "They are kept apart by geometry: toast_place reserves the widget's
    cells (§59.9.2) and a live toast outranks the widget (§12.8.7)".
49. §59.3 (`SPEC.md:111195-111198`) - *"inserting `.chk_toast` meant
    re-pointing `.chk_zones`'s `je` at it"* - `toast_pass` is a tick-gated
    step now, called unconditionally after the `[ticks]` compare
    (`ui.inc:1081-1095`). → delete the bullet, or "toast_pass sits in the
    tick-gated steps (§13.12), behind the one [ticks] compare".
50. §59.4 (`SPEC.md:111240`) - *"It ends in `gfx_flush`"* - there is no
    `gfx_flush`; `toast_now` ends in `fpg_bare` (`toast.inc:411-432`,
    `fprog.inc:161-171`). Also *"The test is three compares"* - it is two (a
    word for flag+owner, then `[sch_cur]`, `toast.inc:393-408`). → "The test
    is two compares ... The draw is direct to the glass (§32 has no back
    buffer), so nothing further is needed".
51. §75.1 (`SPEC.md:134350`) - *"recurse on a 384-byte task stack (§8)"* - a
    negotiator runs on the UI task, whose stack is `STK0_SIZE` = 512
    (`kernel.asm:1568`). → "recurse on the UI task's 512-byte stack
    (STK0_SIZE, §8)".

**SDK** (`apps/os88api.inc`):

52. `apps/os88api.inc:5669-5670` - *"legal from a window"* / *"callback AND
    from a worker (SPEC.md 20.6 rule 7)"* - see item 44. → "legal from the UI
    task and a window callback - NOT from a worker (SPEC.md 55.4)".
53. `apps/os88api.inc:5910-5911` - *"as an inverse-video strip at the right
    end"* / *"of the MENU BAR"* - the strip is centred in the gap the menus
    leave, or over the clock's field when it does not fit (§59.8, §59.9). →
    "as an inverse-video strip in the MENU BAR, beside or over the clock".

## Open questions

- **ui-2's reach.** Which teardown paths can make a *visible* window's
  instance die without the UI task's own close? A kern_mega worker fault
  catch, `sch_stkdie`, `inst_pkg_alive` and WIN16's own task death are the
  candidates. The code's comments assume at least one is live; a
  `clickrace` row modelled on `timerrace` would settle it.
- **`ui_grow`'s y union** (`ui.inc:2342-2347`) takes the new `W_Y` as the top.
  On one display nothing moves `W_Y` down during a grow (`wm_dock_snap` moves
  up). Can `EXT_LAND` on two displays move the origin *down* across the seam,
  leaving the vacated top rows outside the repaint? `wm_dmg_vacate` records
  the old rect, but it is not clear the union reaches it.
- **TOAST_Q and the kernel as one raiser.** All kernel messages share
  `DH = 0FFh`, so a kernel verdict replaces another kernel message rather than
  queueing behind it. Is that intended (it is what makes `Encoding...` →
  `Saved` one message for Paint, which is a package), or should the loader's
  and the file layer's verdicts queue?
- **`.tp_armed`'s `inc byte [ui_post]`** (`ui.inc:839`) makes every pass with
  any timer armed walk the whole posted ladder. That is what makes a due
  timer fire on its own tick rather than one late, so it looks load-bearing.
  It is worth a sentence in SPEC.md 13.9 saying so, since it reads as
  redundant against the tick sweep.
