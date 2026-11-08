# kern_mega: the protected-mode kernel core

Reviewer key: `megacore`. Tree: `elendilon` at `5e4740b`. Read-only review; nothing built or run.

## Scope read

| file | lines read | what |
|---|---|---|
| `kernel/mega.inc` (15,103 lines) | 1-7424, 8570-11700, 11700-12000, 12740-13210, 14044-14160, 14279-14460, 14808-15103 (about 11,600 lines, every proc in them) | the switch, bootstrap GDT, IDT builder, the IRQ reflection and both ways back, the capture/replay stubs, `mega_t08`/`mega_k09`/`mega_i16`, the 8042 watchdog, NMI, the fault reporter, `mega_fkill` and the catch frames, `mega_gpfix`/`mega_gpw`/trampoline/`mega_null0`/`mega_db`, the 286 string-restart calibration, the native floppy, the pool (`mx_*`), the LDT allocator, compaction, the claim family, `OSAPI_SEG`'s dispatch, `mega_rseg`/`mega_useg`, `mega_mxtinit`, `mega_pmtail`, the `.bss`/`.lowbss` state |
| `kernel/mega.inc`, skimmed only | 7424-8569 (the `MEGA_DIAG` floppy strip), 10330-10500, 12000-12740, 13210-14044, 14160-14279, 14460-14808 | diag-only code, the walk, the image/region/SUB family and the window table. Findings below do not cover these spans |
| `kernel/kernel.asm` | 380-860 (the kern_mega macros: `KDS`, `KDATAR_C`, `MEGA_SEG0`, `VSEG`, `CSDO*`, `MEGA_ICW*`, `MEGA_CSELTOP`, `MEGA_PCALL*`), 8897 | the KERN_MEGA arms every module pairs with |
| `apps/win16/win16.asm` | 1365-1420 (`w16_lend`) | the one `SEG_LEND` caller |
| `SPEC.md` | 20.18, 20.18.1, 20.18.2, 50.6.11's table, 124.31.2 item 3, 124.31.7 | the contracts |
| `docs/BOCHS.md` | 1-60, 580-610 | the instrument |
| `build/megak/kernel.map` | `.lowbss` symbols | `mega_pm` at `.lowbss` 1347h |
| `docs/plans/ABI-BREAK-LIST.md` L1-L5, T2, T2-2; `docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md`'s kern_mega rows; `docs/plans/KERN-MEGA-PLAN.md` 14.38 D6, 14.140, 14.165 | | so as not to re-propose |

The other `%ifdef KERN_MEGA` arms (`memory.inc` 58 sites, `hiber.inc` 41, `disk.inc` 40, `diskw.inc` 30) belong to the memory, disk and hibernate reviewers. I followed them only where a `mega.inc` routine calls them.

## Verdict

kern_mega's core is a careful piece of engineering. Its model is the right one: a 286 extender that never goes back to real mode on a per-event path. The tick, the keyboard, int 16h and the floppy are native. Every other BIOS call is reflected, and the PIC stays at its protected-mode base through the reflection. Most of the fragile invariants are build-time `%error`s rather than prose, and the fault model (catch frames, closing a package, refusing to loop) is better than Windows 3.0's UAE.

The weakest part is the **"which segment register holds kernel state" rule**. SPEC.md 20.18.2 fixed four sites, but the reflection path still reads `.lowbss` through SS after `mega_svc_lend` has moved SS to a 1 KB lent stack. That is a real fault on any limit-checking CPU (mc-1).

The second theme is how long interrupts stay off: linear LDT scans and 64 KB compaction pieces. These are fine on a 386 and marginal on the 286 that is the arm's reason to exist (mc-4, mc-5).

The third is a set of machine-global GDT scratch selectors whose correctness is held by prose (mc-9).

The file's own header still describes the retired design (the static identity GDT and LDT aliases), and so do BOCHS.md and the `MEGA_CSELTOP` macro. That is the biggest doc debt.

Almost everything recommended here is in `.mega` or `.cold`, which live above 1 MB after the switch, so the conventional-memory cost is nil.

## Strengths

1. **The PIC never moves for a reflection** (`mega.inc:3407-3418`, `mega_pmtail` 14877-14886). IVT 08h-0Fh is copied to 20h-27h, so real mode finds the BIOS handlers at the protected-mode base. Re-initialising the 8259 is ICW1, which resets the edge latches and lost the serial mouse. Avoiding it entirely is cheaper and more correct than DOSX's or OS/2 1.x's re-programming per switch. The same idea, mirroring a table rather than re-initialising hardware, would suit any future mode bracket.
2. **No per-event path resets a 286.** `mega_t08` (1356), `mega_k09` (1640), `mega_i16` (2007) and the native floppy (`mega_fd13`, 6625) cover the tick, keys and A:/B:. The floppy is explicitly an *accelerator*: every unknown case falls back to the BIOS, and `mega_fdjudge` (6875) charges a fallback against the native path only when the BIOS then succeeds. That is a model worth copying for any native driver.
3. **A fault closes the package and only that package** (`mega_fkill` 2540, `mega_pcin_*` 3038, `mega_fkivt` 3113):
   - the catch frame is per task;
   - the attribution is by pair, then by owning block;
   - the catch must call into the same instance (14.140), so one program cannot get another closed;
   - `MEGA_FKCAS`/`MEGA_FKGAP` stop loops;
   - the IVT cells the program held are given back.

   This is Windows 3.1's `Catch`/`Throw` plus ToolHelp's `InterruptRegister`, done by the kernel and without asking the program.
