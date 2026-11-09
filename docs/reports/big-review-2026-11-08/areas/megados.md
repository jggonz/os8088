# kern_mega DOS support, parking, residency; kern_dos

Reviewer: megados area, big review of 2026-11-08 (branch `the-big-review`).
Read-only review. Nothing was built or booted for it. Sizes come from
`build/megak/kernel.map` and the `kerndos*.bin` files already in `build/`, or
are estimated from the source and marked as estimates.

## Scope read

Read in full unless noted:

| file | lines | notes |
|---|---|---|
| `kernel/megados.inc` | 2,351 | every proc: the claim, the runs map, the snapshot, the sum, the drop, the wake, the 8042 ledger, W6/W7, `mega_pkgok` |
| `kernel/megapark.inc` | 393 | the park stub (HIBER.DRV `.modh`) |
| `kernel/megares.inc` | 590 | the streaming resume's stub and tail (HIBER.DRV `.modh`) |
| `kerndos/kdentry.inc` | 1,133 | |
| `kerndos/kdxpack.inc` | 829 | |
| `kerndos/kdxms.inc` | 692 | |
| `kerndos/kdresume.inc` | 653 | |
| `kerndos/kdshim.inc` | 605 | |
| `kerndos/kdlaunch.inc` | 477 | |
| `kerndos/kdmouse.inc` | 477 | |
| `kerndos/kdpark.inc` | 469 | |
| `kerndos/kdback.inc` | 327 | |
| `kerndos/kdhma.inc` | 297 | |
| `kerndos/kdlayout.inc` | 274 | |
| `kerndos/kdos.asm`, `kerndos.asm`, `kdboot.asm` | 202 / 108 / 144 | |
| `kerndos/kdkbd.inc`, `kdosgate.inc` | 183 / 117 | |
| `kerndos/kdgate.inc` | 230 | first 60 lines only. It is a `KD_GATE` test artefact |

I also read these parts of other files:

- SPEC.md 96.40.9 (the park contract).
- SPEC.md 87.9's resume paragraphs (lines 158,914-158,966).
- docs/plans/KERN-DOS-PLAN.md sections 8 and 8.1-8.2.
- `kernel/hiber.inc`'s mirrored constants (lines 99-290) and `hbm_dosrun`'s kern_mega arm (lines 3040-3100 and 3630-3660).
- `tests/unit/t_mirror.py`'s file list.
- ABI-BREAK-LIST.md section 10, the KERNEL-BYTE-QUEUE.md grep, and CODE-REVIEW-TWO-CYCLES-2026-11-01.md row 4.

