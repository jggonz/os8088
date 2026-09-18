# What `kern_dos` costs a disk, and what the DOS core costs TWICE

*Measured 2026-09-14 on `claude/dos-exec-investigation` at 11292d4, in this
container. Every figure is `nasm -f bin` output, `tools/os88lz.py` on those
bytes, or `os88disk.py --verify` on the tree's own 360KB images. Compressed
sizes of SPANS are marked ESTIMATED and are the measured 83% code ratio
applied to them.*

This file is a **measurement** and true of the tree it was taken on
(docs/README.md): a later `kern_dos` is a new file, not an edit of this one.

> **THIS REPORT WAS WRONG IN ITS FIRST FORM AND IS CORRECTED IN PLACE.** It
> concluded that docs/plans/KERN-DOS-PLAN.md §4.1's part scheme should be
> withdrawn in favour of a `KERNDOS.SYS` file. Two things were wrong with
> that. The gap is **five clusters**, which is a wash rather than a reversal.
> And it weighed nothing for **portability**, which is the reason the part was
> chosen and which a file cannot have: `DOS.O88` with the part in it is one
> file a user copies from disk A to disk B with all of its function, where a
> `KERNDOS.SYS` beside it is a sidecar a copy can separate from the program —
> the exact failure docs/plans/completed/O88-MULTISEG-PLAN.md wave 6 removed
> from `apps/c64`. **The part stands.**
>
> Individual parts compress (`OP_COMP`) and that was never in doubt:
> `apps/skies/csload.asm` ships a whole `.o88` image as `OP_SEG, OP_COMP`,
> which is the precedent section 4 builds on. What a parted package loses is
> only WHOLE-FILE compression, worth 5,425 bytes of `DOS.O88` — and that is
> the whole of the five-cluster gap.

---

## 1. The finding that survives

**The DOS core would be in `DOS.O88` twice.** It is 12,812 bytes of the
package's own 31,868-byte image — `dos_int21` and everything under it, the
PSP, the handle layer, the FCBs, the MCB chain, `AH=4Bh` and the built-in
commands — and every byte of it would be inside the `kern_dos` part as well.

Extracting it to a **third part both halves share** is worth about **12 KB of
every system disk, for ever**. Section 4 measures the seam that decides
whether that can be done cleanly, and it can: the seam is **one-directional,
46 call sites at 33 entry points**, and the core reaches **four `OSAPI_*`
slots and six library calls** outside itself, in three procs.

---

## 2. What the pieces cost

`kern_dos` here is `kerndos/kdos.asm` without `KD_GATE` — W5a's real entry,
the DOS core, the kernel's disk layer and `kdback.inc`'s doors:

| | bytes | of 46,407 |
|---|---:|---:|
| `.text` | 32,999 | 71.1% |
| `.cold` | 11,397 | 24.6% |
| `.ovlw` | 762 | 1.6% |
| `.modf` | 1,235 | 2.7% |
| **the image** | **46,407** | |
| `.bss` | 13,357 | |
| `.lowbss` | 3,328 | |

Compressed with the tree's own packer:

| | bytes | ratio |
|---|---:|---:|
| `kern_dos` raw | 46,407 | |
| …LZ4 | 38,257 | 82.4% |
| …LZB | 33,537 | 72.3% |
| `dos.bin`, the package's image, raw | 31,868 | |
| …LZ4 | 26,401 | 82.8% |

**It compresses badly and that is expected**:
docs/plans/O88-COMPRESSION-PLAN.md's own note is that bitmaps compress and
code does not, and this is 71% `.text`. **83% is the ratio for any span of
this material**, which is what the ESTIMATED figures below apply.

### 2.1 The disk that decides it