4. **The 286 string-restart behaviour is measured, not assumed** (`mega_strcal` 5100, `mega_strfix` 5195). It is calibrated once at the switch on the actual CPU, and the 86Box CX-by-two quirk is absorbed by the measurement rather than by a special case.
5. **Code/data pairs are adjacent, with data = code + 8** (`mega_cs2ds` 4804, `mega_ldt_alloc2` 9882). Converting between them is a subtraction rather than a table, and `mega_ldt2ds` (9932) validates the twin before trusting it. Windows needs separate `AllocCStoDSAlias` selectors for the same job.
6. **Layout invariants are `%error`s.** Examples: the scratch line placement (108-110), stub strides (888, 1036, 1139, 2102, 2127), `MEGA_MXT_SIZE` against `MX_OWN` reads (8801), `MEGA_FKINST` mirrored and checked in `kernel.asm:8897`, and `MEGA_MSELN` checked in `memory.inc:127`.
7. **`mega_layoutf` is a pure function of `[mega_extkb]`** (10888). Every boot of a machine lays the tables out identically, which is what makes a park's anchor and a hibernate resume findable.
8. **The lost-edge watchdog** (`mega_kwd` 1437) re-delivers a stranded 8042 byte through its own gate, rather than dropping it or polling. It costs one `IN` on the common tick.
9. **Conventional bytes are treated as the scarce resource.** `.mega`, the tables' tail (`MEGA_SEL_MXT`) and the refusal strings live above 1 MB. The doors between `.text` and `.mega` are a closed list that `os88ovlchk` enforces (14925-15032).

## Findings

### mc-1 (bug, HIGH, now): a reflection on the LENT stack reads `.lowbss` through SS

`mega_svc_lend` (3463-3488) switches SS to the 1 KB lent claim, `mov ss, ax` with `[mega_rsseg]`, and jumps to `mega_svc.low`. Everything the reflection does from there runs with SS equal to that claim's data selector. The claim is 1 KB (`win16.asm:1406-1414`, `AX = 1`), so the descriptor's limit is 3FFh (`mega_mpbase` 14062-14073). Three routines on that path still read kernel state through SS:

- **`mega_s2segc`** (5457-5458: `cmp byte [ss:mega_pm], 0`) is called unconditionally at `.refl2` and `.esok` (3359, 3381) and from `mega_vrm` (5804). `mega_pm` is at `.lowbss` offset 1347h (`build/megak/kernel.map`), past the 3FFh limit. On Bochs, 86Box or a real 286/386 that is **#SS** in `.cold` code. `mega_fkill` treats a GDT CS as the kernel's, so the result is **the fatal halt**. QEMU checks no limit; there it reads a heap byte past the claim, and when that byte is 0 the BIOS is handed a selector *as a segment*.
- **`mega_rseg` → `mega_p2d`** (3826; 5425: `cmp [ss:si+MC_SEG], dx`) reads `mem_tab` through SS whenever the BIOS hands back a changed ES or DS: int 10h AX=1130h, int 13h AH=08h, int 15h AH=C0h.
- **`mega_hibnc`** (5825: `mov ax, ss` "...or LOW_SEG's dsk_secbuf") lands the bounce in the lent claim at offset `dsk_secbuf` when the read-ahead landing is shed.

This is SPEC.md 20.18.2's rule ("never reads kernel state through SS where a program's stack may be"), missed on the one path that *makes* SS foreign on purpose. The motivating program in 124.31.2 item 3 (Mozart's `int 10h AX=1A00h`) goes exactly this way.

Recommendation:
- `mega_s2segc` (and its `.cold` siblings `mega_slidec`, `mega_kaimf`, `mega_oddsegc`, `mega_bkaimf`, `mega_backf`) read `mega_pm` through ES = LOW_SEG by name, as `mega_codesel` does. Alternatively, give the protected-mode-only callers a PM entry that skips the test.
- `mega_p2d` reads `mem_tab` through ES = LOW_SEG.
- `mega_hibnc` uses `mov ax, LOW_SEG`.
- Add a Bochs row: a probe package that lends a 1 KB stack and issues `int 10h AX=1A00h` and `int 10h AX=1130h` from a stack of its own.
- Consider a fast-tier gate that lists every `[ss:` non-`bp` operand in routines reachable from `mega_svc`.