Row 4 of the two-cycles review (`mega_dhcfill`'s 28h descriptor) is fixed in the tree: it now uses `[mega_dstg]`.

## Verdict

The park is one of the best-engineered pieces of the tree.

- It returns twice, like `setjmp`.
- It parks only the kilobytes in use.
- Its two facts arrive by two independent routes (the launch block below 1MB and the header above it), and the stub cross-checks them.
- The parked kernel builds the stub itself, so nothing can skew between versions.
- One macro defines the sum for both of its takers.
- A wall of `%if` assertions pins the layout.

kern_dos follows the same discipline. It carries one disk layer and one DOS core across two hosts, a launch block that is a self-checking wire format, DOS=HIGH-style trampolines that put only one word on the program's stack, and an EXEPACK unpacker that walks the stream dry before it writes anything.

Most of the defects are at the edges, and most are on recovery paths that are rarely exercised:

- The stub's second integrity check, written for exactly W7's case, can never run for a park.
- The HMA kern_dos quietly takes the mouse vector off its A20 trampoline after the first poll.
- W7's A20 opening tests the gate only once, after clearing the anchor.
- The snapshot's own fallback can build a park smaller than the floor that keeps the stage clear of the loader.

The size story has two real items:

- About 1KB of the live resume (`kd_resume`, the extent walk and the 441-byte `hbs_stub`) assembles into both kern_mega kern_dos images, and kern_mega can never run it. On the LOW image that kilobyte comes off the DOS program.
- A 512-byte gate-only buffer sits in every shipped kern_dos's `.bss`.

On the documentation side, the mirror guard the comments rely on does not scan `kerndos/`. SPEC.md 87.9 still describes a resume route that is gone, and comments on both sides still talk about a hibernate path through the park stub that was retired.

## Strengths

1. **The snapshot returns twice** (`mega_dosparkf`, megados.inc:735, with `mega_dwake` jumping to `.ret` at 1,190).
   - The frame is banked at a named point, and everything after it is "lost or recomputed" (megados.inc:1022).
   - It is kern_big's `hbm_wake` pattern carried into protected mode, and it means the caller (`hbm_dosrun`) is one straight-line routine with an `or al, al` in it rather than a state machine.
   - Keep it as the model for any future suspend.
2. **The trip's facts come by two independent routes.**
   - `kd_unpark` passes the sum, the KB and the base in registers from the launch block (kdpark.inc:106-110).
   - The stub compares them with the header's own cells (megapark.inc:54-63), so a stray write that shrinks the header cannot restore too little and still agree with itself.
   - The header is NOT summed by design, and the reason is written down (megados.inc:45-48).
3. **The stub is the parked kernel's own build.** `mega_dstubup` copies it from HIBER.DRV's image (megados.inc:1495), so a kern_dos and a kernel from different commits only have to agree on two numbers, `KDP_GDT` and selector 08h.
4. **One sum, two takers** (`MPK_SUMLINES`, megados.inc:121-134). The 22-byte phase step and the "every window starts at phase 0" property let the kernel and the stub chunk the regions differently and still agree. This is the same source-sharing rule as WEAVE-SPEC 1.2, applied to an algorithm.
5. **The runs park** (`mega_dmapb`, megados.inc:592).
   - Because the claim map is out of band, free heap holds no state.
   - It parks 196KB where the whole of memory was 639KB, and the program still gets everything.
   - This is a property of the heap's design paying off somewhere nobody planned. It is worth stating in docs/HEAP-CLAIMS.md as a reason never to put headers inside heap blocks.
6. **`mega_dacc`** (megados.inc:690) sets the GDT's and LDT's accessed bits before the sum. This removes a whole class of "the CPU wrote a table I sum" failures for the price of one pass.
7. **W7's anchor is cleared before anything is decided** (megados.inc:2116-2128), so a recovery that fails is never tried twice. The stub's refusal clears it too (megapark.inc:220-229).
8. **The launch block is a real wire format** (kdlaunch.inc).
   - It has a magic, a version that is bumped when a row moves, and a payload length that the sender counts and the receiver recomputes from its own copy of the same `KDL_FIELDS` list.
   - A table-driven gather on one side and scatter on the other replaces two copies of marshalling code.
   - Its "zero means I could not fill this" convention (`KDL_NVOL`, `KDL_DPT`, `KDLF_RAHM` as runs + 1) is the right way to grow a fixed record. DOS's EXEC parameter block had none of this.
9. **One disk layer and one DOS core, two hosts.**
   - `kdos.asm` includes `apps/dos/dos.asm` whole and `kernel/disk*.inc` unchanged.
   - `kdshim.inc` is the complete list of what that code names and this root answers, each stub carrying the § that decided it.
   - `t_kdfar.py` (near versus far, read from the body) and `t_kdapi.py` (no opcode 9Ah into `KD_SEG`) turn the two classic hazards of this approach into build failures.
10. **The HMA trampolines** (kdhma.inc).
    - They are DOS 5's DOS=HIGH design, with the check on a private stack and only the FLAGS word on the caller's stack. That is MCB-safe for a program that shrank its block to its own SP (kdhma.inc:15-27).
    - The pass after hooking, plus `dos_hvec`/`dos_hslot` for vectors the core publishes later, is a complete design.
11. **kd_xunp checks everything before writing anything** (kdxpack.inc:39-44): the header, the table and a dry walk of the whole command stream. A refusal leaves the image byte-identical, which is what makes the LOADFIX and `kd_tolow` fallbacks safe to chain.
12. **Refusal discipline in `kd_resume`.** Every refusal before the handover falls back to `int 19h`, nothing is torn down until the last moment, and the last message prints before the extent list is written so a scroll cannot move it (kdresume.inc:208-235).

## Findings (ranked)

### md-1 (bug, medium, now): the park stub's PCHK check can never run, and its `mega_hbwake` leg is dead

Evidence:

- megapark.inc:69-75: `cmp ax, mega_dwake / je .cells` jumps a PARK straight past `.cells0`.
- `.cells0` (megapark.inc:76-89) is the check that compares the header's `MPK_C_PKB`/`MPK_C_PBASE` against the summed copy at `MPK_SAVE` (written by megados.inc:938-951). Its comment calls it the guard that catches "a header shrunk in BOTH places - W7's probe has no other copy to pass".
- `.cells0` is reached only when the wake is `mega_hbwake`. That never happens: `mega_dhcfill`'s only caller passes `mega_dwake` (megados.inc:1363-1364), and the hibernate resume now runs through `megares.inc`'s own tail (`mrt_wake`, megares.inc:579; written in hiber.inc:4469), not through the park stub.
- `mega_wrprobe` passes the header's own cells in registers (megados.inc:2141-2149 and 2229-2231), so the first check (megapark.inc:54-63) compares the header with itself.
- `MPK_C_PKB` sits at 3B8h, below `MPK_MAGIC` (400h), so the sum does not cover it.

Failure: after a warm reset, a park whose `MPK_C_PKB` was damaged passes every check. The restore loop then moves `[MPK_C_PKB]` KB, which is too little (silent corruption) or too much (the copy runs past 640KB into the adapter and ROM range and wraps into the HMA).

Recommendation:

- Route `mega_dwake` to `.cells0` (one label), and delete the `mega_hbwake` compare and its `cmp word [MPK_NREG], 0` arm.
- Every park has `NREG > 0`, and `MPK_SAVE[0..5]` and the header cells are written from the same `mega_dpkb`/`mega_dpbase`, so the normal trip still passes.

Bytes: about -10 in HIBER.DRV's image. No resident bytes.

### md-2 (bug, medium, now): the HMA kern_dos's mouse vector leaves its A20 trampoline at the first poll

Evidence:

- `kd_mou_arm` writes `CS:kd_mou_isr` into the IVT (kdmouse.inc:155-156), with CS = FFFFh on the HMA arm.
- `kdh_tramp` later rewrites every `KD_SEG` vector to a `LOW_SEG` slot (kdentry.inc:396, kdhma.inc:47-64).
- `kd_mou_rearm`, which runs on every INT 33h AX=3 and every key poll through `kd_mou_read`, tests `cmp word [es:bx], kd_mou_isr` and the segment against CS (kdmouse.inc:391-395). The slot fails that test.

Failure: the first poll re-arms. The vector is written back as `FFFF:kd_mou_isr` without the trampoline, the UART is re-initialised and one RBR byte is drained, possibly in the middle of a packet. From then on, a program that shuts A20 (a direct `int 15h AH=87h` on an IBM-AT-style ROM, or `AH=2400h`) takes its next mouse IRQ into wrapped low memory.

Recommendation: publish the vector through the core's `dos_hvec` in `kd_mou_arm`, and compare against `dos_hvec`'s answer in `kd_mou_rearm`.
- `kdh_vslot` hands back the existing slot for the same target, so the compare is stable.
- On every other arm `dos_hslot` is 0 and the answer is `CS:target`, so those arms behave as today.

Bytes: about +8 in each kern_dos image (HMA, not the program's on that arm).

### md-3 (bug, medium, now): W7's A20 opening tests the gate once, after the anchor is already gone

Evidence:

- `mega_wrprobe` clears the anchor (megados.inc:2116-2128), then tries port 92h and the 8042 (megados.inc:2167-2176).
- The `.kbw` after `DFh` waits only for IBF to clear, which happens when the 8042 has *read* the byte, not when it has switched the output port.
- It then tests once: `cmp ax, bx / jne .out ; still shut: an ordinary boot` (megados.inc:2181-2182).
- Every other opener polls: `kdh_tchk` up to 65,536 times (kdhma.inc:203-206), `kdp_a20w` and `mr_a20` 512 times.
- The anchor was read through `int 15h AH=87h`, which on an IBM-AT-style ROM leaves A20 shut, so the 8042 path is live on exactly the machines W7 was built for (a 286 AT with no port 92h).

Failure: Ctrl-Alt-Del inside a DOS program on an 8042-only AT can lose the session to a microsecond race, with no message.

Recommendation: poll the test, `xor cx, cx` / `loop`, as `kdh_tchk` does.

Bytes: about +8 in `.boot2` (transient: gone after boot).

### md-4 (hazard, medium-low, now): the snapshot's fallback can build a prefix park below `MPK_MIN_KB`

Evidence:

- The runs claim is `KB in use + MPK_SLACK` with no floor (megados.inc:345-357).
- When the recount at the snapshot outgrows it, `.fallb` parks a prefix of `[mega_dclkb]` KB (megados.inc:795-798). That figure can be below `MPK_MIN_KB` = 128.
- The `.ptry` loop enforces `MPK_MIN_KB` (megados.inc:362-364), and megados.inc:94-97 says that floor is what keeps the stage, at P - 16KB (`mega_dstgs`, megados.inc:1834-1840), "clear of its low half" of the loader's working area.
- `KDS_TMPSEG` puts the packed kern_dos stream at 64KB and up (hiber.inc:144).

Failure: a session whose claims grew by more than 16KB between the claim and the snapshot, on a machine whose runs total under about 112KB, gets a stage that overlaps the stream the stub is about to read. The stub overwrites itself.

Recommendation: clamp the runs claim to at least `MPK_MIN_KB` in `mega_dosclaimf` (`cmp ax, MPK_MIN_KB / jae / mov ax, MPK_MIN_KB`).

Bytes: about +8 in `.mega` (above 1MB).

### md-5 (weakness, medium, now): the mirror guard the comments rely on does not scan `kerndos/`

Evidence:

- `t_mirror.py` scans its hand-written `ASM` list plus `kernel/*.inc` (tests/unit/t_mirror.py:89-178, 343-344). No path under `kerndos/` is in it.
- Despite that:
  - kdlayout.inc:206 says the `KDB_*` mailbox is "MIRRORED in kernel/hiber.inc, and tests/unit/t_mirror.py is the guard".
  - hiber.inc:150 and hiber.inc:274 say the same about `KD_*` and `KDB_*`.
  - megados.inc:35 calls its `KDL_*` tail "MIRRORED (kerndos/kdlaunch.inc)".
- The constants that cross from the kernel to kern_dos, two artefacts on different disks, are exactly the ones nobody watches: `KDL_CAP`, `KDL_XBASE..KDL_SNAPHI`, `KDB_SEG/OFF/SIG`, `KD_LSEG`, `KD_H_LBP/LBSZ` and `KDL_FLAGS`.
- `KDP_GDT equ 0x380` (kdpark.inc:21) mirrors `MPK_GDT` under a *different name*, so even a glob would not compare the two.

Recommendation:

- Add `kerndos/*.inc` and `kerndos/kdos.asm` to the glob. Arm-dependent names (`KD_SEG`, `KD_ORG`, `KDL_VMAX` as an expression) go in `DIVERGENT` with their reasons.
- Rename `KDP_GDT` to `MPK_GDT` in kdpark.inc so the check bites.

Bytes: 0.

### md-6 (size, medium, now): both kern_mega kern_dos images carry the whole live resume, which cannot run there

Evidence:

- On `KD_MEGA`, `kd_resume` begins with `jmp .no` (kdresume.inc:54-60).
- Everything after it is still assembled:
  - the body (about 330 bytes);
  - `kd_extents`;
  - the strings;
  - `kdr_*` `.bss`;
  - `%include "hbstub.inc"`, 441 bytes (kdresume.inc:651).
- Only `kd_stageseg` is reached (from `kd_unpark`, kdpark.inc:27).
- `kdr_hma` is already gated (kdresume.inc:354).
- The LOW image (`KD_LOWDOS`) is off the DOS program: kdlayout.inc measures 37,510 of 37,888 against `KD_IMG_KB` 37, and `build/megak/kerndosl.bin` is 35,342 bytes of progbits.

Recommendation:

- Wrap `kd_resume`'s body, `kd_extents`, the strings, the `kdr_*` cells and the `hbstub.inc` include in `%ifndef KD_MEGA`, keeping `kd_resume: stc / ret` and `kd_stageseg`.
- Then measure the low image. A drop of about 950 bytes should put it under 36,864, which would mean `KD_IMG_KB` 37 -> 36 and **+1KB for every program run on the "Load DOS in the HMA" unticked arm**. That is predicted, not measured.

Bytes: about -950 in each kern_mega kern_dos image (estimated). kern_big's kern_dos does not change.

### md-7 (size, low-medium, now): `kd_glb`, 512 bytes of `.bss` in every shipped kern_dos, belongs to the gate

Evidence:

- `kd_glb: resb KDL_SIZE` is unconditional (kdentry.inc:1119-1121).
- Its only user is `kd_dos_entry`, under `%ifdef KD_GATE` (kdosgate.inc:18-111).
- `.bss` counts against the rung: `KIMG_SIZE = KTEXT_SIZE + KBSS_SIZE` (kdos.asm, near its end).

Recommendation: put it inside `%ifdef KD_GATE`.

Bytes: -512 of rung in kern_big's kern_dos (34,796 of 34,816 measured -> 532 left) and in both kern_mega images. This is not a kilobyte to the program on its own, and it should not be argued as one. These are bytes the next core change does not have to find.

### md-8 (weakness, low, now): `KDL_PLOW` is written, documented as binding, and read by nobody

Evidence:

- `mega_dosgo` stores it (megados.inc:1323-1330).
- kdlaunch.inc:259-262 says "kern_dos's arena must end below it".
- No file under `kerndos/` reads it. The arena is bounded only by `KDL_CAP` (kdentry.inc:196), which `mega_dosgo` lowers in the same branch. The two always agree, so this is not a bug.

Recommendation, one of:

- (a) drop the stores (about -11 `.mega`) and mark the word free; or
- (b) take md-15's header sum into it.

Either way, fix the comment.

Bytes: about -11 in `.mega`, or reused.

### md-9 (hazard, low, now): `mega_d8042get` can bank a keystroke as the command byte

Evidence:

- megados.inc:1946-1958 flushes, sends 20h, and takes the first non-AUX byte. The only validation is that bit 2 (SYS) is set.
- A key arriving between the flush and the controller's answer is taken if bit 2 of its scancode is set: 1Ch (Enter make), 1Dh (Ctrl make) and about half of all codes.
- If such a byte has bit 0 clear, `mega_d8042put` writes back a command byte with IRQ1 disabled. AEh does not set bit 0, so the trip comes home with a dead keyboard. That is KERN-MEGA-PLAN 54.3's symptom by another road.

Recommendation: either send ADh (disable the keyboard) before 20h and AEh after, which is the standard sequence, or also require bit 0, since the kernel never runs with IRQ1 off.

Bytes: about +4 to +6 in `.mega`.

### md-10 (hazard, low, now): `kd_unpark` and `mr_a20` wait only 512 probes for an 8042 A20 gate

Evidence:

- kdpark.inc:137 (`kdp_a20w`) and megares.inc:481 (`mr_a20.w`) poll `mov cx, 512` times.
- `kdh_tchk` polls up to 65,536 (kdhma.inc:203).
- On a fast 386 with an 8042-only gate, 512 probes is well under a millisecond.

Failure:

- In `kd_unpark` the user reads "A20 will not open... Press a key to restart" and `int 19h` follows. W7's probe will probably rescue the park, because the anchor still stands, but only if md-3 is fixed.
- In `mr_a20` the resume fails outright.

Recommendation: `xor cx, cx` in both places.

Bytes: -1 each (HMA kern_dos and HIBER.DRV).

### md-11 (hazard, low, now): the stub's "press a key" accepts a mouse byte

Evidence: megapark.inc:243-254 polls 64h/60h and treats any byte with bit 7 clear as a key. It does not test status bit 5 (AUX). A PS/2 mouse that a DOS program's driver left streaming (IRQ12 masked) restarts the machine on the first nudge, before the user has read the sentence. `megares.inc`'s refusal handles exactly this with A7h (megares.inc:225-251).

Recommendation: skip AUX bytes (`test al, 0x20`) as `mega_dkobf` does, or send A7h first.

Bytes: about +4 in HIBER.DRV.

### md-12 (weakness, low, now): `mega_dreg_add` silently drops regions when the list is full

Evidence: megados.inc:480-497 drops a full-list region with `jae .full`, and the dropped region is simply not summed. The list cannot overflow today: at most `MX_NBLK` - 2 pool blocks, one of them split by the snap record, plus about 10 fixed regions, against `MPK_RMAX` = `MX_NBLK` + 10. But only arithmetic in a comment holds that bound.

Recommendation: an `%if` that states the worst case, so a future fixed region fails the build rather than the sum.

Bytes: 0.

### md-13 (size, low, now): ten kdback.inc doors are `call X` / `ret` around a routine with the same contract

Evidence: `dos_k_read`, `rdat`, `write`, `append`, `wrat`, `delete`, `mkdir`, `rmdir`, `rename` and `dfree` (kdback.inc:50-113) each wrap a near target. `kdb_betab` could name those targets directly. The far, ES-sensitive and AL-sensitive doors (`find`, `path`, `vstat`, `rseq`, `wseq`, `goto`) keep their bodies.

Recommendation: name the near targets in the table.

Bytes: about -40 in every kern_dos (kern_big's rung included).

### md-14 (size/consistency, low, now): `mem_free_x` is a no-op behind a compare

Evidence: kdshim.inc:442-447 does `cmp dx, [kd_top] / jne .keep / ret / .keep: ret`, which returns no room on either path. The header claims "only the most recent claim actually returns room" (kdshim.inc:372-373). Only `kd_shed` ever returns room.

Recommendation: alias it to `kd_nothing` and correct the comment.

Bytes: -6.

### md-15 (rethink, low, now with a `KDL_VER` bump): check the park header before jumping into it

Evidence: nobody verifies the stub and its GDT (header +000..+3D1) before they run (megados.inc:45-48). A damaged header is "a stub that cannot run - a reset", or worse, a stub that half-runs.

Recommendation:

- The kernel puts a 16-bit sum of `[0, MPK_C_END)` into md-8's freed `KDL_PLOW` word.
- `kd_unpark` reads the header down with its existing `kdx_87` into the text page and checks the sum before the `lgdt`.
- A mismatch takes `kdp_s_a20`'s path, so W7 (md-3) still gets a try on the warm boot.
- Changing what the row means is a `KDL_VER` 6 -> 7 bump, on kern_mega's arm only.

Bytes: about +20 `.mega`, about +35 in the HMA kern_dos.

### md-16 (hazard, low, now): `mem_claim_x`'s overflow test is the last bit shifted out

Evidence: kdshim.inc:409-412 does `mov cl, 6 / shl ax, cl / jc .no`. CF is only bit 10 of the input, so 2,560KB shifts to 8000h with CF = 0. Every caller today asks for a few KB.

Recommendation: `cmp ax, 0x400 / jae .no` before the shift, or a comment stating the bound.

Bytes: +1.

### md-17 (weakness, low, record): the stub's restore loop is wrong below 33KB

Evidence: megapark.inc:172-190. The first chunk subtracts `bp & 7FFFh` from a count that was itself computed off `floor(500h / 1024)` = 1KB, so for `PKB` < 33 the restore ends 1KB short. `MPK_MIN_KB` and every runs park's first run (which holds the kernel) keep `PKB` far above that, which is why this is a record and not a fix. It is worth a comment if anyone shortens the floor.

Bytes: 0.

### md-18 (weakness, low, record): the trip's own time is not counted, and x87 state is not reset

Evidence:

- `mega_dosparkf` runs the whole copy and sum with IF = 0, and the stub runs with IF = 0.
- `KDL_TICK` is read after the snapshot (megados.inc:1342-1348), and the stub's restore happens before `hbm_wake` reads the tick, so the clock catch-up misses the out and back. That is milliseconds to about half a second on a 286. `MEGADIAG=1`'s `DOSP` line measures exactly this figure.
- `mega_dwake` restores the MSW but not the x87: a DOS program's control word survives the trip. Every FPU package in the tree `fninit`s per operation (apps/os88fp.inc:2770), so no package is exposed today.

Bytes: 0.

### md-19 (size, low, now): HIBER.DRV's private scratch is resident in kern_mega's conventional `.text`

Evidence:

- `mega_hbwk`, `mega_hbxl`, `mega_hbxd`, `mega_hbs`, `mega_hbr`, `mega_hbbl` and `mega_hbf` are 30 bytes at 4C66h..4C80h of `.text` (map), and only the module's `hbm_xwrite`/`hbm_xres` use them (39 sites in hiber.inc).
- `MPS_NAME`/`MPS_TKB` set the precedent (megados.inc:263-265): the park's scratch above 1MB.

Recommendation: move them to `MPS_*`. Each site gains an ES override, so the module grows by about 40 bytes.

Bytes: -30 conventional (resident), about +40 HIBER.DRV.

### md-20 (consistency, low, now): `KDLF_RAHSH` is a shift count among flag bits

Evidence: kdlaunch.inc:394 has `KDLF_RAHSH equ 8` beside `KDLF_NOMOUSE equ 8`, which is bit 3. The prefix says flag, and a reader checking for bit collisions trips on it.

Recommendation: rename it `KDL_RAHSH`.

Bytes: 0.

### md-21 (consistency, low, now): the A20 code is written out six times with four different budgets

Evidence:

- `mega_wrprobe` inline: one test (md-3).
- `kdp_a20t`/`kdp_a20w`/`kd_a20shut`: 512.
- `kdh_tchk_src`: 65,536, position-independent and run from LOW_SEG.
- `mr_a20`: 512.
- `mega_a20` in mega.inc.

They live in four segments and three load contexts, so they cannot share one body. They can share a macro for the test and the sequence, the `MPK_SUMLINES` precedent, which would make the budget one number.

Bytes: 0 to -20.

### md-22 (consistency, low, now): kdlayout.inc is a changelog with a definition file inside it

Evidence: 274 lines for about 30 definitions. The prose carries superseded measurements, such as "At wave 4 kdos.asm measures ... 45,827" and the `61 -> 43 -> 34` history, plus one truncated sentence (md-mismatch 15).

Recommendation:

- Keep the *current* measurement per arm and the rule ("raised to the smallest whole KB that fits").
- Move the history to SPEC.md 96.44's measurement log, where the § numbers already point.

Bytes: 0.

## Rethinks

1. **The park header as summed data, not unsummed code (md-15).**
   - The deeper version: the stub's *code* is the parked kernel's own build and lives in HIBER.DRV's pool block, which IS summed. If the stub ran a 30-byte bootstrap that first sums and compares the HIBER.DRV copy of itself, only that bootstrap would be unverified.
   - That costs about 40 bytes in HIBER.DRV and changes no ABI. md-15 is the cheaper half and probably enough.
2. **A sum density per region class.**
   - The 1/32 sparse sum is right for pool blocks, which can be megabytes (a RAM disk). For the region a single wild word most plausibly hits, though, it catches an isolated changed word only about 1 time in 32: the GDT, LDT and IDT, `.cold`/`.mega`, a few KB in all.
   - A per-region stride, dense over the kernel's own tables and code and sparse over pool blocks, would cost about 10 bytes in each taker and microseconds on a 286.
   - Recorded, not proposed. The threat model (the XMS door, the `AH=87h` filter, `AH=88h` answering 0) already closes the well-behaved routes.
3. **Reorganise the launch block at the next `KDL_VER` bump** (flag day, kern_mega and DOS.O88 only, not `PKG_FMT`).
   - The header has no spare byte, and the tail is a union by build: `KDL_HOFF`/`HWDS`/`HSUM` share bytes with `KDL_XBASE`/`XKB` (kdlaunch.inc:300-321).
   - Every new field so far has been fitted into a reserve that happened to exist (`KDL_RSVD`, `KDL_HBPAD`, `KDL_POUT`, `KDL_MRSV`).
   - When the next field arrives: a fixed header with 16 reserved bytes, the body, and a per-kernel tail, with the kernel tail's identity in the flags.
4. **Leave OS/2's no-copy model alone.**
   - OS/2 1.x never copied anything: its DOS box owned a fixed low region and the kernel lived above it, so a switch was a mode switch.
   - kern_mega cannot do that while its kernel and heap are conventional (`MEGA_RESIDENT` 68KB), and the runs park gets nearly the same result (the program gets ~621KB, against ~500KB in OS/2 1.x's box) for a copy of only what is used.
   - This is recorded so nobody re-derives it as an improvement.

## Prior art

| subsystem | them | os8088 | what to take |
|---|---|---|---|
| Handing the machine to DOS and back | **OS/2 1.x's DOS box**: the 286 reset through the 8042 or a triple fault, real mode at a fixed low region, the kernel above 1MB, the box suspended in place | `mega_r286`'s shutdown-0Ah reset, the same trick. The kernel's in-use runs are copied out instead of being permanently fenced off, so the program gets 621KB, not ~500KB | nothing structural. OS/2's reset path had a CMOS-shutdown-code ledger; `mega_wrprobe` is the same idea for the warm reset |
| Swapping the OS out | **DOS 5 Task Swapper** (DOSSHELL), and **Windows 3.0 standard mode**'s WinOldAp swapping the shell and programs to XMS or disk; GEOS Ensemble's exit to DOS with state files | the park is a swap to extended memory with an **integrity sum** neither of them had | the **Task Switcher API** (int 2Fh AX=4Bxxh): programs and TSRs register for switch notifications and may veto. os8088's driver sweep (`hb_drvmask`) is the kernel half of the same protocol, ad hoc. If more drivers grow trip-sensitivity, a registered "parking/unparked" callback, as on Mac System 7's Shutdown Manager (`ShutDwnInstall`), is cheaper than more `hb_drvmask` special cases |
| DOS=HIGH | **MS-DOS 5**: the kernel in the HMA, low stubs that ensure A20 at every entry, LOADFIX for EXEPACK | kdhma.inc is exactly this, with a better stack discipline (one word on the caller's stack) | MS-DOS 5 also **patched known EXEPACK stubs in the EXEC loader** rather than unpacking. os8088's `kd_xunp` (about 800 bytes in the HMA) unpacks any variant it can validate and keeps the 621KB arena. Keep it, but note that `kd_tolow` now covers the refused case with the program's own unpacker |
| Extended memory ownership | **HIMEM.SYS**: XMS is the only legal door, and INT 15h AH=88h answers 0 once HIMEM owns the memory | `kdx_i15` does exactly this, plus an `AH=87h` range filter, which HIMEM never had | nothing. os8088 is stricter |
| Snapshot integrity | none of DOS, Windows or OS/2 checked a swapped image | the sparse Fletcher over the park and the high kernel, checked by an independent stub | see Rethink 2 for density |
| Suspend/resume structure | Unix `setjmp`/`longjmp`, and Mac's sleep queue | "returns twice", the same on kern_big and kern_mega | keep |

## Doc/code mismatches

1. **kernel/megapark.inc:19**. Current: `cells (mega_hbgof)`. Code: no `mega_hbgof` exists. The resume goes through `megares.inc`'s stub and tail (`mega_hbgor`, megados.inc:1763; `mrt_wake`, megares.inc:579) and never enters this stub. Proposed: "...from somewhere the header is NOT - kd_unpark's launch block, below 1MB - so a header a stray write shrank cannot restore too little and still agree with itself (KERN-MEGA-DOS-PLAN 10.4). W7's probe, which after a reset has nothing else, passes the header's own, which is what `.cells0`'s second check is for. (A hibernate's resume does not come through here: kernel/megares.inc.)"
2. **kernel/megapark.inc:155**. Current: `a hibernate's COPIES, before the restore (W5; 0 on a park)`. Code: on a park `MPK_NCPY` is `MPK_PCPY` (4: the banked table bytes), plus one per run past the first, plus the HMA save (megados.inc:902-1008). Proposed: "--- THE COPIES, before the restore: the banked table bytes back into the kernel's tables, a runs park's later runs out of the image, and the HMA's 64KB (MC4)".
3. **kernel/megados.inc:235**. Current: `...and MPK_NCPY (0 on a park)`. Code: as item 2. Proposed: "...and MPK_NCPY: the bank's MPK_PCPY, a runs park's later runs and the HMA save".
4. **kernel/megados.inc:61**. Current: `mega_hbwake for a hibernate (W5)`. Code: `mega_dhcfill`'s only caller passes `mega_dwake` (megados.inc:1363). Proposed: "dd where it jumps then: mega_dwake (the hibernate route that wrote mega_hbwake here is megares.inc's tail now)".
5. **kernel/megados.inc:805**. Current: `(a hibernate's resume sets it)`. Code: the park's own `.sv`, `.rs` and `.nohcp` loops count it up (megados.inc:934, 995 and 1008). The resume reuses the cell for its own run list (hiber.inc:4003) and never builds a park header. Proposed: "(counted up by the copies below; hbm_xres reuses the cell for its own run list)".
6. **kernel/megados.inc:19** and **SPEC.md:187809**. Current: `the IVT's tick and keyboard back to the BIOS's` and `The IVT's tick and keyboard go back to the BIOS's`. Code: `mega_ivtrom` restores int 08h, 09h **and 15h** (megados.inc:1735-1741; SPEC.md:15934's table already says so). Proposed: "the IVT's tick, keyboard and int 15h back to the BIOS's".
7. **SPEC.md:187819**. Current: `its GDTR and IDTR cells against the build's constants`. Code: `mpk_tabs`' bases are written by `mega_dstubup` from this boot's `mega_gdtr`/`mega_idtr` (megados.inc:1505-1513; megapark.inc:381-386), so the compare is a header-integrity check, not a build check. Proposed: "its GDTR and IDTR cells against the copy staged with the stub, its wake against the legal one".
8. **SPEC.md:158914-158929** (87.9). Current: `**The resume** (`hbm_xres`) reads the WHOLE FILE`. The paragraph then names `mega_hbgof` and "Neither CPU leaves protected mode". Code: that route is retired. `hbm_xres` stages megares.inc and leaves protected mode by `mega_hbgor` (megados.inc:1763-1776). The next paragraph says the streaming resume replaces it, but this one still reads as current and names a routine that does not exist. Proposed: open it with "**Until KERN-MEGA-MEMORY-PLAN MC4.1 (b) the resume** read the whole file..." and put it in the past tense, or move it to docs/history.
9. **kerndos/kdlayout.inc:206**. Current: `MIRRORED in kernel/hiber.inc, and tests/unit/t_mirror.py is the guard.` Code: t_mirror.py scans no `kerndos/` file (t_mirror.py:89-178, 343-344). Proposed, until md-5 lands: "MIRRORED in kernel/hiber.inc. NOT yet watched: t_mirror.py does not scan kerndos/". After md-5, the sentence becomes true.
10. **kernel/hiber.inc:150** and **kernel/hiber.inc:274**. Current: `tests/unit/t_mirror.py is what keeps them honest` and `MIRRORED in kerndos/kdlayout.inc, and tests/unit/t_mirror.py is the guard.`. Code: as item 9. Proposed: the same wording as item 9.
11. **kernel/megados.inc:35**. Current: `MIRRORED (kerndos/kdlaunch.inc)`. Code: as item 9. kdlaunch.inc is unwatched. Proposed: "MIRRORED (kerndos/kdlaunch.inc; t_mirror.py, once it scans kerndos/)".
12. **kerndos/kdlaunch.inc:262**. Current: `kern_dos's arena must end below it`. Code: no kern_dos file reads `KDL_PLOW`; the arena is bounded by `KDL_CAP` (kdentry.inc:196), which `mega_dosgo` lowers in the same branch (megados.inc:1326-1328). Proposed: "written by the kernel and read by nothing: KDL_CAP, lowered with it, is what bounds the arena".
13. **kerndos/kdkbd.inc:180**. Current: `and nobits is not zeroed by anything here`. Code: `kd_entry` zeroes all of `.bss` except the launch block (kdentry.inc:56-67). Proposed: "kd_entry's clear makes it 0 before kd_kbd_start writes both halves".
14. **kerndos/kdentry.inc:771**. Current: `**THERE IS NOTHING TO GO BACK TO ON THIS ARM**`. The paragraph says the confirmation "is W7 and is not built" and that W6's return "is where the return stub goes". Line 828 says `W6: the return. Until it exists, a block`. Code: `kd_leave` returns through `kd_unpark` (a park), then `kd_resume` (the live resume), then `int 19h` with the exit code in the BDA (kdentry.inc:806-850). SPEC.md 96.42 is the floppy arm's confirmation, and it is built. Proposed: "kd_leave - the program is over: home by the park (kd_unpark), by the live resume (kd_resume, SPEC.md 96.49), or by int 19h with the exit code in the BDA (96.41), in that order. Only a block with neither KDLF_REBOOT nor a park reaches `.hold`".
15. **kerndos/kdlayout.inc:135**. Current: `it is the HMA's, which holds 63`. The sentence is truncated: the next line starts a new paragraph. Proposed: "it is the HMA's, which holds 63KB".
16. **kerndos/kdlayout.inc:4**. Current: `kern_dos sits at the bottom of conventional memory`. Code: on kern_mega's default arm the image is at FFFF:0010 (kdlayout.inc:21-35). Proposed: "kern_dos sits at the bottom of conventional memory (on kern_mega, in the HMA, with only its low rungs here)".
17. **kerndos/kdshim.inc:373**. Current: `recent claim actually returns room`. Code: `mem_free_x` returns no room on either path (kdshim.inc:442-447); only `kd_shed` gives room back. Proposed: "mem_free_x returns nothing at all: room comes back only through kd_shed's ladder, which is all a stack of two caches needs".
18. **kerndos/kdshim.inc:278**. Current: `the BIOS tick the retry paths time out against`. Code: nothing in kern_dos advances `ticks`; `fdd_wait`, its timeout reader, is outside `KD_BUILD` (disk.inc:11166-11180). Proposed: "[ticks] is a word the disk layer names and nothing here advances: no KD_BUILD path times out against it".
19. **kernel/hiber.inc:145**. Current: `at KD_SEG and is 46KB`. Code: the images are 32,894 bytes (kern_big), 37,244 (HMA) and 35,342 (low) in build/, and the HMA one is not at KD_SEG's low address at all. Proposed: "at KD_SEG and is under 40KB (kdlayout.inc's KD_IMG_KB; on the HMA arm it is not down here at all)".
20. **kerndos/kdmouse.inc:25**. Current: `(`KD_IMG_KB` is 33 since apps`. Code: `KD_IMG_KB` is 34 on kern_big, 39 on the HMA arm and 37 on the low arm (kdlayout.inc:116, 182 and 190). Proposed: "(kerndos/kdlayout.inc carries the current KD_IMG_KB and its measurement.)".
21. **docs/plans/KERN-DOS-PLAN.md:664**. Current: `There is no second stub, no second extent walk`. Code: `kerndos/kdresume.inc` is that second stub and walk (SPEC.md 96.49). kd_leave tries it before `int 19h`. Proposed: a lead line on section 8: "**Superseded in part by SPEC.md 96.49**: kd_resume puts the image back directly when it can, and this route is its fallback".
22. **docs/plans/KERN-DOS-PLAN.md:728**. Current: `So `kd_leave` writes `'DX'`, the code and a checksum there`. Code: `kd_bda` writes `'KDX1'` (`KDB_SIG`), the whole code word and the arena KB, and no checksum (kdentry.inc:921-941). Proposed: "So `kd_bda` writes `'KDX1'`, the code word (its high byte the refusal reason, 96.40.7) and the arena in KB (96.41.1) there".

## Open questions

1. md-6: does the low image actually drop to 36KB? It is one build of `kerndosl.bin` with the gate in. That is the measurement that decides whether low-DOS programs get the kilobyte.
2. md-3/md-10: which field machines are 8042-only for A20? The Victory 286 is the obvious candidate (docs/FIELD-MACHINES.md). The 86Box `mr286` profile could confirm the W7 race with `tools/os88box.py`.
3. md-2: is a direct `int 15h AH=87h` from a DOS program under kern_mega common enough to matter? The filter lets an in-arena move through to the ROM, which then leaves A20 shut. No program that does this is known from the field.
4. Rethink 1 versus md-15: does the owner want the header verified at all, given that the threat (a program writing above 1MB outside the arena) is already closed off for every well-behaved route?

## Verification

Adversarial pass of 2026-11-08. Every bug, hazard and medium finding was
re-read in the source, with the other build arms; the low kern_dos image was
re-assembled into a scratch directory (byte-identical to
`build/megak/kerndosl.bin`) to re-measure md-6. Nothing in the tree was built
or booted. None of the findings is in `docs/plans/ABI-BREAK-LIST.md` or
`docs/plans/KERNEL-BYTE-QUEUE.md`.

- md-1 CONFIRMED - only `mega_dwake` is ever written to `MPK_C_WAKE` (megados.inc:1363), so `.cells0` is dead; bytes refined to -12 HIBER.DRV.
- md-2 CORRECTED - the defect is real (kdh_tramp runs after kd_mou_arm, kd_mou_rearm then drops the slot), but `dos_hvec` is `DOS_MEGA`-only, so the fix must be gated (`KD_HMA` / `KD_MEGA`) to leave kern_big's kern_dos unchanged.
- md-3 CONFIRMED - one A20 test after an IBF-only wait, with the anchor already cleared.
- md-4 CORRECTED - real path, severity medium -> low: it needs >16KB of new claims between claim and snapshot and an in-use count low enough for the stage to reach the packed stream at 64KB.
- md-5 CONFIRMED - t_mirror's set is its list plus `kernel/*.inc`; no other test compares the kerndos/ mirrors.
- md-6 CORRECTED - measured, not estimated: 357 + 189 + 459 (hbs_stub) bytes of `.text` and 23 of `.bss`, about -1,025 per kern_mega image; KD_IMG_KB 37 -> 36 on the low arm is predicted to hold.
- md-7 CONFIRMED - `kd_glb` is used only under `KD_GATE`.
- md-8 CONFIRMED - nothing reads `KDL_PLOW`.
- md-9 CONFIRMED - narrow (IF = 0, microseconds), low.
- md-10 CONFIRMED.
- md-11 CONFIRMED.
- md-12 CONFIRMED - the bound holds today (10 fixed regions plus MX_NBLK - 1).
- md-13 CONFIRMED (spot-checked).
- md-14 CONFIRMED - -8 bytes.
- md-15 unchecked.
- md-16 CONFIRMED - latent.
- md-17 CORRECTED - below 33KB the loop does not restore 1KB short; CX wraps and the stub overruns P or spins. Still unreachable, still `record`.
- md-18 unchecked.
- md-19 CORRECTED - 26 bytes, not 30, and `mega_hbwk` must stay in `.text` (it has to be in the image that `mega_hbwake` reads it from), so -22.
- md-20 CONFIRMED.
- md-21 unchecked.
- md-22 CONFIRMED (spot-checked: the truncated sentence and the 61 -> 43 -> 34 history).
- md-23 unchecked.
- md-24 unchecked.
- md-25 unchecked.

Doc mismatches: all 24 confirmed against the exact text, with the
replacements refined (SPEC.md 96.40.9's stub sentence keeps "the two legal
ones", which is what the code still accepts until md-1 lands; the SPEC.md
87.9 paragraph is marked retired rather than rewritten). One more was added
from the same block: `kd_leave`'s `jz .hold` comment ("W6: the return. Until
it exists"), so there are 25 doc fixes in `data/megados.json`. None refuted.