`build/os8088-360.img` has **53 of 354 clusters free** and carries `DOS.O88`
in `APPS/` (the Makefile's `SYSROOT`). `DOS.O88` is **26,443 bytes on disk
against a 31,868-byte image**, because it is whole-file compressed (flags bit
3) — and `tools/os88pkg.py` refuses `--compress` on a package that has parts,
so any part gives those 5,425 bytes back.

Three shapes, the part `OP_COMP` in all of them:

| | on disk | clusters | over today |
|---|---:|---:|---:|
| `DOS.O88` today, no `kern_dos` at all | 26,443 | 26 | |
| **1.** the part as W5a builds it, the box left in | image 32,256 + part 38,400 | **69** | **+43** |
| **2.** the box CUT from the part (14,622 bytes) | image 32,256 + part ~26,000 | **58** | **+32** |
| **3.** …and the shared core extracted to its own part | image ~19,456 + core ~10,600 + `kern_dos` ~15,700 | **46** | **+20** |
| *for comparison: a `KERNDOS.SYS` file, box left in* | 26,443 + 38,257 | *64* | *+38* |

Shape 3 is **12 clusters better than shape 2 and 23 better than shape 1**. The
comparison row is the container question and it is a wash: five clusters
against portability.

---

## 3. The two containers, honestly

| | `KERNDOS.SYS` | a PART of `DOS.O88` |
|---|---|---|
| 360KB system-disk clusters | +38 | +43 |
| portable — one file carries the function | **no** | **yes** |
| `DOS.O88` keeps whole-file compression | yes, worth 5,425 | no |
| the part or file itself compresses | yes | yes (`OP_COMP`) |
| what the stub does | walk its extents, expand | walk the same extents, expand |
| versions with the box | yes | yes |

The stub is the same either way, which is what makes this a packaging choice
rather than an engineering one: docs/plans/KERN-DOS-PLAN.md §4.1.1's argument
is that the part is never loaded AS a part — the handoff walks its bytes into
extents and the stub reads them with `int 13h` — so a file's bytes and a
part's bytes walk identically. The only difference at the code is the part
table read first, out of an image the box is already holding.

---

## 4. Can the shared core be extracted? MEASURED: yes

### 4.1 How big it is

Partitioning `apps/dos/`'s procs by reachability from the INT 21h family, the
launch sequence and the exit sequence — stopping at the `dos_be_*` doors, and
with the built-in command table's **indirect** edges added as roots, because
`dsh_tab`'s `mov ax, [bx+9]` / `jmp ax` is a call-graph edge no walker sees:

| | bytes | of 31,868 | procs |
|---|---:|---:|---:|
| **CORE** — what `kern_dos` needs | **12,812** | 40.2% | 171 |
| **BOX** — the window, the pages, the prompt, the packet driver | 13,075 | 41.0% | 268 |
| shared libraries and data (`os88con.inc`, `os88ui.inc`, `os88line.inc`) | 5,981 | 18.8% | |

**Adding the indirect edges moved 2,911 bytes across the line**, and that is
worth recording: the first pass put the thirteen built-in command bodies in
BOX, which is the same failure `tests/unit/t_dosseam.py`'s own header records
one wave earlier, for the same reason.

### 4.2 The code seam is ONE-DIRECTIONAL

| | transfers | distinct targets |
|---|---:|---:|
| BOX → CORE | **46** | **33** |
| CORE → BOX | **0** | **0** |

The busiest entry point is five call sites (`dos_upc`), and nothing is in a
per-character path — `dos_tty` becomes internal to the core once the built-in
commands join it.

**CORE → BOX being zero is not luck, it is W2.** The one edge that exists is
`dos_be_go`'s `jmp word [dos_betgt]`, which is the seam
docs/plans/KERN-DOS-PLAN.md §3 built and which `kerndos/kdback.inc` already
re-implements — so the direction that would have been hard is the direction
that was designed.

### 4.3 What the core reaches outside itself — ten sites, three procs

| | sites | where |
|---|---:|---|
| `OSAPI_MOUSE` | 1 | `dos_mou_read`, INT 33h |
| `OSAPI_DRV_CALL` | 1 | `dos_pkt_poll` — BOX-side in a split (§10: arm 3 has no packet driver) |
| `OSAPI_MEM_CLAIM` | 1 | `dsh_sbuild`, the built-ins' scratch |
| `OSAPI_MEM_FREE` | 1 | `dsh_sfree` |
| `con_write` | 1 | `dos_tty`'s windowed arm |
| `con_markall` | 1 | `dos_snap` |
| `con_tx_scroll`, `con_takerow`, `con_tx_row`, `con_tx_cursor` | 4 | `dos_fsx_owed` |

**`OSAPI_MEM_CLAIM` and `OSAPI_MEM_FREE` are the only hard ones**, and they
are hard for a stated reason: SPEC.md 20.12's parts rule 2 forbids a part the
kernel's ES-fenced slots, because ES is stamped from the caller's DS and the
kernel reads it as *who is asking*. The answer is in that rule already — *the
primary claims and passes a segment down* — so they are **two more doors**, in
the shape the other twenty-two have.

The six library calls are all in three procs and all on the **windowed** arm,
which W4 established `kern_dos` never takes: `[dos_inbr]` is 1 for ever there
(SPEC.md 96.38). So they are three more doors, or three procs that move to the
BOX side outright.

### 4.4 The state seam: 46 of 251 cells, and it is the launch block

| | cells |
|---|---:|
| bss cells the CORE alone touches | 139 |
| …the BOX alone | 66 |
| …**both** | **46** |

Twelve of the 46 fall away without a decision: `dos_trace*`, `dos_trnm*` and
`dos_trseg` (5) exist only in the `DOSTRACE` build, `dos_pkt_*` (6) are the
packet driver, and `dos_mrad` is the Memory page's radio record.

**The ~34 that remain are the launch block.** `dos_name`, `dos_args`,
`dos_ebuf`, `dos_blaster`, `dos_vol`, `dos_curdir`, `dos_arena`, `dos_apara`,
`dos_ldpsp`, `dos_ldpara`, `dos_ldname`, `dos_imgsz`, `dos_imghi`,
`dos_exit`, `dos_inbr`, `dos_vw`, `dos_vh`, `dos_dta`, `dos_dtaseg`,
`dos_sv_ss`, `dos_sv_sp`, and the `dsh_*` command-line scratch.
`kerndos/kdlaunch.inc` already marshals seven of them across a segment
boundary and is the shape the rest take.

### 4.5 The shape to build

`apps/skies/csload.asm` is the worked example and it is close: a package whose
part 0 is **a whole `.o88` image**, `OP_SEG` and `OP_COMP`, far-called, with
its own bss, its handoff a block at the head of that bss, and the kernel not
involved.

So the core is **one assembly, `OP_SEG | OP_COMP`, far-called, with its own
bss, in BOTH hosts**:

- `DOS.O88`'s box loads it with `op_load`/`op_seg` and far-calls the 33 entry
  points;
- `kern_dos` has no part loader, so its stub reads the core part's extents
  into a segment of its own and far-calls the same 33.

**One ABI, because it is the same ABI** — which is what makes it one build
rather than two, and is a property the file-versus-part argument does not
touch at all.

---

## 5. What this does NOT settle

- **The far-call bill.** 46 sites at 46.7 µs is ~2 ms of a launch, which is
  nothing — but `dos_be_goto` (4 sites) and `dos_upc` (5) have not been
  checked against a real workload.
- **Splitting the `DBSS` chain.** The core's bss and the box's become two
  chains and `os88_image_end` means a different thing in each. Mechanical, and
  it touches every one of 251 declarations.
- **Whether the 66 box-only cells really are.** They were classified by USE
  and not by intent, and a cell only the box touches today may be the core's
  tomorrow.
- **The order of the work.** Extraction is a refactor of a package that ships
  and works, and W5c's handoff does not depend on it. Doing them the other way
  round would put an unbuilt seam under an unbuilt stub.

---

## 6. The four-piece shape, costed — and the join can be NEAR

*Added after the owner proposed the shape below. Section 4 assumed the core
would be a far-called segment of its own; it does not have to be, and the
measurement that says so is in 6.2.*

The proposal:

| | what | |
|---|---|---|
| **image** | the parts loader, ~2 KB, dropped after it loads | `OSAPI_PKG_REHOME` |
| **part 0** | the UI — becomes the main image when the loader rehomes to it | `OP_SEG, OP_COMP` |
| **part 1** | the INT 21h core | `OP_SEG, OP_COMP` |
| **part 2** | `kern_dos` — the FAT, the mouse, the kernel bits | `OP_SEG, OP_COMP` |

…with **part 1 joining to EITHER part 0 or part 2**, never both, because the
two hosts are alternatives: one is the windowed box and the other is the
machine after the handoff.

**Every mechanism it needs already exists.** `OSAPI_PKG_REHOME` (0x0530) is an
X cell that tells the kernel *the program is at DX, not at me*: the loader's
region is freed, the carve is re-owned, and `ld_start` runs step 8 again
against the part (SPEC.md 20.12.10). The ordinary launch pays six bytes for
it. `apps/skies/csload.asm` is the worked example and its loader is **2,000
bytes** — the estimate was exact. And the loader needs no protocol to tell the
program where the parts went: it writes the vector into the head of the
program's bss, which ships inside the part and which the kernel does not zero
(SPEC.md 20.12.10.2).

### 6.1 What it costs on the 360KB system disk

| | raw | `OP_COMP` | clusters |
|---|---:|---:|---:|
| image — the loader | 2,000 | 2,000 (an image cannot compress) | 2 |
| part 0 — the UI | 19,556 | ESTIMATED 16,231 | 16 |
| part 1 — the core | 12,812 | ESTIMATED 10,633 | 11 |
| part 2 — the `kern_dos` bits | 18,959 | ESTIMATED 15,735 | 16 |
| | | | **45** |

**+19 clusters over today's 26**, against +43 for the shape W5a builds and +32
with only the box cut. It is also ~900 bytes better than section 2.1's shape 3
and **2 KB better in RAM**, because shape 3 kept the box as the uncompressed
image and this drops a 2 KB loader instead.

### 6.2 The join can be NEAR, and the reason is a coincidence worth checking yearly

A near join needs the core at the **same offset in both hosts**, so each host
reserves the range below it. The obvious objection is the hole that leaves in
whichever host is smaller. **Measured, there is almost no hole:**

| | bytes |
|---|---:|
| part 0, the UI (13,075 of box code + 5,981 of libraries + a header) | 19,556 |
| part 2, the `kern_dos` bits (`.text` + `.cold` + `.ovlw` + `.modf`, less the core, less the box that came along) | 18,959 |
| `CORE_ORG`, 512-aligned above the larger | **19,968** |
| the hole in part 0 | 412 |
| the hole in part 2 | 1,009 |
| the segment ends at | 32,780 |

**The two hosts are within 600 bytes of each other.** Part 0's 412 bytes are
zero-run padding that compresses to nothing, so the hole costs disk zero and
RAM 412; part 2 needs no padding at all, because the stub places it and does
not carve it.

With a near join, every obstacle section 4 listed dissolves:

| section 4's obstacle | with a near join |
|---|---|
| 46 far calls at 46.7 µs | they stay near, at 11 µs |
| a second `DBSS` chain | none — `os88_image_end` is the same offset in both hosts |
| `OSAPI_MEM_CLAIM`/`FREE` forbidden to a part (rule 2) | not a part's call at all; it is the host's own segment |
| six library calls out of `dos_tty`, `dos_snap`, `dos_fsx_owed` | six words of vector the host fills, not six doors |

What is left is **one new ABI and one new budget**:

- **a 33-entry jump table at `CORE_ORG`** — 99 bytes — because part 0 cannot
  know the core's internal addresses at assembly time. It is the same cost as
  33 far thunks and it is near.
- **`CORE_ORG` is a budget with two claimants**, exactly like `KERN_BUDGET`:
  both hosts assert against it and the ledger says who spent what. Today's
  600-byte margin between them is luck and will not stay lucky — the guard is
  what makes that a decision somebody takes rather than a build that breaks.

### 6.3 One thing to keep straight about part 2

`kern_dos` is **not loaded by the parts loader**, and
docs/plans/KERN-DOS-PLAN.md §4.1.1 is why: the heap is being given away, so
there is nowhere to load it to. The handoff walks part 2's bytes into extents
while the file layer is still alive and the **stub** reads them with `int 13h`
— and on this shape it reads **two** runs, part 2 to `KD_SEG:0000` and part 1
to `KD_SEG:CORE_ORG`. Same loop, one more extent list. "A smaller loader in
part 0" is exactly right; what it loads is a stub, not a part.