Bytes: about +16 `.cold`/`.mega`, all above 1 MB after the switch. 0 conventional.

### mc-2 (hazard, medium, now): an option-ROM IRQ on an unlent foreign stack is reflected with a selector as the real-mode SS

In `mega_svc .none` (3290-3296), an IRQ the ROM owns (vector high byte 1) arriving with SS neither LOW_SEG nor the lent stack goes to `.nest` → `.low`. Then `[mega_rss] = SS` (3448-3456), and `mega_svc_lo.rmc` (3576) loads that selector *as a segment*. The ROM's ISR pushes its frame at physical selector × 16, which is somebody's heap.

The comment says "an IRQ the ROM owns goes on as before". "As before" is the bug that `SEG_LEND` was invented for (124.31.2 item 3). It is only out of reach because WIN16 re-lends before every run; any other program that keeps its own stack meets it, on a PicoMEM above all.

Recommendation, cheapest first:
- **(a)** For the IRQ case with no lend, run the reflection on a small fixed `.lowbss` stack that the kernel owns. 128-192 bytes is enough for a ROM ISR plus the capture stubs.
- **(b)** Mask that line and replay it at the next LOW_SEG reflection.
- **(c)** At the least, make the contract explicit: a package that runs on a stack of its own must `SEG_LEND`, or option-ROM IRQs corrupt memory. Put that in SPEC 50.6.11's `SEG_LEND` row and fix the comment.

Bytes: (a) about +20 `.mega` and +128..192 `.lowbss`, which is resident conventional and so the owner's call; (c) 0.

### mc-3 (bug, low, now): `SEG_LEND` clobbers the live loan's paragraph before validating

`mega_segf .lset` (12779-12784) writes `[mega_rspar]` *before* `mega_p2d` can refuse. A refused lend while another loan is live leaves `mega_rsseg` naming the old claim's selector and `mega_rspar` naming the refused paragraph. The next reflection then runs protected mode on one stack and real mode on another, and the BIOS pushes into the wrong memory. SPEC 50.6.11 also says "a claim of the caller's", but nothing checks the owner, so a caller can lend another package's claim.

Recommendation: store `rspar` after the `jc .lno`, keeping the paragraph in a register. Optionally compare `mem_tab`'s `MC_OWN` with `mega_segown`'s byte.

Bytes: 0..+12 `.mega`.

### mc-4 (hazard, medium, now): linear LDT walks run with interrupts off, once per block on a teardown

- `mega_ldt_alloc` and `_alloc2` (9846-9930) scan from `MEGA_LDT_FREE0` to `mega_ldte` with IF = 0.
- `mx_dfree` (9413-9478) walks the **whole LDT** for every program or driver block freed.
- `mx_free` (9251-9294) and `mx_relall` (9295-9335) run `mx_kill → mx_unsel → mx_dfree` inside their own `pushf/cli`.

`[mega_ldtn]` is `FREE0 + 512 + extKB/16` (10903-10910), so about 1,500 allocator entries on a 15 MB machine. Predicted, not measured: about 26 clocks an entry on a 286 (`cmp byte [es:di+5],0` / `je` / `add` / `cmp di,[ss:]` / `jb`), which is about 5 ms a walk at 8 MHz. An instance teardown with N pool blocks is N such walks back to back with IF off. A WIN16 program with dozens of `GlobalAlloc` blocks reaches hundreds of milliseconds. An 8250 or 16450 at 1200 baud overruns after 8.3 ms, and the sample ISR of SPEC 34.11 starves.

Recommendation:
- **(a)** Keep a high-water mark `[mega_ldthi]`, raised by both allocators, and end every walk there instead of at `mega_ldte`. The top of the LDT is almost always empty.
- **(b)** Move `mx_dfree` out of `mx_free`'s and `mx_relall`'s `cli`. Each `mega_ldt_free` already brackets its own descriptor write, and only the `mx_tab` row update needs the cli.
- **(c)** Give `mega_ldt_alloc` a rotating start hint.

Bytes: about +20..+30 `.mega`.

Prior art: Windows 3.x and every DPMI host chain free LDT entries through the descriptors themselves. A free entry's access byte is 0, so 6 bytes of payload are available and allocation is O(1). The pairs need two adjacent entries, so the free list would serve lone selectors and the scan would remain for pairs.

### mc-5 (hazard, medium, record): compaction moves 64 KB pieces with interrupts off

`mx_cpmove` (11055-11190) copies each piece and rebases its descriptors in one `pushf/cli` … `popf`. `mx_pcut` (11556) cuts at 64 KB, because any claim's own 64 KB selector spans its piece. KERN-MEGA-PLAN 14.38 D6 measured 0.257 ms a KB above 1 MB on the Victory 286, so **a 64 KB piece is about 16.4 ms with IF off**, plus the `mx_desc` walk (mc-4). That is two 1200-baud serial bytes. `mx_cpflags` holds the rate hook for FH32 claims, but nothing saves the UART.

`mx_alloc` (8957-9100) also holds `[sch_lock]` across a full `mx_packupf`. At the same 0.257 ms/KB, packing 2 MB is about 0.5 s with no task switch (interrupts on).

Recommendation: record it. The real fix is a model change. Windows 3.x moved MOVABLE segments with interrupts on, and code that touched memory at interrupt time had to `GlobalFix` or `GlobalPageLock` it first. os8088 assumes any ISR may read any block, so it moves with IF off. A per-claim "ISR-reachable" flag would let every other block move with interrupts on and cut only at descriptor boundaries. `MX_CPHOLD` and SPEC 34.11's sample-ISR exception are already half of that flag. Declaring it is a contract on packages (flag day). Until then, quote the 16 ms figure wherever the pool's compaction is described.

Bytes: small (+20 `.mega`) once the contract exists.

### mc-6 (bug, low, now): spurious IRQ7/IRQ15 gets a non-specific EOI

When IRQ7 or IRQ15 is the ROM's, its gate is `mega_eoi1` or `mega_eoi2` (`mega_idt_gate .romk`/`.irqm`, 840-852; 2136-2150). These send a **non-specific** EOI. For a spurious IRQ7, which an AT raises routinely and which `mega_capon`'s own comment acknowledges (1095-1097), no ISR bit is set. The EOI then retires whatever interrupt is in service at that moment, which matters wherever an ISR has `sti`'d before its EOI. The AT BIOS's `D11` handler reads the ISR first for exactly this reason.

Recommendation: give 27h and 77h a stub that reads OCW3 0Bh and skips the EOI (for 77h, the slave's EOI) when bit 7 is clear.

Bytes: about +16 `.mega`.

### mc-7 (bug, low, now): `mega_rfclk` breaks at midnight

`mega_rfclk` (3892-3934) takes the periods as `[40:6C] - [mega_t0]`, low words only. If a reflection spans midnight, the ROM's handler has reset 40:6C/6E to 0, so AX becomes about 65,360: `[ticks]` jumps an hour, and the rate arm's `.fix` subtracts that from the time of day. The correct difference is `new - t0 + 0x1800B0`, and since 180000h ≡ 0 mod 65536 the correction is `+ 0x00B0` when the subtraction went negative. `mega_t08` handles midnight; this path does not.

Recommendation: `jns` over an `add ax, 0xB0` once the result is ≥ 8000h (no reflection is 30 minutes long).

Bytes: about +6 `.mega`.

### mc-8 (bug, low, now): `mega_k09` keypad and shift fidelity

- **Keypad - and + type no ASCII with NumLock off.** The `.pad` arm (1837-1858) treats 4Ah and 4Eh as navigation keys, so they store 4A00h and 4E00h. The AT BIOS gives 4A2Dh and 4E2Bh in every state.
- **40:96's right Ctrl and right Alt bits (2, 3) are never maintained**, and int 16h AH=12h returns AH = 40:18 alone (2069-2073). The ROM composes AH from 40:18 and 40:96.
- **A plain Ctrl+ScrollLock (an 83-key keyboard's Break) toggles Scroll Lock.** Only the E0 form is swallowed (1700-1703).

Recommendation: fix the keypad case (about +8 bytes). Track the right-hand bits (about +12). Leave the 83-key Break.

Bytes: about +20 `.mega`.

### mc-9 (hazard, medium, now and rethink): machine-global GDT scratch selectors held by prose; `BACK` is not rebased by the compactor

ODD, SLD, BACK, K0-K5, FD, VB, XA/XB and WIN are each **one descriptor for the whole machine**, re-aimed per use (4029-4145, 4161-4181). Their correctness rests on comments: "a caller holds MEGA_SEL_ODD only until its next call", "only a TASK loads it", "under the [sch_lock] mx_alloc holds" (4002-4006, 4175-4177, 11027-11030).

`mx_cpmove` rebases only SLD among them (11173-11179: `mov si, MEGA_SEL_SLD / lea cx, [si+8]`). BACK, which `lz_match` aims at a source 64 KB behind an output that may be a pool claim (`lz.inc:519-534`), is left at the old base. A task suspended in the middle of a match while another task's `mx_alloc` packs that block would read stale bytes. The window is small, but nothing guarantees it is never hit.

Recommendation:
- **(now)** Widen the rebase to K0 … SLD. These ascend (SCR+0 … SCR+9, 157-169), and PSCR and CODE in between are based outside the pool, so `mx_desc` leaves them alone. The change is `mov si, MEGA_SEL_K0` / `mov cx, MEGA_SEL_SLD+8`: about +1 byte.
- **(rethink)** Give each LZ stream and each module stream its own LDT selector, allocated with the stream. With mc-4's O(1) allocator that is cheap, and it retires the scratches and their prose.

Bytes: +1 now. The rethink is about −60 `.cold` of aim helpers against +40 of alloc/free calls.

### mc-10 (weakness, medium, now): kernel code's own writes through CS are completed silently

`mega_fault` sends every #GP to `mega_gpfix` and `mega_gpw` whatever CS is (2418-2427). `mega_cs2ds` maps the kernel's 0x0060 to KDATA (4818-4823). So a missed `CSDO` or `PUSH_CSDATA` in the kernel's own `.text` becomes a permanent #GP per store, with no report on a shipped build, and `.cold` with a CS: prefix reaches `mega_report` only by luck (4602). The trap exists for *packages* (SPEC 20.18; L3). For the kernel it hides a bug.

Recommendation: count fixups whose faulting CS is a GDT selector in a `[mega_kfix]` word, which the harness reads (as it reads `[mega_infault]`), and have a Bochs row assert it stays 0 across a boot and a standard workload. `MEGATRAP=1` already logs these, but nobody runs it by default.

Bytes: about +8 `.mega`.

### mc-11 (consistency, low, now): CMOS index port read back

`mega_enterf` (476-478: `in al, 0x70 / or al, 0x80`) and `mega_pmtail` (14893-14895: `in al, 0x70 / and al, 0x7F`) read-modify-write port 70h, which is write-only on the AT. Every other site writes 8Dh or 0Dh outright (`mega_up` 3693, 3719; `mega_svc_lo` 3528; `mega_r286` 3974-3978).

Recommendation: `mov al, 0x8D` / `mov al, 0x0D`.

Bytes: −2 `.cold`, −2 `.mega`.

### mc-12 (hazard, low, now): `mega_s2segc` truncates an off-grid base

`mega_s2segc` (5470-5486) shifts the base right by 4 without testing the low nibble. A `SEG_WINL` window (an arbitrary linear base, 50.6.11) handed to a reflected BIOS call in ES or DS becomes a segment up to 15 bytes low, so a BIOS transfer lands early. `SEG_REAL` refuses exactly that case (12855-12857).

Recommendation: return FFFFh for a base with a nonzero low nibble. int 13h 02h/03h then bounces through `mega_hibnc`, and other calls see "no real-mode address".

Bytes: +4 `.cold`.

### mc-13 (weakness, low, now): `mega_ustab` rows are never freed, and can go stale

`mega_useg` (14337-14385) gives a permanent row (8 of them) and LDT entry to every distinct segment that `mega_rseg` cannot map. `mega_rseg`'s `.u` arm (14316) sends *any* non-identity, non-claim-base segment there, including one **inside** a conventional claim, despite `mega_useg`'s header ("at or above A000h"). The row names a physical paragraph, so it is wrong once that claim moves. A ninth distinct segment gets the NULL selector.

Recommendation: limit `mega_useg` to ≥ A000h as documented, and return NULL for an in-claim non-base segment. Alternatively, free rows in `mega_mpgap`/`mega_cdfree` when their paragraph falls in a freed or moved range.

listed_as: CODE-REVIEW-TWO-CYCLES row 18 (still open).

Bytes: about +6 `.mega`.

### mc-14 (weakness, low, now): `MX_FWIN` is set and never cleared

`MX_FWIN` is set at 13480 and read by `mx_cpflags` (11243). Once a bound window has existed in a block, the block moves with FSXF_RATE's hook held off for ever.

Recommendation: clear it when the block's last window goes (`mega_ldt_free`'s wtab walk already finds the row).

listed_as: CODE-REVIEW-TWO-CYCLES's kern_mega list.

Bytes: about +10 `.mega`.

### mc-15 (hazard, low, now): `mega_leds` spins inside IRQ1 with interrupts off

`mega_kack` (1332-1345) polls port 64h up to 65,536 times, twice per lock-key press, inside `mega_k09` with IF = 0. That is about 0.1-0.2 s if the keyboard never ACKs.

When the waiting byte is the AUX port's, `mega_kack` returns without reading it. The keyboard's FAh for the EDh then arrives later as an ordinary IRQ1 byte, and `kbm_isr`'s map sees FAh as a scancode.

Recommendation: bound the wait to about 2,000 polls (the AT BIOS's order). Check that `kbd_track` ignores FAh/FEh; if it does not, swallow them in `mega_k09`.

Bytes: about +4 `.mega`.

### mc-16 (rethink, medium, now): move the master PIC off 20h, the DOS software vectors

With `MEGA_PIC_BASE` at 20h:
- `mega_i21` (952-1000) must probe the 8259's ISR on every keystroke to tell IRQ1 from a program's `int 21h`;
- a program's `int 20h`, `int 25h` or `int 26h` enters the IRQ0, IRQ5 or IRQ6 gate (`int 26h` sets the floppy-done flag in `mega_fdisr`);
- `mega_fkivt` must special-case 21h (3146-3153).

124.31.2 records that "moved off 20h (DOSX used 50h) ... is not taken" because it was the larger change to the reflection path. **But the reflection path is already base-agnostic.** It never re-initialises the PIC and simply copies IVT 08h-0Fh to the base (14877-14886). Moving the base to 50h moves that copy to IVT 50h-57h and leaves the rest alone. `mega_r286`'s compare and re-init (3980-4003) take the base from `mega_pic_pm`. `mega_i21`, `mega_swv`'s 21h special case and `mega_fkivt`'s 21h mirror then go, and DOS's 20h-27h become ordinary software vectors for `mega_swv`.

Recommendation: re-open the decision with that observation. Check megados.inc's `mega_dropr` and the hibernate path, which put 08h back.

Bytes: about −60 `.mega` (`mega_i21`) against +4 (two more `mega_swv` stubs).

listed_as: SPEC.md 124.31.2 item 2's refusal.

### mc-17 (hazard, low, record): `OSAPI_XMEM_COPY` writes anywhere below 1 MB

`mx_copy` (9751-9840) accepts any conventional end with DX < 10h, checking nothing. Under kern_mega every other stray write by a package is a #GP that `mega_fkill` contains; this primitive writes the IVT, the kernel at 600h or another package unchecked. XMS had the same hole (HIMEM never checked the conventional end), so this is parity rather than a regression.

Recommendation: record it. If fault containment is ever promised, check the conventional end against the caller's claims and region (`mega_fown`'s walk, about +30 bytes).

### mc-18 (weakness, low, record): the software-vector dispatch enters a handler with the dispatcher's flags

`mega_swc` (891-950) and `mega_i21` (952-1000) enter a package's handler with the arithmetic flags of `mega_swok`'s compare, not the flags the `int` instruction left. A real-mode handler sees the caller's flags in the register. That is harmless for every handler in the tree, since they read FLAGS from the frame.

Recommendation: record the flags point in SPEC 20.18.1.

Bytes: 0.

## Rethinks

1. **One allocator for LDT selectors, with a free list** (mc-4, mc-9). Make selectors cheap enough to give every stream its own, instead of re-aiming nine global scratches. This is the Windows/DPMI model. It retires the "who holds ODD now" prose and the BACK-not-rebased class of bug, and it makes `mx_dfree`'s whole-LDT walk unnecessary if each block chains its own SUB/window selectors (Windows' arena knows its handles). About ±0 bytes net in `.mega`/`.cold`; no ABI.
2. **Interrupts-on compaction with an ISR-reachable flag** (mc-5). This is the Windows 3.x `GlobalFix` contract. It needs packages to declare which claims their ISRs touch, so it goes on the flag day, beside L3. The pieces would then cut at descriptor boundaries and nothing would hold IF off for 16 ms.
3. **PIC base 50h** (mc-16): no ABI, about −56 bytes, and the int 21h ambiguity disappears.
4. **A per-image "protected-mode clean" header bit**, Windows 3.0's NE `PROTMODE` flag. It would let a package that has been converted say so. `mega_gpw` would then *close* it on a CS store (a bug), and `t_megapkg` could ratchet the census. This is the incremental road to L3 that does not wait for The Wire to serve two builds: an old binary keeps the fixups, and a clean one forbids them. It costs one class bit (ABI-visible, but additive, so a lock update without a `PKG_FMT` bump) and about +12 `.mega`.

## Prior art

| system | its model | better than os8088 | os8088 better | cheap to adopt |
|---|---|---|---|---|
| **DOS extenders (DOS/16M, Phar Lap 286)** and **Windows 3.0 standard mode's DOSX** | 286 PM with reflection of real-mode interrupts; 286 return via CMOS shutdown code and reset (DOSX used 09h/0Ah too) | they reflected IRQs to the BIOS less often by moving the PIC (DOSX: 50h), so no vector ambiguity | os8088 never re-initialises the PIC (strength 1); native keyboard, tick and floppy mean far fewer 286 resets than DOSX, which reflected every keystroke on a 286 | the 50h base (mc-16) |
| **OS/2 1.x** | its own PM device drivers for everything, with no BIOS in PM. The DOS box ran in real mode, reached by a 286 reset each way | no BIOS reflection at all: a disk transfer never resets the CPU | os8088 runs on BIOS-only hardware (option-ROM disks, PicoMEM) that OS/2 never supported, and its fallback-to-BIOS accelerator keeps that coverage | the accelerator pattern already *is* OS/2's driver model, made safe; the next candidate is the fixed disk on a 286, where every int 13h is a reset (`MEGA_E9`'s `mega_mhd13` already flags it) |
| **Windows 3.0/3.1** | per-segment selectors, CS→DS aliases, a selector free list chained through the LDT, MOVABLE vs FIXED vs page-locked memory, UAE (3.0) then parameter validation and ToolHelp fault hooks (3.1), `Catch`/`Throw` | an O(1) LDT allocator; an explicit contract for memory an ISR touches, which made interrupts-on movement possible; the PROTMODE header flag | os8088's pair adjacency is cheaper than alias selectors; its fault close is automatic and attributed (3.0's UAE left the system unstable) | the free list (rethink 1); the clean-bit (rethink 4) |
| **Mac System 5-7** | one address space, handles and a Memory Manager that compacts at allocation time with interrupts on; ISRs may touch only locked or non-relocatable blocks (`HLock`, `NewPtr`) | the same interrupt-time contract as Windows: compaction never needs IF off | os8088's selectors make relocation invisible to the holder (no double-dereference discipline, no `HLock` bugs) | the contract (rethink 2) |
| **GEOS (8088)** | real mode only, handles plus explicit locking, no protection | nothing in this area: no protected mode | | |
| **AmigaOS** | no protection, `Forbid()`/`Disable()` | `Forbid` (no task switch, interrupts on) versus `Disable` (interrupts off) is exactly the distinction os8088 draws with `sch_lock` versus `cli`; os8088 uses it well in `mx_alloc` and less well in `mx_free`/`mx_relall` (mc-4) | | the habit: take `Forbid`, not `Disable`, for table walks |

## Doc/code mismatches

| file:line | current text (exact) | what the code does | proposed |
|---|---|---|---|
| `kernel/mega.inc:13` | "GDT entry i, for every i from 8 to 8191, is DATA with base i x 128 and a" | GDT is identity only up to `COLD_SEG` (`MEGA_GDT_ID`, 94); claims, regions, images, frame buffers and ROMs are LDT selectors (the second header block, 78-85; `.gdt` loop 541-560) | replace THE MAP paragraph with: "THE MAP (KERN-MEGA-MEMORY-PLAN MC7.4). GDT entry S/8 is DATA with base S x 16 for every segment the kernel itself loads below `.cold` (MEGA_GDT_ID); KERNEL_SEG's and COLD_SEG's entries are CODE. Past them are the per-use scratches. Every claim, region, image, frame buffer and ROM is an LDT selector, a code/data PAIR (data = code + 8) for an image." |
| `kernel/mega.inc:24` | "LDT entry i, for i from 0 to 2047, is READABLE CODE with base i x 512, so" | the LDT sits on the GDT (same base) and its entries past `MEGA_LDT_FREE0` are an allocator (`mega_ldt_alloc`, 9846); the static aliases were retired by MC7 step 6 | delete the paragraph; the MAP replacement above covers it |
| `kernel/mega.inc:29` | "Both tables and the IDT live above 1MB (MEGA_*_LIN), which is why this is" | the block is placed by `mega_layoutf` at the top of extended memory or claimed in conventional memory (`mega_enterf` 393-413); no `MEGA_*_LIN` symbol exists | "The tables' block lives at the top of extended memory (mega_layoutf), or in a conventional claim on a machine too small for that, which is why this is kern_big's and why xm_sniff keeps XMEM.DRV out on this build: kern_mega owns the memory up there." |
| `kernel/mega.inc:8` | "It is wave M1 of KERN-MEGA-PLAN 10." | the file holds M1-M6 and the MC/MC7 memory refactor | "Its history is KERN-MEGA-PLAN 10 and KERN-MEGA-MEMORY-PLAN." |
| `kernel/mega.inc:58` | "SS:SP and the registers to QEMU's debug console (port E9h) and stops. In" | E9 only on a `MEGA_E9` build; #GP and #DB go first to `mega_gpfix`/`mega_gpw`/`mega_null0`/`mega_db` (2418-2427); a package's fault closes the package (`mega_fkill`, 2540); only the kernel's halts, with a glass line under the menu bar (2470-2538) | "...ends in mega_fault. A #GP that is a package's far transfer through a data segment, a write through a code segment or a segment-0 access is completed (mega_gpfix, mega_gpw, mega_null0). A package's own fault closes that package (mega_fkill). Anything else is reported on the glass line under the menu bar (and on port E9h in a MEGA_E9 build) and stops." |
| `kernel/mega.inc:844` | "70h-77h for themselves, 10 bytes apart" | `mov ah, 6 / mul ah` (849-851); `%error "mega_irv is six bytes a stub"` (1037) | "70h-77h for themselves, 6 bytes apart" |
| `kernel/mega.inc:1428` | "this kernel always (mega_ivt_sync leaves 20h/21h alone), IRQ12's is whoever" | `mega_ivt_sync` writes gate 21h = `mega_i21` on every run (4987-4989); IRQ1 still reaches `kbm_isr` through it because the `int` comes from a GDT CS (`mega_i21` 966-971) | "this kernel always (gate 21h is mega_i21, which sends an int from a GDT CS to kbm_isr), IRQ12's is whoever" |
| `kernel/mega.inc:2405` | "; mega_fault - report and stop" | dispatches to the #GP and #DB fixups, closes a faulting package, or reports and halts | "; mega_fault - every exception: fixed up (#GP, #DB), the package closed (mega_fkill), or reported and stopped" |
| `kernel/mega.inc:3250-3253` | "an LDT code alias is" / "is taken as the identity selector it now is." | `mega_s2segc` reads any selector's descriptor (5457-5490); a changed register comes back through `mega_rseg`: identity below COLD_SEG, a claim's base its pair's data half, else a `mega_useg` row, else NULL (14299-14336) | "...LOW0 is 0, and any other selector is its descriptor's base (mega_s2segc; FFFFh above 1MB). Coming back, a register the BIOS left alone is given its own selector back, and one it changed is made a selector by mega_rseg." |
| `kernel/mega.inc:3372` | "; .cold: the segment has no room for it" | `mega_hibnc` is in `section .mega` (opened at 5711, before `mega_rmaskf`), called through `MEGA_SEL_CODE` | "; .mega: the segment has no room for it" |
| `kernel/mega.inc:5680` | "FAR: it is .cold, because" | same: `.mega` | "FAR: it is .mega's, because" |
| `kernel/mega.inc:5152` | "SI = DI = MEGA_SCOFF, CX = 2" | `mov cx, MEGA_SCCNT` (5155), which is 8 (5077) | "SI = DI = MEGA_SCOFF, CX = MEGA_SCCNT" |
| `kernel/mega.inc:9819` | "MEGA_LDT_N - 1 (768 of them) are descriptors with ANY 24-bit base and limit," | entries `MEGA_LDT_FREE0` to `[mega_ldtn]` - 1, sized per machine (`mega_layoutf` 10903-10910: 512 + one per 16 KB); no `MEGA_LDT_N`; no fixed aliases below (the GDT's own entries are) | "Entries MEGA_LDT_FREE0 to [mega_ldtn] - 1 (MEGA_LDT_BASE plus one per 16KB of extended memory, mega_layoutf) are descriptors with ANY 24-bit base and limit, handed out and taken back by selector. The entries below are the GDT's own (the LDT's base is the GDT's)." Replace the rest of the paragraph with: "Everything that READS a selector's base asks its descriptor (mega_selbase, mega_s2segc), and a code selector's data twin is +8 only when mega_ldt2ds finds one." |
| `kernel/mega.inc:9845` | "when all 768 are taken" | the bound is `[mega_ldte]` | "when every allocator entry is taken" |
| `kernel/kernel.asm:754` | "segment, becomes its CODE alias (LDT entry seg/32, selector (seg >> 2) \| 4," | `sub word [ss:bp+2], 8` (772): the pair's code half; a GDT selector is left alone | "; MEGA_CSELTOP - kern_mega: the word on top of the stack, an image's DATA selector (a pair's data half), becomes its CODE half (- 8, KERN-MEGA-MEMORY-PLAN MC7.4); a GDT selector is left alone. Every register and FLAGS preserved." |
| `docs/BOCHS.md:594` | "and CS `FFD8` is `.mega` at 0. A CS with bit 2 set is an LDT" | `.mega`'s CS is `MEGA_SEL_CODE` = `(MEGA_GDT_SCR + 8) * 8` (`mega.inc:164`), which depends on COLD_SEG (about 12xxh on today's layout); FFD8 was the pre-MC7 top-of-GDT scratch | "...and CS `MEGA_SEL_CODE` (`(MEGA_GDT_SCR + 8) * 8`, read it off a `make mega` listing) is `.mega` at 0." |
| `docs/BOCHS.md:595` | "code alias, whose image is segment `(CS & ~7) << 2`." | LDT code selectors are pair halves with any base; the image is the descriptor's base (`mega_selbase`) | "...an LDT code selector: a pair's code half (data = CS + 8) with any base, so read its base off the descriptor (`dbg "info ldt"`) rather than computing it." |
| `kernel/mega.inc:3270` (`mega_svc`'s lent-stack paragraph, 3256-3268) and SPEC.md 124.31.2 item 3 | "an IRQ the ROM owns goes on as before" | reflected with the foreign SS's selector as the real-mode stack segment (mc-2) | until mc-2 is fixed: "an IRQ the ROM owns is reflected on the program's stack, whose selector real mode takes as a paragraph: a program on a stack of its own must lend one (SEG_LEND)." |

## Open questions

1. **mc-1:** has WIN16's Mozart path (`int 10h AX=1A00h`) actually been run under Bochs or 86Box, or only under QEMU, which checks no limit? The code says it cannot survive a limit check. One Bochs row would settle it.
2. **mc-16:** is anything in megados.inc or the hibernate resume known to depend on the PIC being exactly at 20h, as opposed to "whatever `mega_pic_pm` sets"? I found only `mega_pic_at`'s AH parameter, which suggests not.
3. **mc-5:** is the 0.257 ms/KB figure from KERN-MEGA-DOS-PLAN 2.4 the Victory's `rep movsw` above 1 MB, or a BIOS block move? If it is the latter, the IF-off estimate for a piece is pessimistic.
4. Is LZ decoding into a pool claim serialized across tasks anywhere (a file-layer lock)? If it is, mc-9's BACK hazard is unreachable today and the +1-byte fix is insurance only.
