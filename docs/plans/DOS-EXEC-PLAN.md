# DOS-EXEC-PLAN.md — running DOS `.COM` and `.EXE` programs

**STATUS: INVESTIGATION, with a first round of owner decisions folded in.
Nothing here is BUILT, and every size in it is an ESTIMATE unless the line
says MEASURED.** The three heap figures in §2.2 are measured; every byte count
for code that does not exist yet is a guess against a comparable that does.
**§12 carries the decisions taken and the questions still open**, so a reader
who wants to know what is settled should start there rather than infer it.

The ask: double-click `FOO.COM` or `FOO.EXE` in a Disk window and have it
run. Fullscreen is acceptable. The capability lives on disk until it is
wanted. A later phase carries our own drivers through to the DOS program —
mouse, hard disk, Sound Blaster, packet driver, and one day a redirector for
the RAM disk and the `OS88NET` volumes.

---

## 0. The verdict, before the detail

**It is feasible, it is smaller than it looks, and the reason is that this
machine is already an 8086 running real mode.** There is no CPU to emulate.
A DOS `.COM` is machine code for the processor we are standing on; "running"
it is setting up the memory it expects, pointing `INT 21h` at code of ours,
and doing a far jump. RunCPM (SPEC.md 74) had to interpret a Z80 to do the
equivalent; we do not.

The corollary is the same one a real XT lives with: **a program built for a
286 or a 386 will not run on a 4.77MHz 8088**, here or anywhere, and a great
deal of DOS software from 1990 onward is. This capability is worth what the
1981–88 catalogue is worth, which is most of what anyone wants it for; it is
not a route to running later software on the target machine.

Four findings shape everything below.

1. **The memory is there, and it is in the right place.** MEASURED on a
   640KB machine: **`OSAPI_MEM_AVAIL` answers 523 KB** to a package that has
   just been loaded — that call already counts purgeable caches as free and
   already includes what a compaction would recover, so it is the same
   question `mem_claim` answers and a claim of it will be served (§2.3.1).
   The free run is contiguous and **ends exactly at the top of conventional
   memory**, and a Disk window costs it nothing, because every claim the file
   manager makes is at the bottom of the heap. That is about what a 512KB PC
   running DOS 3.3 gave a program: a realistic DOS machine, not a token one.

2. **The containment answer is "exactly as contained as DOS itself, and no
   more."** An 8086 has no MMU. What we can do — and it is most of what is
   worth doing — is make the DOS program's picture of the machine *true*:
   give it a run whose top really is the top of something, and patch the one
   BIOS word every well-behaved program derives "how much memory is there"
   from. A program that respects its PSP allocation is contained by
   arithmetic. A program that scribbles at a hardcoded address is not, and
   on real DOS it was not either. **§14 is the one thing that would change
   that answer**, and it does it in time rather than in address space.

3. **The right home is a PACKAGE, not a kernel module.** A module's *data*
   is resident in `.text` (SPEC.md 2.6, 2.8) — the exact thing we are trying
   not to spend. A `.o88` package costs **zero resident bytes**, is on disk
   until double-clicked, gets its own segment, and the file-type association
   mechanism that launches it already exists and needs no kernel change
   (SPEC.md 54.5). The kernel cost of wave 1 is plausibly **nil**.

4. **The gap is file handles.** os8088's file API is whole-file and
   name-based — there is no open/close/seek/write-at-offset anywhere in it
   (SPEC.md 18.4.4 is the closest, and it is cluster-aligned and stateless).
   `INT 21h` `3Dh/3Eh/3Fh/40h/42h` are the functions every DOS program past
   1983 uses. **This is the single largest piece of work in the project**,
   and it is the piece that has to be written rather than mapped.

---

## 1. What "run a DOS program" decomposes into

Seven separable problems. They are listed in the order of how much work they
are, largest first, because that ordering is not the obvious one.

| # | problem | who provides it today | work |
|---|---|---|---|
| 1 | `INT 21h` file handles — open/close/read/write/seek | **nobody** | large |
| 2 | `INT 21h` everything else — ~50 functions | nobody | medium |
| 3 | Program load — `.COM` image, `.EXE` header + relocations, PSP, environment | nobody | small |
| 4 | Memory — an arena, an MCB chain, `48h/49h/4Ah` | the heap (SPEC.md 50) | small |
| 5 | The machine — screen, keyboard, timer, exclusive use | **fsx (SPEC.md 53)** | ~nil |
| 6 | Vector and hardware save/restore around the session | nobody | small |
| 7 | Launching it from a double-click | **assoc (SPEC.md 54)** | ~nil |

Items 5 and 7 are free, and they are the two that would have been the
frightening ones in a system that did not already have them.

---

## 2. Memory — the question that decides the design

### 2.1 How a DOS program knows what memory it has

Four mechanisms, and a DOS layer has to answer all four because different
programs use different ones. In rough order of how well-behaved they are:

1. **The PSP's word at offset `02h`** — "segment of the first byte beyond
   the memory allocated to this program". `.COM` programs that resize
   themselves read it; so does almost every self-respecting `.EXE`.
2. **`INT 21h AH=4Ah` (resize) then `AH=48h` (allocate)** — the documented
   way to get more. `48h` with `BX=FFFFh` deliberately fails and returns
   the largest available block in `BX`, which is how a program asks "how
   much is there". **This is the mechanism we control completely**, and the
   one a well-written program uses.
3. **`INT 12h`, or the BIOS data word at `0040:0013`** — "conventional
   memory in KB". Games and demos use it far more than they should. It is
   one word, we can change it, and see §2.3.
4. **The MCB chain**, reached through the undocumented `INT 21h AH=52h`
   (List of Lists; the word two bytes before the returned pointer is the
   first MCB segment). Memory-mapping utilities and some installers walk
   it. We build the chain, so we decide what they see.

An `.EXE` additionally declares `e_minalloc` and `e_maxalloc` in its header.
`maxalloc` is `FFFFh` in almost every real file, which means "give me
everything", and the DOS loader gives it everything and lets the program
give the surplus back with `4Ah`.

### 2.2 What our heap actually looks like — MEASURED

MartyPC, `os8088_5150`-class 640KB machine, `build/os8088-360.img` with
`build/apps360.img` in B:, build 186, `kern_big`. Read out of `mem_tab`
(SPEC.md 50.2, with `MC_SIZE` 10 per SPEC.md 66.2) and `mem_base`/`mem_top`:

```
heap                    0x1B20 .. 0xA000   =  531.5 KB

bare desktop            one claim, 63.0 KB at 0x2000 (purgeable read-ahead)
                        LARGEST FREE RUN   449.0 KB at 0x2FC0..0xA000
                        ...and it ENDS AT mem_top

B: Disk window open     four claims, ALL of them at 0x1B20..0x2FC0
                        LARGEST FREE RUN   449.0 KB at 0x2FC0..0xA000
                        ...and it STILL ends at mem_top

+ Paint running         ten claims; Paint's REGION is 33.0 KB at 0x97C0
                        LARGEST FREE RUN   390.0 KB at 0x3640
                        ...and the run ending at mem_top is 0.0 KB
```

**These are a HOST-SIDE WALK of `mem_tab` with the purgeable claim counted as
occupied — the at-rest picture, and NOT what a claimant can have.** The figure
a package is actually told is `OSAPI_MEM_AVAIL`'s, which counts caches as free
and includes what a compaction would recover: **523 KB**, measured in-guest on
this same machine (§2.3.1). Read the table below for where the free run
*sits*; read §2.3.1 for how big a claim may be.

Three things fall out of that table and each one matters.

- **It is a lot of DOS machine** — 449 KB at rest, **523 KB deliverable**.
  DOS 3.3 on a 640KB PC left a program about 580KB; on the 512KB machines
  most of this software was written for, about 430KB. We are in that range
  without trying, at either figure.
- **A Disk window costs the DOS program nothing**, because the file
  manager's claims are all bottom-up. The state you are in when you
  double-click is the good state.
- **A running package sits at the TOP.** A region is claimed top-down
  (SPEC.md 50.3), so the DOS runner's own image is the highest thing in the
  heap and the free run no longer reaches `mem_top`. That is the one fact
  the design has to be built around, and §2.3 is how.

The 63KB purgeable claim is the disk read-ahead and is `MEM_PG_HIGH`; the
kernel will shed it under pressure, which merges the low fragment and gives
**531.5 KB contiguous**. Not something to design against, but it is there.

### 2.3 Giving DOS a run, and whether it stays in it

The arena: **one `OSAPI_MEM_CLAIM_HI` of N KB**, so it lands as high as the
runner's own region allows, **with N taken from `OSAPI_MEM_AVAIL` and nothing
else** (§2.3.1). With a ~16KB runner region at the top, the run sits at
roughly `0x2FC0..0x9C00`.

Inside it we build a DOS machine:

```
  base+0000   MCB 'M'  owner = PSP     ->  environment block
  base+....   MCB 'M'  owner = PSP     ->  PSP (256 bytes) + program image
  base+....   MCB 'Z'  owner = 0       ->  the rest, free
                                            ^ 'Z' is the end of the chain
```

- `PSP:0002` = the paragraph past the program's block. Mechanism 1 answered.
- `48h/4Ah/49h` walk *our* chain and nothing else. Mechanism 2 answered, and
  a `48h BX=FFFFh` probe reports our largest free block rather than the
  machine's.
- `AH=52h` returns a List-of-Lists stub of ours whose `[-2]` word names our
  first MCB. Mechanism 4 answered.
- **`0040:0013` is patched to `(top of the run) / 1024` for the duration of
  the session and restored at the end.** Mechanism 3 answered. This is
  verified safe: `int 0x12` appears **exactly twice in the whole tree** —
  `mem_init_x`, which runs once at boot, and the Task Manager's display,
  which cannot run inside a bracket because everything else is frozen
  (SPEC.md 53.2).

**What that buys.** A program that uses any of the four mechanisms is
contained by arithmetic, with no hardware help and no per-access checking.
Its picture of the machine is internally consistent: memory below its PSP is
"DOS and its drivers" (really our kernel and the low heap claims), memory
above its block's end is "not there".

**What it does not buy.** Nothing stops a program doing `mov ax,0x8000 / mov
es,ax / stosw`. On an 8086 there is no mechanism that could. The honest
statement is that **our exposure is DOS's own exposure** — DOS had no
protection either, and every program that ran on it ran under exactly this
contract. The difference is consequence, not likelihood: a DOS machine that
gets scribbled on is rebooted, and ours takes unsaved documents with it.
That is a product decision (a warning on first run, or a "close your work
first" note) rather than an engineering one.

**The one place we are worse than DOS**, and it is worth saying: DOS loads
programs *low*, so "everything above me" is the program's own. We load high,
so a program that assumes it owns up to `0x9FFF` reaches over our runner.
The patched `0040:0013` fixes the programs that ask; it cannot fix the ones
that assume. Two mitigations exist if this turns out to bite, neither needed
for a first wave:

- **Load packages LOW instead.** Top-down regions are a workaround from
  before regions could be compacted, kept to hold the immovable thing out of
  the movable thing's way — and SPEC.md 50.3's own door says so in as many
  words, having been rewritten once already when §66.6.1 refuted its original
  justification. With regions movable, that asymmetry has mostly stopped
  earning its keep, and turning it round would put every os8088 claim below
  the DOS program and leave nothing but video memory above it. **That is a
  heap change and not a DOS change**, it touches every package in the tree,
  and whether it is worth taking depends entirely on a number nobody has yet:
  how many real DOS programs ignore what we tell them. **Not wave 1**, by
  decision — wave 1 is how the number gets measured.
- on a 386, V86 mode gives real trapping. Out of scope: the target machine
  is a 4.77MHz 8088 and `OSAPI_CPU_INFO` (SPEC.md 60) would gate it to
  machines this project does not calibrate against.

#### 2.3.1 Sizing the arena — `OSAPI_MEM_AVAIL`, and nothing else

**One call answers the whole question, and an earlier revision of this
document got it exactly backwards.** It claimed `OSAPI_MEM_AVAIL` reports the
run *before* any shed, and went on to propose that the runner walk
`OSAPI_CLAIM_SNAPSHOT` itself to work out the post-shed figure and then probe
for it. **All of that is wrong and none of it is needed.** The correction is
recorded here rather than quietly deleted, because it is a plausible mistake
that this project has already made once at a higher cost than a paragraph.

What `mem_avail_x` actually does, read in `kernel/memory.inc`:

- it enters at `MEM_LVL_TOP` — *"an ordinary claim's rank: **every cache
  counts as free, because `mem_claim` sheds them all**"*, in the routine's
  own first comment;
- its largest-run answer is **`mem_cp_plan`'s** — the run a **compaction**
  would leave, with those caches dissolved (on `kern_small`, which has no
  compactor, it is `mem_bigrun` over the same shed);
- and its total subtracts a claim only when that claim outranks the caller.

So **`OSAPI_MEM_AVAIL` already answers the same question `mem_claim` answers**.
A claim of the figure it returns will be served. SPEC.md 50.6.3 owns this and
says so in as many words, and it carries the history: `mem_avail` *did* report
the at-rest number once, on the reasoning that under-reporting was the
conservative direction — and that reasoning is *"exactly backwards ... every
consumer sizes itself DOWN from this number"*, so under-reporting is a feature
silently lost rather than a safe error. SPEC.md 66.10.3 is the other half, and
it is the one that put compaction into the answer.

**Measured on this build rather than argued**, driving `tests/heapfrag`
(`tests/heapcheck.py`) on the 640KB MartyPC machine — the same one §2.2 was
taken on, and the same situation the DOS runner is in, a loaded package asking
what it may have:

```
host-side walk of mem_tab, purgeable counted as occupied   449 KB
OSAPI_MEM_AVAIL, asked from inside the guest               523 KB   <- L0
```

**74 KB of difference, and the larger number is the true one.** §2.2's 449 is
this document's own naive walk and is the AT-REST picture; it is in that table
because it shows where the free run *sits*, not how big a claim can be.

Three consequences for the design, and the first is a rule rather than a
detail:

1. **The runner does not walk the claim table, and no package should.** The
   shed, the compaction and the ranking are the allocator's business, and a
   package that "allows for" them is subtracting what has already been added.
   `OSAPI_CLAIM_SNAPSHOT` exists for the Task Manager to *display* the heap,
   not for a package to *reason* about it.
2. **There is no probing.** Ask `OSAPI_MEM_AVAIL`, claim what it said. The
   compact-shed-retry loop inside `mem_claim` (SPEC.md 66.4, 50.6.2) is what
   makes that claim succeed, and it is the same walk that produced the figure.
3. **"Give the DOS program the maximum" is therefore two instructions.** No
   new slot, no user-facing choice, no `Give this program all the memory`
   check box — which was on the table and is not needed. An `.EXE` whose
   header carries a bounded `maxalloc` can still be given exactly what it
   asked for and the caches left alone; a `.COM`, and the `maxalloc = FFFFh`
   that almost every real `.EXE` carries, get the figure.

### 2.4 What about XMS and EMS

`XMEM.DRV` (SPEC.md 41) already manages memory above 1MB on a 286+.
Publishing it as an XMS driver (`INT 2Fh AX=4310h` → an entry point) is a
small, well-specified job and would let a DOS program that wants XMS find
some. **Not wave 1**, and worth noting it is `kern_big`'s alone. EMS is a
different thing entirely — it wants a page frame and hardware or a 386 — and
should be refused rather than faked.

---

## 3. Where the code lives

Three candidate homes. The table is the argument.

| | resident cost | own segment | own data | launched by |
|---|---|---|---|---|
| kernel module (SPEC.md 2.8) | **its data, in `.text`** | no — `COLD_SEG`, `DS = KERNEL_SEG` | **no** | a kernel thunk |
| driver (SPEC.md 51) | image only while attached | yes | yes | `SYSTEM.CFG` or a mount |
| **package (SPEC.md 20)** | **none** | yes | yes | **a double-click** |

**A package wins on every column.** The decisive one is the second row of
the first column: SPEC.md 2.8 says in as many words that a module's data
"stays in `.text`, reached through DS exactly as cold code's data is", and
`os88ovlchk` enforces "no data in `.cold`". A DOS layer's tables — the
handle table, the PSP scratch, the MCB cursor, the DTA, the vector save area
— are exactly the kind of data that would land resident. That is the
opposite of the brief.

A package also gets `APP_MAX_SIZE` = **60KB** of image + bss (MEASURED from
the SDK's own `equ`), against ~11KB for `ETHER.DRV` — a complete NE2000 plus
a TCP/IP stack — and ~22KB for Paint. A DOS shim is comfortably inside it.

So: **`DOSBOX.O88`** (name to be decided), in `apps/dosbox/`, shipping on the
apps floppy, declaring `COM` and `EXE` in its header's association block.
Zero kernel bytes for wave 1 is the target, and nothing found in this
investigation says it cannot be met.

The one thing a package does *not* get is the kernel's internal disk
routines — `dsk_next_clus_x`, `dskw_alloc`, `dsk_rd1_x` and the rest of the
cluster-level layer that §6.3 wants. That is the trade, and §6.3 is where it
is paid.

---

## 4. The bracket — fsx already does the hard part

`OSAPI_FSX_RUN` (SPEC.md 53.1) is very nearly purpose-built for this:

- **Multitasking is suspended** by a whitelist rather than `sch_lock`, so the
  BIOS tick chain, `sch_account` and `TF_SERVICE` driver workers keep running
  while every other instance's tasks freeze (SPEC.md 53.2).
- **The app owns the video mode** — nine of them across the three adapters
  (SPEC.md 53.4).
- **It runs on the UI task**, which is the one context allowed `int 16h`
  (SPEC.md 7), so the DOS program's keyboard just works.
- **The file API is legal mid-bracket**, which is what lets `INT 21h` reach
  the disk at all.
- **The gfx lock is held throughout**, so the mouse cursor cannot appear over
  the DOS program's screen.
- **The restore is one ordered path** (SPEC.md 53.6): mode back, BIOS key
  buffer drained, event queue drained, desktop repainted.
- Entry **silences every other instance's sound** (SPEC.md 53.3), which is
  exactly the "what do we silence" question for the audio half.

Four notes that are specific to this use and are not obvious from reading
SPEC.md 53:

1. **The runner must call `OSAPI_FSX_MODE` at least once**, even if only for
   `FSXM_TEXT`. `fsx_restore` skips `vid_setmode` when `[fsx_cur]` is still
   `0xFF` — verified in `kernel/fsx.inc` — and a DOS program will have set
   its own mode behind our back through the ROM's `int 10h`. Without our
   own mode call the desktop comes back into whatever mode the program left.
2. **The DOS program runs on its own stack**, `SS:SP` inside its own block,
   exactly as under DOS. Task 0's stack (`STK0_SIZE` = **512**, MEASURED)
   carries only the runner's frames up to the far jump, so the bracket adds
   no depth pressure.
3. **`FSXF_KEEPWORKER` is not wanted.** The runner has no feeder task.
4. **An app that hangs in the bracket hangs the machine** — SPEC.md 53.1
   says so, and it is documented rather than defended. A DOS program that
   hangs will hang os8088. There is no watchdog that could safely break in,
   because breaking in means restoring a machine mid-mutation. **A "hung
   program" escape is an open question, not a solved one** (§12).

---

## 5. The interrupt and machine-state ledger

What the kernel owns, verified by grepping every IVT write in `kernel/`:

| vector | owner | what it does | if DOS takes it |
|---|---|---|---|
| `08h` | `sch_isr` | the scheduler; **chains the ROM** | scheduler stops; sound worker starves; BIOS tick stops. Restore on exit. |
| `09h` | `kbm_isr` | **peeks port 60h and chains the ROM** | the BIOS buffer still fills either way; `int 16h` keeps working |
| `0Bh`/`0Ch` | `mou_isr` | the serial mouse, whichever COM the port is on | mouse dies for the session. Restore on exit. |
| `74h` | `mou_p2_isr` | the PS/2 aux port, on machines that have one (SPEC.md 9.9) | same |
| a discovered IRQ | `sbl_isr`, in `SOUND.DRV` | Sound Blaster DMA completion | a live stream dies |

That is the **whole** list. Nothing else in the tree installs a vector.

The `09h` row is the happy surprise: our keyboard hook is a peek-and-chain
that leaves the ROM's handler and the ROM's buffer intact, so a DOS program
polling `int 16h` needs nothing from us at all.

**The save/restore discipline**, and the answer to "do we need to?" is yes,
all of it, because DOS programs hook vectors as a matter of routine and TSR
installers never unhook:

- **Save the whole IVT** — 1KB, one `rep movsw`, restore it the same way at
  exit. Cheaper in both bytes and thought than a list of vectors to
  remember, and it is correct for vectors we do not know about (`INT 1Ch`,
  `1Bh`, `23h`, `24h`, the ROM's `1Eh` diskette table pointer, and every
  vector a TSR the program loads might take). **It must go in the runner's
  own bss and NOT in the arena** — the arena is the DOS program's to
  scribble on, and a save area the program can corrupt is worse than no save
  area, because it fails at restore time when there is nothing left to do
  about it. 1KB of the package's 60KB budget.
- **Save the whole BDA and restore a NAMED LIST out of it.** §5.1 is that
  list. It is settled rather than open, and it is short: 40 bytes restored
  out of 256 saved, four bytes zeroed, and everything else deliberately
  left as the DOS program left it.
- **PIT channel 0.** A DOS program that wants a fast timer reprograms it,
  and the scheduler's quantum goes with it. `sch_fast_on`/`sch_fast_off`
  already exist for exactly this (SPEC.md 53.2) and `sched_unhook` already
  knows how to put channel 0 back to mode 3 divisor 65536. Restoring it is
  three `out`s we already have code for.
- **The 8259 mask.** A program that masks IRQs and does not restore them
  leaves us with no timer. Save `0x21` and `0xA1`, restore both.
- **The video mode**, via the `fsx_mode` note in §4.
- **Nothing about the disk**, at the hardware level — see §7.

### 5.1 The BDA, and which of it comes back

**The BIOS Data Area is segment `0040`** — 256 bytes at linear `0x400`, written
by the ROM at POST and maintained by the ROM's own handlers for the whole life
of the machine. It is where the BIOS keeps the things it has to remember
between calls: how much memory the machine has, which video mode is set, where
the keyboard buffer is and how full, whether the floppy motor is spinning, how
many ticks since midnight. `INT 12h` is nothing but a `mov ax, [0040:0013]`.
A DOS program can write to any of it, and plenty do.

**What os8088 itself touches is a short list**, and it decides most of this
section. Grepped exhaustively for a load of segment `0040` across `kernel/`
and `drivers/` — **seven fields, nine sites**:

| field | site | what it does |
|---|---|---|
| `40:08`..`0F` LPT port base table | `lplink.inc` — `NET.DRV`'s port scan | reads it to FIND the parallel port |
| `40:10` equipment word | `vid_cga_equip` (`viddet.inc`), `vidsel.inc` | **WRITES** bits 5:4 to say which display os8088 picked |
| `40:17` keyboard flags 1 | `kbm_slock` and `kbm_isr`'s keypad-5 hatch (`mouse.inc`) | tests ScrollLock, and NumLock |
| `40:1A`/`40:1C` kbd buffer head/tail | `kbd_ovflow` (`mouse.inc`) | **every `int 09h`** — reads both, writes the tail |
| `40:3F` motor status, `40:40` motor countdown | `dsk_here_ok` and **`dsk_fdd_probe`** (`disk.inc`) | which floppy motors are spinning |
| `40:65` CGA mode-select shadow | `vid_bk` (`vidsel.inc`) | read-modify-write, to blank and unblank |
| `40:6C` tick count | `spl_clock` (`splash.inc`), `pm_ticks` (`SOUND.DRV`) | the wall clock |

Everything else in the 256 bytes, this system never looks at. **Two of those
rows change a verdict below from what it would otherwise have been**, and both
change it towards LEAVE rather than RESTORE — see `40:3F` and `40:6C`.

**The rule that generates the list**, and it is one sentence: *restore a field
if and only if the kernel reads it AND its pre-session value is still true;
zero a field whose honest value is "nothing is held"; leave everything else,
because the machine really did change.*

#### RESTORE — 40 bytes in seven spans

```
40:00  16  COM1-4 and LPT1-4 port base addresses
40:10   2  equipment word
40:13   2  conventional memory size in KB
40:1A   4  keyboard buffer head and tail pointers
40:72   2  soft-reset flag
40:80   4  keyboard buffer start and end offsets
40:98  10  INT 15h user-wait flag pointer, count and flag
```

- **`40:13`** is the one we patched ourselves (§2.3). Not restoring it leaves
  the machine reporting a small size for the rest of the session — the Task
  Manager's own display reads `int 12h`, and it is the second of the only two
  `int 12h` sites in the tree.
- **`40:80`/`40:82` is the sharpest entry in the whole ledger**, and it is not
  obvious. A DOS TSR that *enlarges the keyboard buffer* does it by repointing
  these two words at a bigger buffer **in its own memory**. When the bracket
  ends and the arena is freed, the ROM's `int 09h` keeps writing keystrokes
  into an address that is now whatever claimed that memory next — silent heap
  corruption that first shows minutes later, on an unrelated keystroke, in an
  unrelated program. Restore them and the ROM writes into `40:1E` again.
  Restore what was THERE, not the constants: `kernel/mouse.inc` records that
  these two words **do not exist on the earliest 5150 ROM**, which is exactly
  why `kbd_ovflow` uses literal `KBD_BUFB`/`KBD_BUFE` and not these.
- **`40:98`..`40:A0` is the same trap in a different field.** `INT 15h AH=83h`
  arms a wait by handing the ROM a far pointer to a flag byte to set when the
  interval expires. A program that arms one and dies leaves the ROM's tick
  handler writing into the arena after we have freed it.
- **`40:1A`/`40:1C`.** `fsx_restore` already drains the buffer through
  `int 16h`, so head and tail agree by then. What the drain cannot fix is a
  program that left them pointing **outside** `40:1E..40:3D`: `kbd_ovflow`'s
  range check then takes its `.out` arm and the overrun guard of SPEC.md 9.8
  is dead for the rest of the session, silently. Restore them with the pair
  above — the keyboard goes back as one thing or not at all.
- **`40:10`.** The kernel *writes* this to match the adapter it chose, so a
  program that rewrites it leaves the kernel's own statement about the machine
  untrue.
- **`40:72`.** `1234h` here means "warm boot, skip POST". A program that set
  it changes what the next `int 19h` does, and `ui_cmd_reboot` is an `int 19h`.
- **`40:00`..`40:0F` has a real reader and it is not the obvious one.** The
  mouse addresses its UART by port number, so the COM half (`40:00`..`40:07`)
  is in for completeness only. **The LPT half is not**: `NET.DRV`'s candidate
  scan walks `40:08`..`40:0F` to find the parallel port at all
  (`drivers/net/lplink.inc`), so a DOS program that blanks an entry — which a
  printer TSR reasonably might — leaves the parallel link with nothing to
  attach to, and the failure is "there is no cable" rather than anything that
  points here.

#### ZERO — 4 bytes, because restoring them would be worse

```
40:17  1  keyboard flags 1 (shift/Ctrl/Alt, and the lock states)
40:18  1  keyboard flags 2
40:96  1  keyboard flags 3 (101-key)
40:97  1  keyboard flags 4 (LED state)
```

These say which keys are **held right now**. The pre-session value is not the
truth and neither is the program's — the truth is whatever the user's hands are
doing when the desktop comes back, and the honest encoding of that is *nothing
is held*. Restoring the old bytes can leave a phantom Ctrl or Alt down, which
makes every menu and every keystroke behave oddly until the user presses and
releases that key by accident. A stuck ScrollLock silently disables the
keypad-5 mouse-button hatch, since that is the bit `kbm_isr` tests. The BIOS
re-asserts the LEDs on the next keystroke.

#### LEAVE — everything else, and each for a stated reason

- **`40:6C` (dword) and `40:70`** — ticks since midnight, and the midnight
  rollover flag. **Time passed.** Our `sch_isr` chains the ROM handler, so this
  counted up through the whole session exactly as it should have; putting the
  old value back would set the machine's clock back by the length of the DOS
  program's run, and `int 1Ah` off this counter is rung 0 of the clock ladder
  (SPEC.md 37.90) — the rung a 5150 with no RTC actually uses. It has **two
  live readers in this tree** (`spl_clock`, and `SOUND.DRV`'s `pm_ticks`), and
  both want it CURRENT: this is a field the kernel depends on and must not get
  back, which is why the rule at the top of this section is two clauses and
  not one.
- **`40:3E`..`40:48`** — floppy recalibrate status, motor status, the motor
  timeout counter, last status, and the 765's ST0–ST6. This is *hardware*
  state and the ROM's tick handler counts `40:40` down to turn the motor off.
  **The kernel reads `40:3F` and `40:40` in two places** — `dsk_here_ok`,
  and `dsk_fdd_probe`, whose own comment says "there is no
  readable DOR, so the BIOS's own MOTOR_STATUS is the only place the current
  motor state exists". That makes the LEAVE verdict **stronger** rather than
  weaker: restoring a pre-session motor bitmap would hand the probe a
  statement about which drives are spinning that is not true of the machine,
  and SPEC.md 18.97 retires a drive on what that probe decides.
- **`40:49`..`40:66`** — the entire video block: mode, columns, page size and
  offset, eight cursor positions, cursor shape, active page, CRTC port, and the
  `3x8h`/`3x9h` shadows. **This is the largest block in the BDA and it needs
  nothing from us**, because `fsx_restore` step 1 calls `vid_setmode`, which
  goes through the ROM's `int 10h`, and the ROM rewrites all of it as part of
  setting the mode. Restoring it before that would be overwritten; restoring it
  after would contradict the mode actually set. (`40:65` is in this block and
  is one of the four fields the kernel reads — `vid_bk`'s blank/unblank — and
  it still wants leaving, for the same reason: the mode set is what makes it
  true again.)
- **`40:74`..`40:77`** hard-disk status, drive count and the XT fixed-disk
  control pair — the ROM maintains these and `HDD.DRV`'s rung 0 goes through
  `int 13h` (SPEC.md 52.1), so the ROM's own copy is the one that matters.
- **`40:84`..`40:95`** — EGA/VGA rows, character height, the two video display
  data areas, floppy media control and per-drive media state and current track.
  Video half is `vid_setmode`'s; floppy half is hardware state.
- **`40:78`..`40:7F`** printer and serial timeouts, **`40:A1`..`40:A7`** the
  LAN bytes, **`40:A8`** the video parameter table pointer, **`40:F0`..`40:FF`**
  the inter-application communication area, and **`0050:0000`** the
  print-screen status byte. Nothing here reads any of them.

#### Why save all 256 when only 40 come back

Because the save is what makes the list **auditable** rather than a guess. One
`rep movsw` into the runner's own bss costs 256 bytes and ~1,300 cycles; having
the before-image means a diagnostic can print what the DOS program actually
changed, which is how the next field in the RESTORE column gets found. The
alternative — saving only the seven spans — saves 216 bytes of a 60KB package
and loses the evidence.

---

## 6. `INT 21h` — the actual work

### 6.1 What is free because the ROM is still there

A DOS program spends a lot of its life in the BIOS, not in DOS, and the BIOS
is untouched: `int 10h` video, `int 16h` keyboard, `int 13h` disk, `int 1Ah`
clock, `int 17h` printer, `int 14h` serial. **We provide none of these and
they all work**, which is most of why this project is tractable. It is also
why a graphics-mode game is *easier* than a text-mode utility: the game
talks to the ROM and to the framebuffer, and the utility talks to `INT 21h`.

### 6.2 The shim's own surface

Roughly fifty functions. Grouped by what they cost us:

**Trivial — a few instructions each, no state (≈20 functions).**
`30h` version (report 3.31 or 5.00 — see §12), `25h`/`35h` set/get vector,
`2Ah`–`2Dh` date and time (`OSAPI_GET_TICKS` and the clock, SPEC.md 37
is resident), `19h`/`0Eh` current/select drive (we have volume indices
already, SPEC.md 18.7), `1Ah`/`2Fh` set/get DTA, `33h` break flag,
`62h` get PSP, `50h`/`51h` set/get PSP, `59h` extended error, `0Dh` disk
reset, `54h` verify flag, `38h` country info (a stub).

**Character I/O — `01h`, `02h`, `06h`, `07h`, `08h`, `09h`, `0Ah`, `0Bh`,
`0Ch`.** Under a fullscreen bracket these go straight to `int 10h` TTY and
`int 16h` and are nearly free. They are also the functions a *windowed*
DOS box would have to intercept instead (§10).

**Process control — `00h`, `4Ch`, `31h`, `4Dh`, and `INT 20h`, `INT 27h`.**
Terminate is the interesting one: it has to unwind back to the runner's
saved `SS:SP` and return from the bracket. `INT 22h`/`23h`/`24h` are vectors
we install and the program may replace; `24h` (critical error) needs a real
handler that fails the operation rather than retrying forever.

**Memory — `48h`, `49h`, `4Ah`, `58h`, `52h`.** §2.3. A first-fit walk of
our own MCB chain; a few hundred bytes.

**EXEC — `4Bh`.** A program launching another program. `4B00h` (load and
execute) is genuinely used by installers and by anything that shells out.
It is the loader called recursively with a second PSP, and the memory has to
come from the same arena. **Deferrable**, and `4B01h`/`4B03h` (load without
executing, load overlay) more so — though `4B03h` overlays are how some
large programs are built and refusing it will lose a few titles.

**FCB functions — `0Fh`–`24h`, and `29h` parse filename.** The pre-DOS-2
file interface. Largely dead by 1985, but `29h` is used by programs parsing
their own command tail even when they then use handles, and a few stubborn
titles are FCB-only. **Wave 3 at the earliest**, and possibly never: the
cost is real and the audience is small.

**Directories — `39h`, `3Ah`, `3Bh`, `47h`.** These map almost exactly onto
`OSAPI_FILE_MKDIR`, `OSAPI_FILE_RMDIR`, `OSAPI_FILE_GOTO_Q` and
`OSAPI_FILE_HERE`. Cheap.

**Find first/next — `4Eh`, `4Fh`.** `OSAPI_FILE_FIND` is an ordinal-based
enumeration that fits this well; the work is the DTA format and wildcard
matching, both small.

**...and the handles, which are their own section.**

### 6.3 File handles — the one real gap

`3Dh` open, `3Eh` close, `3Fh` read, `40h` write, `42h` lseek, `45h` dup,
`46h` dup2, `3Ch` create, `5Bh` create-new, `41h` delete, `43h` attributes,
`56h` rename, `57h` file date, `68h` commit.

**os8088 has no file handle anywhere.** The whole published API is by name
and by whole file:

- `OSAPI_FILE_READ` reads an entire file into a buffer you sized.
- `OSAPI_FILE_WRITE` creates or replaces an entire file.
- `OSAPI_FILE_APPEND` appends, and **only at a cluster boundary**.
- `OSAPI_FILE_READ_AT` reads at an offset, and **the offset and the capacity
  must both be whole clusters**, and it **walks the chain from the front on
  every call** because it is deliberately stateless (SPEC.md 18.4.4).

So the shim has to build the handle layer itself. Two ways, and the choice
is the biggest open design question in this document.

**(a) On the published API, in the package.** A handle is a record holding
the name, a file position, and one cluster-sized buffer. `3Fh` for a read
inside the buffer is a `movsw`; a read that crosses a cluster is an
`OSAPI_FILE_READ_AT`. Writes are the problem: there is no write-at-offset in
the API at all, so an in-place update means read-modify-rewrite of the whole
file. That is correct but it is seconds per write on a floppy, and a program
that updates a record at a time (a database, a save-game slot) would be
unusable. **Reads work well; writes work badly.**

- Cost: ESTIMATE ~2–3KB of package code and one cluster buffer per handle.
- Risk: the `READ_AT` chain walk is O(clusters) per call. SPEC.md 18.4.4
  argues that is cheap against the floppy time, and for sequential reading
  it is — but a program seeking backwards in a large file pays it every time.

**(b) With a kernel addition.** A published `OSAPI_FILE_SEEK`/`READ_AT_RAW`/
`WRITE_AT` trio over the cluster layer that `diskw.inc` already has
internally. That makes the DOS side thin and fast, and it is a capability
the whole system lacks and would benefit from — but it is resident kernel
bytes, it needs a design of its own, and it is a much bigger conversation
than a DOS box.

**Recommendation: (a) for wave 1, with writes restricted to
create/append/replace, and the in-place-write case refused honestly** in
SPEC.md 47's sense — a notice naming what the program asked for. Then
measure which real programs that loses, and let that decide whether (b) is
worth a kernel design.

**But (b) is wanted on its own merits, and that changes who should pay for
it.** This is not a DOS-box problem for a DOS box to solve privately: the
whole-file API costs os8088's own programs the same thing, and the sharpest
number on the table is **TANK ATTACK taking 2-4 seconds to load or write a
high-score file of a few bytes**. No amount of buffering inside a DOS shim
fixes that, because the cost is in the layer underneath it. So the honest
framing is that **`INT 21h` is the SECOND customer for a seek/write-at API,
not the first** — and if that API gets designed, the DOS side should be built
on it rather than around it. Wave 1 shipping read-only handles is a
sequencing decision, keeping the DOS work off the critical path of a kernel
design that wants doing for its own reasons; it is not a judgement that
read-modify-rewrite is good enough.

---

## 7. Disk state — what needs saving, and what does not

**At the hardware level, nothing.** The kernel's disk layer is `int 13h`
plus a FAT driver (SPEC.md 18); it holds no controller state across calls,
and `dsk_xfer` raises `[sch_lock]` around each transfer. A DOS program
calling `int 13h` is calling the same ROM we do. The one exception is the
diskette parameter table — SPEC.md 18.92 says the `int 1Eh` table is **ours**
— and the blanket IVT save/restore of §5 covers the pointer.

**At the filesystem level, quite a lot**, and this is a correctness issue
rather than a crash one. The kernel caches three things a DOS program can
invalidate by writing to a disk behind our back:

- the **global mount snapshot** (the directory listing every window reads),
- the per-volume **FAT window** (SPEC.md 18.8), a cache of the allocation
  table,
- the per-window **listing cache** and the icon cache.

If a DOS program writes to A: through `int 13h`, or through our own `INT 21h`
(which goes through the file API and so *does* keep the kernel's own state
straight), the snapshot may be stale. The cure exists and is one call:
**`OSAPI_VOL_MOUNT` on exit**, which re-mounts and re-lists. `[dsk_lstale]`
is the kernel's own name for this debt and `dsk_relist` is what pays it.

Cheapest correct policy: **re-mount every volume the session could have
touched, at bracket exit, unconditionally.** It is a few hundred ms of
floppy per volume against a class of silent corruption, and the user is
coming back from a fullscreen program so a pause is expected.

**The removable-media hazard is real and is already documented**: SPEC.md
18.9.3's batch bracket rests on the argument that a user cannot swap a disk
while the machine is frozen. A DOS session is minutes long and the machine
is *not* frozen from the user's point of view — they can absolutely change
the floppy. The safe answer for wave 1 is that a DOS program reaches disks
through our `INT 21h` and our mount discipline, and that **`int 13h` writes
from a DOS program are out of contract** — allowed, because we cannot stop
them, but the re-mount at exit is what we promise and nothing more.

---

## 8. Launching — the double-click

Already built, and needs **no kernel change**:

1. `DOSBOX.O88` carries an association block in its header declaring the
   extensions `COM` and `EXE` (SPEC.md 54.5 — the block carries extensions
   only, and the program's stem comes from the file it was harvested from,
   which is what makes this work on any disk).
2. `disk_mount` harvests it; `.COM` and `.EXE` files get the runner's icon
   marked as documents (SPEC.md 54.1) — so they *look* runnable in a Disk
   window, which is the whole user-visible half of the ask.
3. Double-click → `assoc_locate` finds `DOSBOX.O88` → `ld_run_name` launches
   it → the new instance reads `OSAPI_ARG_FILE` and learns which file it was
   opened on.
4. It is launched standing in the clicked file's own directory (SPEC.md
   19.2.1), which is also where a DOS program expects to find its data.

Two consequences worth stating.

- **This is `kern_big` only.** SPEC.md 54.0 gates the whole association
  feature out of `kern_small`. That is the right answer anyway: a 128KB
  machine has ~31KB of heap and cannot host a DOS program.
- **A second entry point is wanted**: launching `DOSBOX.O88` directly, with
  no argument, should give a command prompt rather than an error. That is
  where a `COMMAND.COM`-ish shell would live if one is ever wanted — and the
  honest first answer is a file-picker, not a shell, because a shell means
  `4Bh` EXEC, a command parser, batch files, and internal commands.

---

## 9. Phase 2 — carrying our drivers through

The shape of every row here is the same: **we already have the device
working; what is missing is the DOS-facing interface to it.** None of these
needs a device driver to be written or ported. That is worth saying plainly
because the resources named in the brief are all *drivers*, and drivers are
the half we already have.

### 9.1 Mouse — `INT 33h`

**The cheapest and highest-value row.** The kernel's mouse ISR keeps
`mouse_x`/`mouse_y`/`mouse_btn` fresh throughout an fsx bracket — SPEC.md
53.1 states it explicitly, and it is why the pointer stays live. So an
`INT 33h` implementation is a translation layer over variables that are
already being maintained, not a driver.

Functions worth having: `00h` reset/detect, `01h`/`02h` show/hide,
`03h` position and buttons, `04h` set position, `05h`/`06h` press/release
counts, `07h`/`08h` ranges, `0Bh` motion counters, `0Ch` event handler,
`0Fh` mickeys-per-pixel. ESTIMATE **under 1KB**.

**CuteMouse is a reference, not a source.** It is GPL-2 (checked), this tree
is MIT under one licence file, and CONTRIBUTING.md §6 forbids vendored
third-party code. More to the point it is a *driver* — it talks to the
serial port and the 8042 — and we do not need one.

**One real gap.** `mou_apply` consumes the raw deltas into a screen-clamped
position and keeps no mickey accumulator (read in `kernel/mouse.inc`). Two
consequences: function `0Bh` has to be derived from position changes, which
loses motion while the pointer is against a screen edge; and the desktop's
resolution is not the DOS mode's, so everything needs scaling. Absolute
programs (menus, CAD, paint packages) will be fine. Mickey-driven ones
(anything with mouselook) will feel wrong at the edges. **A raw-mickey pair
in the ISR would fix it for an estimated ~10 resident bytes** — the one
kernel change this whole document actively recommends considering, and it
should be costed properly rather than taken on this sentence.

### 9.2 Hard disk

**Rung 0 is already free.** SPEC.md 52.1: a drive the BIOS knows is reached
through `int 13h` 80h/81h, so a DOS program sees it with no work from us at
all.

**Rung 1 is not reachable.** That rung is the IDE task file at 1F0h/170h,
for a drive the BIOS does *not* know, and it is gated on `CPU_286`. There is
no `int 13h` number to offer for it. Giving DOS access to a rung-1 drive
means either providing `INT 21h` file access to it (which the shim does
anyway, through the volume layer — so **a DOS program using `INT 21h` gets
rung-1 drives for free**) or writing an `int 13h` shim over `DSV_BLK`
(SPEC.md 51.8), which is possible and is a wave of its own.

The useful framing: **`INT 21h` covers every volume os8088 can mount,
including driver-backed ones, because the shim goes through the volume
layer** (SPEC.md 18.7 — a volume is an index, and `dsk_xfer` dispatches to
BIOS or driver). `int 13h` covers only what the BIOS knows. Programs that
use files are fine; programs that read raw sectors are not.

### 9.3 Sound Blaster

The hardest row, and the one where the honest answer is likely "don't".

`SOUND.DRV` owns the card: it hooks a discovered IRQ, drives DMA channel 1,
and has a feeding worker (`TF_SERVICE`, which keeps running inside a bracket
by design, SPEC.md 53.2). A DOS program that wants the Sound Blaster wants
to program it *itself* — reset the DSP, set its own IRQ and DMA, own the
card completely.

Two choices, and they are exclusive:

- **Detach.** `sbl_detach` / `sbl_halt` / `sbl_unhook` already exist and put
  "the vector, mask and DSP back as we found them" (their own comment). Call
  the driver's detach before the bracket, re-attach after. The DOS program
  gets a virgin card and a `BLASTER=` environment variable naming its port,
  IRQ and DMA, and everything works exactly as it does under DOS. **This is
  the right answer**, and it is mostly existing code.
- **Virtualise.** Trap the card's ports and translate to our sound layer.
  Not possible on an 8086 — there is no I/O trapping without protected mode.

**DECIDED: detach for the session, re-attach at exit**, and accept that
os8088's own audio is silent while a DOS program runs — nothing on our side is
expected to keep playing. Which it would be anyway: SPEC.md
53.3 already silences every other instance at bracket entry.

**"Sound Blaster from scratch" is not needed.** We are not writing a DOS
sound driver; the DOS program brings its own. We are getting out of its way.

### 9.4 Packet driver

**A packet driver is an interface, not a program**, and the brief's framing
is worth correcting: mTCP is a suite of DOS *applications* (FTP, telnet,
IRC, a web server) that **consume** a packet driver. It is GPL and it is the
wrong half. What we would provide is the **Crynwr Packet Driver
Specification** — a published interface, vectors `60h`..`80h`, with a
signature string `PKT DRVR` at offset 3 of the handler so clients can find
it, and about a dozen functions (`driver_info`, `access_type`,
`release_type`, `send_pkt`, `get_address`, `terminate`).

And it is a **very good fit**, for one specific reason: `ETHER.DRV` **hooks
no interrupt vector at all** and polls the NE2000's receive ring (verified in
`drivers/ether/ether.asm`: "DRVV_ATTACH — find a card, and hook NOTHING").
So there is no IRQ to arbitrate and no ISR to hand over.

> **BOTH BULLETS BELOW WERE WRONG, and they are left with the correction
> beside them because the shape of the error is the useful part** — this
> section costed the wave by reading the driver's *design* and never its
> *verb table*. See §15.7, and SPEC.md 96.23.1.
>
> 1. **There is no transmit verb.** `ETHER.DRV`'s table is fifteen verbs and
>    every one is a SOCKET; `ne_tx`/`ne_rx` are internal and reachable from no
>    package. Wave 4 had to add three raw verbs first (SPEC.md 72.22).
> 2. **The box unloads the network driver on the way in.** §51.11's
>    `drv_suspend_x` skips only `DRVC_DISK` and `DRVC_FILE`, so wave 3's own
>    door took the card away before a packet driver could reach it.

A packet driver over `ETHER.DRV`'s existing verbs is:

- `send_pkt` → the driver's transmit verb, directly;
- receive → poll the ring from our `INT 08h` path or from the shim's idle
  points, and up-call the client's registered receiver.

The catch is that the client's receiver is called *from* the poll, so the
polling has to happen often enough. Under a bracket we control the loop, so
this is tractable. ESTIMATE **1.5–2.5KB**, and it would make mTCP's own
applications run on os8088 — a very good validation target, and a genuinely
attractive demo.

**Ordering note**: this is only worth doing after the shim's `INT 21h` is
solid, because every mTCP application is a heavy user of file handles.

### 9.5 A DOS redirector for the RAM disk and network volumes

The brief mentions this as "potential ... at some point in the future", and
the investigation turns up something better than expected: **os8088 already
has a redirector interface of its own.** `DRVC_FILE` is a driver class whose
`DSV_FS` points at an `FSV_*` verb table — `FSV_LIST`, `FSV_CHDIR`,
`FSV_STAT`, `FSV_READ`, `FSV_WRITE`, `FSV_APPEND`, `FSV_READAT`,
`FSV_DELETE`, `FSV_RENAME`, `FSV_MKDIR`, `FSV_RMDIR`, `FSV_DFREE`,
`FSV_ENUM`, `FSV_COPY`, `FSV_RMTREE` (`kernel/driver.inc`). The RAM disk
(`drivers/ramdisk/`) and the parallel-link network drive (`drivers/net/`,
SPEC.md 62) both use it.

So there is **no DOS redirector to write**. The shim's `INT 21h` goes
through the volume layer, and the volume layer already dispatches a
`DRVC_FILE` volume to its driver's `FSV_*` verbs. **A DOS program gets the
RAM disk and the network drive as ordinary drive letters, for free, the day
`INT 21h` works.** The ISA-PicoMEM redirectors named in the brief solve a
problem this system solved differently and earlier.

(The one thing that would need the real DOS redirector interface — `INT 2Fh`
`AX=11xx` — is a *third-party* DOS network client wanting to see our
volumes. That is a different and much rarer want, and it should not be
confused with the above.)

---

## 10. What will not work, stated plainly

A section written now so it does not have to be discovered later.

- **A windowed DOS box for arbitrary programs.** A program that does all its
  output through `INT 21h` character functions or `int 10h` TTY **can** be
  rendered into a window, and RunCPM's terminal (SPEC.md 74.2) is the working
  precedent for the rendering half — that is the windowed text mode §11 now
  carries as a named phase. What cannot be made to work is the general case:
  **a DOS program writing directly to `B8000` bypasses every interception
  point**, and most of them do, because that is what made them fast. Making
  *that* work means a shadow buffer at `B8000`, which means the program
  cannot be at its real address, which on an 8086 means no. So the windowed
  mode is a **capability a program either has or has not**, decided the first
  time it writes a character, and the refusal — "this program draws its own
  screen; run it fullscreen" — is a normal path in SPEC.md 47's sense rather
  than a failure.
- **TSRs that survive the session.** `31h` and `INT 27h` can be made to work
  *within* a session, but the IVT restore at exit takes them out. A resident
  DOS program outliving the bracket would mean our vectors staying replaced
  while the GUI runs, which breaks the scheduler and the mouse.
- **Anything needing DOS 5+/6 specifics**, long filenames, or `INT 2Fh`
  services from `COMMAND.COM` (`INT 2Eh`).
- **Programs that reprogram the PIT and do not restore it**, if they also
  crash before terminating. We restore at exit; a program that never reaches
  our exit path never gives us one.
- **Multitasking.** The bracket is exclusive by design. One DOS program at a
  time, and os8088 is frozen behind it.
- **`.EXE`s larger than the arena**, and any program wanting more than
  ~433KB. Not many.

---

## 11. Waves

Sizes are ESTIMATES against measured comparables (`ETHER.DRV` 11,230 bytes
for an NE2000 plus a TCP/IP stack; `HDD.DRV` 5,489; Paint 21,962).

| wave | what | kernel bytes | package bytes (EST) |
|---|---|---|---|
**WAVES 1 AND 2 ARE BUILT** — see §15 at the end for what they cost and
what three of these rows got wrong.

| 1 | `.COM` only. Arena + MCB chain + PSP + environment. fsx bracket, IVT/BDA/PIT/8259 save-restore. Character I/O, process control, memory, date/time, vectors. **Read-only file handles, behind the back-end indirection §14.4 asks for.** The `..` walk for the cwd (§11.3). Double-click via an association block. | **0** | 6–9 KB |
| 2 | **BUILT.** `.EXE` loader — MZ header, relocations, `minalloc`/`maxalloc`. Directory functions, find-first/next, create/replace/append writes, `INT 33h` mouse. | **0** | **+2.7 KB** (image 3,134 → 5,878; 5,321 compressed on disk) |
| 3 | **BUILT, all four.** `4Bh` EXEC (SPEC.md 96.14), XMS via the `OSAPI_XMEM_*` slots (96.15), `INT 12h`/BDA (already done in wave 1: the BDA's memory word is written at bracket entry, and the ROM's `int 12h` reads it), and **the drivers out of the way** — which came out a KERNEL slot rather than package code, `OSAPI_DRV_SUSPEND` (96.17, 51.11), and is the only row in the whole plan that spent a kernel byte. §15.5 below is what it cost and what the refusal got wrong. | **209 `.text` + 5 `.bss` + 1 API cell** | **+2.9 KB** |
| 4 | **BUILT.** Packet driver over `ETHER.DRV` (SPEC.md 96.23), validated with mTCP's own `PKTTOOL` and `PING`. **Not the package-only row this table claimed**: §9.4's two premises were both false and §15.7 is what it cost. | **4 `.text`** + 3 driver verbs | **+1.2 KB** (image 13,074 → 14,292; 1,060 compressed) |
| 5 | **BUILT.** The create/append write path landed first (§15.8–§15.10, `INSTALL.EXE` runs to completion); **write-at-offset followed** as `OSAPI_FILE_WRITE_AT` (SPEC.md 18.4.7) with `3Dh` modes 1/2 honoured over it (96.11.6). The row's own estimate of "0 or ~400" kernel bytes was the right shape and high: **15 resident**, because the chain walk and the window were both already the right shape. Tank Attack can have it too. | **14 `.text` + 1 `.bss`** (+279 `.cold`) | **+176 bytes** |
| 6 | **Windowed text mode** (§10). **THE CAPTURE HALF IS BUILT** (SPEC.md 96.34): a program's last text screen becomes the console's buffer at teardown, and the console is seeded onto the program's screen on the way in, so the two are one continuous session. It cost **+245 bytes of image and ZERO bss**, because `con_scr` IS text VRAM's layout — the answer to "where does the text live" was that it already lives there. **The FULL windowed half is deferred behind §14**: output arriving in the band *as it runs*, with the bracket never taking the screen, turns on refusing a `B8000` writer into full screen, and that decision reads differently once `kern_dos` exists. | **0** | **+245 bytes** (image 28,659 → 28,904) |
| 7 | **BUILT.** A command interpreter in that window — and it came out an INPUT, an OUTPUT and a PROMPT with **no verb of its own** (SPEC.md 96.33), because `dosh.inc` was already the interpreter and §96.30 said so when it was written. `apps/os88con.inc` is the screen, extracted from Telnet (§70.8.12); `apps/dos/dosc.inc` is the prompt; `DIR` is the one verb the table was missing (§96.33.4). | **0** | **+3.9 KB image, +6.7 KB bss** (image 23,889 → 27,751, bss 4,367 → 11,186; 23,115 compressed on disk) |
| — | *deferred, needs its own design* | | |
| ? | FCB functions (§6.2) — cost is real, audience is small | 0 | +2 KB |
| ? | DOS 5 rather than 3.31 (§12 q1) — the version byte is free, the functions behind it are not | 0 | ? |
| ? | `kern_small`, launched to a window and a file dialog (§11.1 item 3) | 0 | small |
| ? | **The hibernate phase (§14)** — the whole machine for DOS, ~636 KB, and the session safe on disk. Not scheduled; §14.4 is the only thing waves 1–7 must not box out | 0 or a third kernel | ? |

**Wave 7 came in under its estimate on code and over it on RAM**, and the split
is the thing worth knowing: the +4–6 KB above was a guess at an interpreter,
and the interpreter already existed — what the wave actually spent is a **6,819
byte SCREEN**, which is `con_scr`'s 4,000 cells, the CP437 face's 2,048 and one
640-byte band. That is bss in the package's own segment, so it comes straight
off the arena a DOS program is handed; on a 640KB machine it is 1% of it.

**Wave 1 is the one that decides everything**, and it is worth building as a
throwaway first: a `.COM` that does nothing but `INT 21h AH=09h` (print a
string) and `AH=4Ch` (exit) exercises the arena, the PSP, the bracket, the
vector save/restore and the exit path — every load-bearing piece — in a
program small enough to hand-assemble and read.

### 11.1 The starting decisions — taken

Six things had to be chosen before wave 1 could start. All six are decided;
they are recorded with their reasoning because each will look arbitrary later.

**1. The package is `DOS.O88`.** Not `DOSBOX` — that names a well-known
*emulator* and nothing here is emulated; not `RUNDOS`, because the same
package will eventually be launched with **no argument** and go to a command
prompt (wave 7), and a name built around *running a file* would be wrong for
half of what it does.

**2. It is a `SYSAPPS` package** — `SYSTEM/` on all four system-disk
geometries, on no apps disk. `apps360.img` has too little room to carry a
second copy, and the argument that settles it is the same one §92 made for
`THEWIRE.O88`: a `.COM` can be sitting on any floppy, so the program that
runs it belongs on the disk the machine booted from.

**3. `kern_small` gets it in a wave of its own, and the route is a window.**
SPEC.md 54.0 gates associations out of that kernel, so a double-click cannot
reach `DOS.O88` there — but a package that is *launched* can open a window and
put up the Standard File dialog (`FDLG.DRV`, SPEC.md 38.0, which `kern_small`
has), and the user picks the `.COM` from there. Same package, one extra entry
path, and it is the same entry path wave 7's command prompt needs. Its own
wave because the 128KB machine's arena is the question, not the mechanism.

**4. It claims `.COM` and `.EXE` from wave 1**, before wave 2 can run an
`.EXE`. A double-click then gets a refusal naming the reason instead of the
kernel's *"Bad package"* (§54.4), which is §54.4.1's argument exactly.

**5. The window stays open when the exit code is non-zero**, and closes itself
when it is 0. A program that fails silently in a tenth of a second is the case
that needs a window, and one that succeeds does not. **The better version of
this is deferred and named here so it is designed for**: capturing what the
program wrote to the console and showing it in that window. Wave 6's windowed
text mode is the machinery — once `INT 21h`'s character output can go
somewhere other than the screen, the last screenful is a buffer the window can
paint.

**6. The drive map is the identity, with our hole in it** — §11.2.

**Two things that looked like decisions and are not.** The bracket must call
`OSAPI_FSX_MODE` with `FSXM_TEXT` on entry whatever else it does, or the video
restore is skipped (§4 note 1). And the arena is `OSAPI_MEM_AVAIL` and one
claim (§2.3.1) — there is no sizing policy to pick.

### 11.2 The drive map, and what "invalid drive" means

DOS answers *"there is no such drive"* **differently in every function that can
be asked**, and programs test for the specific value, so there is nothing to
invent here — the shim returns what DOS returns:

| function | invalid-drive answer |
|---|---|
| `AH=19h` get current drive | cannot fail; returns the current drive |
| `AH=0Eh` select drive | sets it anyway and returns AL = the drive count; the error surfaces on the next file call |
| `AH=36h` get free space | `AX = FFFFh` |
| `AH=1Ch` get drive data | `AL = FFh` |
| a handle call on a lettered path | CF=1, `AX = 0Fh` (invalid drive) |
| an FCB call | `AL = FFh` |

**The decision is not those values. It is whether our drive map has a HOLE in
it, and it does.** SPEC.md 18.7.4 reserves volume index 2 — C: — for a hard
disk *whether or not the machine has one*, so a two-floppy machine with a RAM
disk reads **A:, B:, (nothing), D:**. DOS never does that; DOS assigns
contiguously and a program may reasonably walk drives upward and stop at the
first failure. Such a program stops at C: and never sees D:.

Three ways to answer, and the choice is real:

- **(a) Pass the hole through.** C: answers invalid, D: works. Truthful about
  the machine; loses the scan-until-failure programs.
- **(b) Compact the map** — present our volumes as contiguous DOS letters, so
  the RAM disk is C: to the DOS program and D: on the desktop. Enumeration
  works; the letter the user reads off the desktop is not the letter they type
  into the program, and no error message can fix that.
- **(c) Report a drive count that spans the hole** (`AH=0Eh` answering 4) while
  leaving C: invalid — which is (a) with a hint, and helps only the programs
  that ask the count rather than scanning.

**Recommend (a), with (c)'s count.** The letter on the desktop must be the
letter you type, or the feature is confusing in a way nothing can explain
away — and SPEC.md 18.7.4 already took that trade for os8088's own UI, in its
own words: *"a letter that always means the same kind of device is worth more
than contiguity"*. A DOS box inheriting it is consistent rather than a second
decision. The programs it loses fail by **not seeing a drive**, which is the
safe direction.

### 11.3 The current directory — and it is not only ours to fix

`INT 21h AH=47h` cannot be answered truthfully today, because `dsk_cwd` is a
first-**cluster** word and there is no path string anywhere in the tree:
`OSAPI_FILE_HERE` answers a cluster and a volume index, not a name. A program
that calls `47h`, builds a path from the answer and opens it will fail, and
the failure will look like a file error.

**This is not a DOS-box problem.** It is the same missing capability that makes
TANK ATTACK walk the directory tree to reach its own save file, which is
§6.3's Tank Attack number in a second form: the first was the cost of *writing*
a few bytes, this is the cost of *finding* where to write them.

So it is fixed twice, deliberately:

- **The easy way, in wave 1**: one walk of `..` entries at bracket entry
  (`dskw_rt_*` already walks parents), building a path string the shim then
  maintains itself as the program `3Bh`-chdirs around. One walk per session,
  not per call.
- **The correct way, in wave 5** — the write wave, which is already adding
  seek and write-at to the kernel for `INT 21h` and for os8088's own programs.
  A path is the same kind of capability and the same customers want it, so it
  belongs in that design rather than bolted to this one.

---

## 12. Open questions — and the ones now settled

**Settled by the owner, recorded here so the reasoning is not re-derived:**

- **A package, not a module** (§3) — the resident cost was the whole
  objection, and a package has none.
- **Purge and compact for the maximum** (§2.3.1) — and it needs no mechanism
  at all, because `mem_claim` is already a compact-shed-retry loop. No user
  choice, no check box: an `.EXE` with a bounded `maxalloc` gets what it
  asked for and everything else gets the maximum.
- **Load packages low?** Not wave 1 (§2.3). It is a heap change touching every
  package in the tree, and the number that decides it — how many real programs
  ignore what we tell them — is what wave 1 measures.
- **Which BDA bytes come back** (§5.1) — 40 bytes restored in seven spans,
  four zeroed, the rest deliberately left.
- **Write support is not wave 1**, and it is wave 5 rather than "deferred"
  (§6.3, §11).
- **All from scratch**, MIT, with FreeDOS and CuteMouse read when a return
  value is in doubt (§13).
- **Windowed text mode and a command interpreter are waves 6 and 7** (§10,
  §11), not niceties.
- **The six starting decisions** (§11.1): the package is `DOS.O88`, it is a
  `SYSAPPS` package, `kern_small` reaches it through a window and a file
  dialog in a wave of its own, it claims `.EXE` from wave 1, its window
  survives a non-zero exit code, and the drive map is §11.2's.
- **`SOUND.DRV` detaches for the session and re-attaches after** (§9.3) —
  nothing of ours is expected to keep playing.
- **The cwd is walked once at bracket entry in wave 1 and done properly in
  wave 5** (§11.3), because it is Tank Attack's problem as much as ours.
- **The hibernate phase is a real future phase** (§14), and the only thing
  waves 1-7 must do for it is §14.4's back-end indirection.

**Q1. What DOS version should `AH=30h` report, and what does DOS 5 buy?**
The target is DOS 5; 3.31 is acceptable to start. Two things are worth
separating before that is implemented:

- **The version byte is free and the functions behind it are not.** Programs
  branch on it, so reporting 5.00 while lacking DOS 5 services is *worse* than
  reporting 3.31 — a program takes the DOS 5 path and fails at a function we
  answer with CF=1. **Make the reported version a SETTING** (SPEC.md 51.5 — 2
  resident bytes for a whole setting, and this one would live in the
  package's own config rather than the kernel's), so a title that wants 5.00
  gets it without a rebuild and without lying to everything else.
- **"More command hooks" needs pinning down before it is costed.** The DOS 5
  additions that plausibly matter here are `AX=3306h` (true version),
  `AH=4B05h` (set execution state), `AH=58h` subfunctions 2/3 (UMB link
  state), `INT 2Fh AX=4A01/4A02` (HMA), the `AH=6Ch` extended open that
  arrived in DOS 4, and `COMMAND.COM`'s installable-command interface over
  `INT 2Eh`/`INT 2Fh`. **Which of those the owner means is not settled here**
  — several are only reachable once wave 7 exists at all — and the honest
  next step is a survey of what the target software actually calls, not a
  guess at the list.

**Q2. Getting out of a hung program.** The backstop is what DOS's own was: a
reboot. **Ctrl-Alt-Del already works for free** whenever the program has not
taken `int 09h`, because that is the ROM's handler and we have not removed it.

Above that, a real escape is cheap enough to be worth building, in two rungs
and with its limits stated:

- **Rung 1 — the runner's own `int 09h`.** Install over `kbm_isr` for the
  session; read port 60h; on a magic combo restore the runner's saved `SS:SP`
  and jump to the exit path; otherwise chain to whatever the DOS program
  installed. ESTIMATE **40–60 bytes**. Defeated by a program that takes
  `int 09h` itself, which games routinely do.
- **Rung 2 — the same test from `int 08h`.** A tick handler samples the last
  latched scancode directly, which catches a program that took the keyboard
  but not the timer. Defeated by a program that takes `int 08h`, or that runs
  with `IF=0`.

**Why the longjmp is safe is not obvious and is worth writing down**: the exit
path restores the IVT, the BDA list, the PIT, the 8259 masks and the video
mode regardless of how it was reached, so the *machine* comes back consistent
from an abort exactly as it does from a clean `4Ch`. What does not come back
is the DOS program's own cleanup — which is fine, because our `INT 21h` owns
its file handles and can close them itself. The one thing that can defeat both
rungs is a program that scribbled over task 0's stack, where the runner's
saved `SS:SP` lives; there is no defence against that on an 8086.

**Still open:**

- **Q3. Is the raw-mickey pair worth ~10 resident bytes?** (§9.1). The one
  kernel change this document actively recommends considering, and it should
  be costed on its own rather than taken on a sentence.
- **Q4. What is the first-run warning?** A DOS program can take the machine
  down with unsaved work in other windows. A product decision, not an
  engineering one.
- **Q5. Which real programs does read-only wave 1 actually lose?** The
  measurement that sizes wave 5, and it cannot be taken before wave 1 runs.

---

## 13. Sources, and what may be taken from them

CONTRIBUTING.md §6 is binding: **"No vendored third-party code. Everything in
the OS is hand-written and the whole tree is MIT under one license file."**
The `apps/c64` and `apps/apple2` GPL departure was an explicit, stated,
user-decided exception for an *application*, and it is not a precedent for
the kernel or for a system facility.

| resource | licence | what it is good for |
|---|---|---|
| FreeDOS kernel (FDOS/kernel) | **GPL-2-or-later** (checked) | **Behavioural reference only.** It is a whole DOS — its own FAT driver, its own memory manager, its own device-driver model — for a machine where it *is* the OS. We want a shim that maps onto os8088's file system, which is a different shape, not a smaller version of the same one. Read it to settle "what does DOS actually return for X". |
| CuteMouse | **GPL-2** (checked) | Reference for the `INT 33h` function semantics. We do not need a mouse *driver* (§9.1). |
| mTCP | GPL | **A consumer, not a provider** (§9.4). Its real value is as a validation suite: if mTCP's FTP client runs, the shim's file handles and the packet driver are both genuinely working. |
| ISA-PicoMEM redirectors | — | Solving a problem os8088 already solved with `DRVC_FILE` (§9.5). |
| Ralf Brown's Interrupt List | freely usable, non-copyleft terms | **The actual reference to work from** for `INT 21h`, `INT 33h`, the PSP and the MCB layout. |
| Crynwr Packet Driver Specification | a published specification | The interface to implement in §9.4. |

**All of it from scratch, MIT, against RBIL and the published specifications
— DECIDED**, with FreeDOS and CuteMouse read the way one reads a second
opinion: to check a return value when something is stuck, not to supply one.
That is not licence caution for its own sake. The shim's whole job is to sit
on os8088's volume layer, heap and fsx bracket, and none of the code in those
projects knows those things exist — so even with the licences set aside, the
lift would be of the half that does not fit.

---

## 14. The hibernate phase — handing DOS the whole machine

**SUPERSEDED AS A PLAN by [docs/plans/KERN-DOS-PLAN.md](KERN-DOS-PLAN.md),
which is this phase costed.** What is below was written while the phase was
far away, and it is an idea rather than a plan — keep it for §14.1's survey of
what §87 already gives and for §14.4's one decision, and read the other
document for the shape, the 600 KB budget, the waves and what the third arm
loses. Two things the costing found that are not below: **`dos_be_*` is the
whole port** (twenty doors and `dos_load`, nothing above them changing), and
**`kern_dos` should ship as a PART of `DOS.O88`** rather than as a file on a
system disk that is about to come under pressure.

**Not built, not scheduled, and written down now for one reason: there is
exactly one decision in waves 1–7 that could box it out, and it costs nothing
to get right (§14.4).**

The idea: for a program that wants more than the heap can give, **hibernate the
session to the hard disk, hand the emptied machine to DOS, and read the session
back when the program exits.** Hard disk only — SPEC.md 87 already forbids
hibernating to a floppy, and for the same reason.

### 14.1 Most of it is already built, including the hard part

SPEC.md 87 is a working hibernate: it writes **all of conventional memory** to
`HIBERNAT.IMG`, stops the machine, and on the next boot a stub reads the image
back and returns into `ui_task`'s loop with every window, package and task as
they were. Three parts of it are exactly what this phase needs:

- **The stub already lives somewhere the teardown cannot reach.** §87.5 runs
  it out of the **text framebuffer** — *"the one RAM on the machine that no
  rung of §2's ladder owns and every adapter has at least 16KB of"*. That is
  the hard problem of this whole phase, solved, for a different reason.
- **The image is read back by EXTENTS** — absolute LBA and sector count,
  computed from the FAT chain *before* the teardown, so the read needs no file
  system at all. The same trick loads the DOS program.
- **The restore is already a return into a live kernel**, not a boot:
  `hb_wake` (§87.6) re-enters on the restored stack with the gfx lock still
  held, discards the disk caches, reloads the drivers and returns through the
  module's dispatcher into the UI task.

**And it is fast.** SPEC.md 87.7: the image is 1,280 sectors on a 640KB
machine and both directions go out in track-sized runs, so it is **~80 `int
13h` calls each way** against an XT hard disk — "seconds, not minutes". That
figure is an ESTIMATE and SPEC.md 87.7 says so; nothing has measured it.

### 14.2 What it is worth — and the memory is the smaller half

**The memory.** Conventional memory is 638.5 KB above the BIOS data area
(SPEC.md 2). Take ~2 KB for the stub, its parameters and the extent list and
the program sees about **636 KB**, against the **523 KB** measured today
(§2.3.1) — **+113 KB, +21.6%**.

Put beside the machines this software was written for, that is the striking
number: **DOS 3.3 on a 640KB PC leaves a program about 580 KB, and DOS 5 with
`DOS=HIGH` about 620 KB.** So the phase does not buy a narrow band of programs
between 523 and 636 — it buys **more free memory than any real DOS machine
ever offered**, which is "every DOS program that ran on a 640K PC, with no
exceptions to explain".

**The safety, which is the bigger half.** §2.3's honest limitation is that our
containment is DOS's own, and that the difference from DOS is *consequence*: a
DOS machine that gets scribbled on is rebooted, and ours takes unsaved
documents with it. **This phase deletes that sentence.** The session is on the
disk before the DOS program is given a single byte, so a program that
corrupts everything costs the user the program and nothing else. That is a
containment guarantee real DOS never had and that an 8086 cannot otherwise
provide — no MMU required, because the protection is in *time* rather than in
address space.

It also makes §2.3's load-low question moot on this path: there are no
packages in memory to be above anything.

### 14.3 The crux — `INT 21h` needs something underneath it

**The kernel is gone, so the file half of the shim has nothing to call.** Every
file function in §6.2 and §6.3 is built on `OSAPI_FILE_*` and the volume layer;
after the hibernate there is no volume layer. This is the question that decides
the phase's shape, and there are three answers:

1. **A minimal kernel**, which is what the request supposed — a third build in
   `make emu`'s shape (SPEC.md 9.11.7): `int 13h`, the FAT driver and the
   volume layer, with no window manager, no drawing, no scheduler. The file
   system and the window system are 69% of the kernel between them
   (docs/plans/KERN-SMALL-CUT-PLAN.md), so this is not a small subtraction —
   plausibly 30–40 KB, leaving DOS ~600 KB. Most work, most capability, and a
   third kernel to maintain.
2. **The shim carries its own FAT reader.** `DOS.O88` stops being a package for
   this path and becomes a standalone image the stub loads: `int 13h`, FAT12/16
   read, the `INT 21h` dispatcher. ESTIMATE 12–20 KB, leaving DOS ~615 KB. No
   third kernel, but a second FAT implementation in the tree — which
   CONTRIBUTING.md's instincts are against, and rightly.
3. **`int 13h` only, no `INT 21h` file functions.** Cheapest and nearly
   useless: DOS programs reach files through `INT 21h`, not through the BIOS.

**Option 1 or 2, and it is not decidable from here** — it turns on how much of
the file layer can be cut free of the rest, which is a measurement on the
kernel rather than a judgement about DOS.

### 14.4 The ONE thing to get right now, and it is nearly free

**Put every kernel call the shim makes behind an indirection, from wave 1.**

A back-end table inside `DOS.O88` — open, close, read, write, seek, find,
chdir, free-space — with one implementation that calls the `OSAPI_*` slots,
and room for a second that does not. Options 1 and 2 above then both become
*a second back end* rather than a rewrite of `INT 21h`.

It is the shape this system already uses twice: `DSV_BLK` lets `dsk_xfer`
serve a volume through the BIOS or through a driver without knowing which
(SPEC.md 18.7), and `DSV_FS`'s `FSV_*` table does the same for a whole file
system (SPEC.md 51.8). The DOS shim wanting it is the same want one layer up.

**Cost now: a handful of near-call cells and the discipline not to call
`OSAPI_FILE_*` from inside a function handler.** Cost if retrofitted: every
file function in §6.2 and §6.3, which is the largest single piece of the
project.

Nothing else in waves 1–7 constrains this phase. The arena, the MCB chain,
the PSP, the vector and BDA ledgers, the drive map and the `INT 33h` mouse are
all written against the machine rather than against the kernel, and none of
them changes when the kernel goes away.

### 14.5 What else this phase would have to settle

Named rather than solved, so the list exists when it is picked up:

- **A second file name.** A DOS session's image must not be `HIBERNAT.IMG`, or
  launching a DOS program destroys a real hibernation the user is holding.
- **The stub moves out of the text framebuffer.** §87.5's home is exactly
  where a text-mode DOS program writes. The classic answer is the top of
  conventional memory with `0040:0013` reduced to match — which §2.3 already
  patches for a different reason, so the mechanism is there.
- **`hb_wake` without a fresh boot.** §87.6 step 2 takes the clock from the
  boot that just happened, because the RTC ladder is boot-overlay code; here
  there is no such boot, and the clock has to come from `0040:006C` — which
  §5.1 already says to leave alone, so the two fit.
- **Hibernate's own refusals apply unchanged** (SPEC.md 87.2): a BIOS-known
  hard disk (rung 0 — the stub cannot speak rung 1's task file), no extended
  memory held, and room on the volume for `[mem_top]` × 16 + 4,096.
- **A two-card desktop loses its second display** until the next
  `vid_disp_init`, exactly as a hibernate does today (SPEC.md 87.7).
- **Write it as a MODE of hibernate rather than a copy of it.** The difference
  from §87.4 is that the machine does not stop and the stub loads a program
  instead of returning; everything before that is the same code, and a second
  copy of it would drift.

---

## 15. What waves 1 and 2 cost, and the three things they overturned

**Both waves are built and gated.** `SPEC.md §96` is the contract;
`tests/doscom.py`, `tests/dosexe.py`, `tests/dosmouse.py`, `tests/dosfile.py`
and `tests/dosdir.py` are the rows. The real-program check is SOPWITH — a
1984 game whose `.EXE` has no MZ header at all — which reaches its title
screen and plays.

**The bill is zero kernel bytes**, as every row of §11 predicted, and
**+2.7 KB of package** for wave 2 (image 3,134 → 5,878 bytes; 5,321
compressed on the floppy). The `.o88` is on the system disk in `APPS/`, so
none of it is resident on any machine.

Three rows of this document were wrong, and each was wrong in a way worth
keeping.

### 15.1 §6.3's file handles: option (a), plus a rule nobody costed

The recommendation was right — build the handle layer on the published API,
reads working well and writes restricted to create/append/replace — and it
came out at ~1.2 KB against the 2–3 KB estimate, over **one** cluster-aligned
window carved off the top of the arena rather than one buffer per handle.

What the estimate did not contain is **§96.4.1**, and it is the load-bearing
half: **a kernel file call may not run on the DOS program's stack.** Inside
the bracket `SS` is a segment in the middle of the arena; every os8088
context has `SS = LOW_SEG`, and `sch_switch` declines to switch when that
does not hold — right for a short foreign-stack window, not for a
multi-sector disk write. `OSAPI_FILE_WRITE` was **entered and never came
back**: the bracket was torn down, the desktop returned, and the package's
own window said *"The program has finished. Exit code 000"*. Nothing about
that says "stack" from the outside, and the thing that found it was printing
a character through the ROM teletype either side of the far call and watching
the second one never arrive.

The fix is one door that runs every back-end call on the UI task's own stack
— **which is what §14.4's back-end indirection turned out to be worth before
the hibernate phase it was designed for.** There was one place to put it, and
it was already there. It costs 258 of task 0's 510 bytes at the deepest point
of a 20 KB write.

### 15.2 §11.3's `..` walk cannot be built at all

The plan proposed building `AH=47h`'s answer by walking up: ask a directory
who its parent is, then search the parent for the entry pointing back at the
child. **`.` and `..` are not reported to a package**, so it cannot be built.

**The mechanism, written down here so this stops being re-litigated** - it has
now been doubted twice, and the second time was by a reader who found a source
comment elsewhere in the tree claiming the opposite:

`dsk_find_x` (kernel/disk.inc) filters the raw directory sectors, and four
lines into its entry loop it has `cmp al, '.'` / `je .skip` - *"the on-disk dot
links (SPEC.md 19)"*. Both cells go through it: `api_file_find` and
`api_file_find_raw` join at `api_ff_fence` and differ in **the size field and
nothing else**, so the RAW cell is not a way round it either.

`OSAPI_FT_UP` is in the SDK's type list because `dsk_synth_up` builds an
up-entry for `disk_mount`'s **LISTING** (SPEC.md §19.5) - a different structure
a package cannot reach. Nor is there another way in: `OSAPI_ARG_FILE` hands
over a name, a **cluster** and a volume, and a cluster is not a path.

**`apps/ftpd` reached the same conclusion independently and solved it the same
way**, which is the strongest evidence available that this is the shape of the
problem and not an oversight. Its `FD_CDMAX` comment states the finding in as
many words - *"CDUP NEEDS A STACK BECAUSE THE KERNEL CANNOT ANSWER IT"* - and
its walker handles `..` by stripping the last component of a path stack 16
levels deep, never by asking the kernel. So two packages have now each built a
descent stack for want of one kernel answer, and **that, rather than "can it be
done", is the question a path wave should be costing.**

So the walk was abandoned and **the launch directory became the program's
root** (SPEC.md §96.12.2) — self-consistent, exactly round-tripping, §96.6's
containment one layer up, and **cheaper than the thing it replaced**: the path
is maintained by `AH=3Bh`, the only call that can move us, so `AH=47h` is a
string copy rather than one floppy mount per directory level. The version
this document asked for would have been slower *and* wrong.

### 15.3 §9.1's mouse: the mickey pair was not the interesting half

§11 costed `INT 33h` at "0 kernel bytes, 10 if the mickey pair is taken", and
the pair was **not** taken — `AH=0Bh` is derived from the position and loses
what the hand spends against a screen edge, which is named in SPEC.md
§96.10.2 rather than fixed.

What the row did not see is that **functions 5 and 6 need edges**, and a
handler that only runs when the program calls it sees only the transitions
its own polls straddle. Answering `0` presses would have been honest and would
have broken the common case — a program whose entire click detection *is*
function 5. The counts are accumulated on every state read, and `dos_getkey`
stopped being `int 16h AH=00h` and became a poll around it, so *waiting for a
key samples the mouse too*: "press a key or click" is a prompt DOS programs
write, and `AH=00h` is itself a spin on the BIOS buffer's head and tail, so it
costs a machine that has already borrowed the screen nothing.

### 15.4 Still open in waves 1–2's own scope

**Two of the four rows this section carried are BUILT and were left saying
otherwise**, which is worth a line of its own: a plan's open list goes stale
in the safe-looking direction, and a reader planning the next wave from it
would have costed work already done. Checked against `apps/dos/dos.asm`'s
dispatch rather than against memory.

- ~~**Date and time**~~ — **BUILT**. `AH=2Ah`/`2Bh`/`2Ch`/`2Dh` all dispatch
  (`.getdate`/`.setdate`/`.gettime`/`.settime`).
- ~~**`AH=43h` attributes**~~ — **BUILT** (`.getattr`).
- **`45h`/`46h` dup** — not built, and still not wanted by anything measured.
- **FCB functions** — already deferred in §11 and still deferred.
- ~~**`3Dh` modes 1 and 2**~~ — **BUILT** (SPEC.md 18.4.7, 96.11.6). It was
  carried here as *"the one still-open item likely to stop a real program
  next"* and as a kernel question, and both were right: `OSAPI_FILE_WRITE_AT`
  is the slot, and it came in at **15 bytes of the 64KB segment window** (an
  8-byte cell, a 6-byte thunk and one `.bss` byte) with the body cold.
  **The seam is what made it small, and it is worth knowing for the next one
  of these**: `dsk_read_chain` is direction-agnostic — its run coalescing,
  corruption tests, progress widget and resume point are all about the CHAIN,
  and the only read-specific instruction in it is one `call` inside `.flush`
  — while `disk_read_x` and `disk_write_x` differ by one stored byte and then
  share `dsk_xfer`. On the package side `dos_fh_fill` already positions the
  window cluster-aligned over the handle's position, so an in-place write is
  `dos_fh_rdloop` with the copy reversed and the read-modify-write is free.
  §22.24 and §22.25 did NOT answer it, as this entry said — whole files say
  nothing about writing inside one.

  **The row grew twice after it was called built**, each time from a question
  about what DOS actually does rather than from a failing test — which is the
  useful part of the record. *"Will our DOS connection switch over to append
  as needed?"* found that a `3Dh` handle could not GROW its file at all, and
  the two halves compose exactly: `WRITE_AT` reaches the end of what is
  allocated, which is precisely where the size becomes a cluster multiple,
  which is `APPEND`'s own precondition (96.11.6, +128 package bytes). Then
  *"why would anything seek past the end?"* — three ordinary answers, and a
  seek past the end now LAYS the gap instead of refusing it (96.11.6.1, +138
  package bytes, four of instance `.bss`, and the kernel byte-identical). Both
  are **package-side and free to every machine**, because the kernel slot the
  first one bought turned out to be the only kernel work either needed.

  **`CX=0` was the third question and it SPLIT** (96.11.6.2). Writing zero
  bytes is how DOS spells *"the file ends HERE"*, and the table was measured
  on a real IBM DOS 3.30 and on this box with one `.COM` run unchanged on both
  (`tests/dostrap/cx0.asm`): DOS moves the length in **either** direction and
  this box moved it in neither, **both answering `CF=0` with `AX=0`**, so the
  program could not tell. The EXTENDING half is now built and cost **26
  package bytes** — it is 96.11.6.1's gap with the data write empty, and all
  it needed was the gap test moved to the top of the loop, where a count of
  zero has not yet returned.

  **The TRUNCATING half is BUILT IN THE PACKAGE, and the kernel slot is
  COSTED AND REFUSED** — §15.12 below is the costing, kept because the refusal
  is a judgement somebody may want to revisit rather than a fact. It shrinks
  by copying the kept prefix out under a temporary name and swapping the two:
  **336 package bytes, four of instance `.bss`, nothing resident**, over five
  doors that were all already here.

### 15.5 Wave 3, and the row that turned out to be a door rather than code

**All four rows are built** and gated. Three of them cost no kernel byte —
`AH=4Bh` (SPEC.md §96.14), XMS (§96.15), and `INT 12h`/BDA, which wave 1 had
already done without noticing: the BDA's memory-size word is written at
bracket entry and the ROM's own `int 12h` reads exactly that word, so the
polish this row asked for was a consequence of §96.3's containment rather than
work of its own.

The fourth is the Sound Blaster, and it is **the only row in this entire plan
that spent a resident byte**: `OSAPI_DRV_SUSPEND` (§51.11), 209 bytes of
`.text`, 5 of `.bss` and one API cell. What follows is why the shape changed,
because the first reading of this row was wrong twice and both errors are the
kind worth writing down.

**Error one: it was called refused, and §9.3 was wrong about why.** §9.3 called
detach "mostly existing code", and the code does exist — `sbl_detach`,
`sbl_halt`, `sbl_unhook`, all with the right comments on them. What did not
exist is a **door**: `SOUND.DRV` publishes no `DSV_PKGCALL`, so
`OSAPI_DRV_CALL` refuses and no package on any floppy can reach it; and
`OSAPI_SND_CAPS` answers capability bits and "is a driver loaded", not the
card's port, IRQ or DMA, so even `BLASTER=` could not be built. That part
stands, and it is exactly why the answer came out a **kernel slot** and not a
driver verb — a package asking a driver to stand down is a package reaching
round the kernel's own record of what is attached.

**Error two, and this is the one that made the row look skippable:**
*"`SOUND.DRV` is not mounted unless `SYSTEM.CFG` asks"*. It is false.
§51.3.1's boot sniff runs an OPL timer dance at `MARK 25` and sets the sound
row's want bit when a chip answers, so **a machine with a card and no
`SYSTEM.CFG` at all mounts the driver** — which is the common case, not the
configured one. The row was not a corner; it was the ordinary path.

The second half of the old refusal — *"sending `DRVV_DETACH` behind the
kernel's back would be wrong"* — was right, and is what the built answer is
made out of. The kernel does not put a driver to sleep, because a dormant
state is one every driver would have to grow and none has: it **unloads**
them, at the bracket, and **loads them back** at the other end. `drv_unload`'s
own wait on `[drv_wcnt]` is what makes that safe, and it is already built.

**The `TF_SERVICE` hazard was real and is what the design turns on.** A loaded
driver's refill worker keeps running inside an fsx bracket by design (§53.2),
so a mounted `SOUND.DRV` can feed the DSP while a DOS program is resetting it.
That is a second exception beyond the kept worker, and it is not fixable by
asking the driver to be quiet: `drv_svc` is a **copy** taken at attach, so a
driver clearing its own service table changes nothing the kernel reads and
`DSV_TICK` is still far-called from IRQ0. Unloading is the only thing that
removes both the feeder and the vector, which is why the slot is shaped the
way it is.

What the row actually needed, in the end, was **one fact the driver had and
nobody could ask for**: the card's base port, IRQ and DMA. That is
`DRVV_HWINFO` (§51.11.2), four lines in `snd_entry`, and it is what turns
`BLASTER=` from guesswork into a report.

### 15.5.1 ...and what a real Sound Blaster program then showed

The wave was validated against **Creative's own** `TEST-SBC.EXE` (Sound
Blaster 2.0, v1.81, 1991): a 42KB Microsoft C `.EXE` that relocates itself and
walks the MCB chain. It loads, runs, finds the card at 220h, exits cleanly —
and the window afterwards carries **no unsupported-function line**, so every
`INT 21h` it made was answered. `SOUND.DRV` went `9E80` → `0000` on the way in
and back on the way out, which is the wave's whole claim, seen from outside.

It reports a failure of its own at its interrupt-detection stage. **That is
not evidence about the bracket**, and the reason we can say so is `tests/
dosirq.py` (SPEC.md §96.18), written because of it: a program of **ours**
resets the DSP and reads its version back, hooks `INT 0Fh`, unmasks IRQ7 and
takes exactly one interrupt from DSP command `0F2h`, then runs a 256-byte
transfer on channel 1 of the 8237 and takes exactly one completion. Three
right answers to the three questions the third-party program's verdict was
being read as evidence about.

**The lesson is the general one and it is worth carrying forward**: a
third-party program is a good way to *find* a question and a bad way to
*answer* one, because its internal verdict is a number we cannot read. Two of
the earlier test programs the owner supplied turned out to need a **386** —
`FMLR.COM` has a `0F 84` near `jz` 33 bytes past its entry and `PCMPLAY.COM`
has a 286 `C1 EB 04` at 16 and the same near `jz` at 23 — which on an 8088 is
`POP CS`, so both popped the PSP's zero word into CS and ran into segment 0.
That looked exactly like a loader bug for as long as nobody disassembled the
entry path. `ndisasm` from the entry point, by eye, cost five minutes and a
byte census over the whole file cost longer and said nothing: the counts
tracked file size, which is the signature of data.

**None of these programs are in the repository and none can be** — they are
Creative's and the respective authors' work. Everything the tree asserts is
asserted by `tests/dosirq/irq.asm`, which is ours.

### 15.6 What wave 3 left, in the order the evidence ranks it

The two items this section used to head with are **both built and gated**, so
they are recorded here rather than listed: the XMS working path now has its
QEMU twin (`tests/dosxmsq.py`, SPEC.md §96.15.3) and the Sound Blaster row is
`OSAPI_DRV_SUSPEND` (§15.5). What is left is smaller than either.

1. **`4Bh` nesting**, refused in §96.14.1 for want of a stack rather than for
   want of a reason. Wave 7's command interpreter does not need it (a shell
   runs one child at a time); a batch file that calls a batch file does.
2. **Handles are not inherited by a child** (§96.14.2), which nothing measured
   has minded and which the one-window design of §96.11 would not survive
   being made per-PSP.
3. **`DRVV_HWINFO` has one implementer.** `SOUND.DRV` answers it; every other
   driver in the tree does not, and `drv_suspend_x` neither needs nor asks.
   The slot is published (§51.11.2) so that the next driver whose hardware a
   fullscreen program wants to name can answer it, and until one does the
   contract is a table with one row in it. That is honest rather than
   speculative — but it is also why the verb number should not be treated as
   settled by use.
4. **A suspended driver is unloaded, not paused, so a bracket that takes long
   enough is a mount away from the disk.** `drv_load` reads the `.DRV` back
   off the boot volume at the resume, which is fine on a machine that still
   has that volume in the drive and is a refusal on one that does not. The DOS
   box's own resume path treats a failed reload as "the driver is gone" and
   carries on, which is the only answer available inside a bracket, but it
   means **a user who swaps the system disk during a DOS program loses the
   sound driver for the session**. Nothing measured has hit it; it wants a
   sentence in the user-facing docs before it does.

### 15.7 What wave 4 cost, and the three things it overturned

**BUILT** — SPEC.md 96.23 is the contract, 72.22 the driver half, and
`tests/dospkt.py` the gate. mTCP's own `PKTTOOL` and `PING` run unmodified.

**1. §9.4 costed the wrong half, and the wave table's "0 kernel bytes" was
right by luck.** Both of that section's premises were false (corrected in
place above): there was no transmit verb to call, and the box was unloading
`ETHER.DRV` at the bracket. What that actually cost is small — **4 bytes of
kernel `.text`** for the `DRVC_NET` skip, exact, plus three verbs in a
driver, whose bytes are never resident. But it was found by reading the verb
table, not by reading the plan, and no estimate in §9.4 would have produced
it. **A section that costs a wave against a driver's design rather than its
published surface is costing a guess.**

**2. The interesting cost was MEMORY, and it moved twice.** A packet driver
wants a 1,514-byte frame buffer each way, and as package bss that is 3,028
bytes zeroed into the heap claim at every launch on every machine — including
every machine with no card, where nothing can read them. It is a 2KB heap
claim now, taken only when a card answers (SPEC.md 96.23.7), and the transmit
half turned out not to be needed at all: `NETV_RAWTX` takes a segment, so the
client's buffer goes down where it lies (96.23.8). **bss +592 instead of
+3,616**, and the staging copy that went with it was a second copy of 1.5KB
per frame on a 4.77 MHz machine.

That claim **cannot be lazy**, which is the one thing here that wants §96.3 to
change: the arena takes `OSAPI_MEM_AVAIL`'s whole answer, so a claim on the
first `access_type` is always refused and the buffer has to be taken in front
of the sizing call. A machine with a card that runs `EDIT.COM` therefore still
pays the 2KB.

**3. The debugging lesson is the one this project keeps paying for, and it
cost most of a session.** Every counter inside the guest said the transmit
path worked — `eth_nrawtx` 1, `CF` clear, the right length, the card really
transmitting — and the frame on the wire was 42 bytes of `dos_save_machine`.
**A frame that is SENT is not a frame that is RIGHT**, and nothing in the
guest was ever going to say so; `ETHDUMP=`'s pcap was the only instrument that
could, and it should have been the first one reached for rather than the
fifth.

The cause was one fact with three symptoms that looked unrelated (SPEC.md
96.23.9): `driver_info` is *defined* to return `DS:SI`, so the probe ran with
the driver's segment as its own from its first call — which made its strings
print as garbage, made `send_pkt`'s frame read out of our image, and made
`access_type` bank an ethertype of two bytes of our code, so no arriving frame
matched a handle and the receive path looked broken while being correct
throughout. mTCP saves `DS` around those calls; our probe did not.

**Still open, and small:** promiscuous mode is refused (SPEC.md 96.23.10) —
the NE2000 can do it and only a sniffer wants it; `4Bh` nesting and handle
inheritance are §15.6's items and untouched by this wave.

### 15.8 Wave 5's first finding, and it was not in the write path at all

The write wave opened on Prince of Persia's `INSTALL.EXE`, which — once
§96.27 gave it `AH=36h` — got as far as selecting C:, making `\PRINCE`,
standing in it, and then stopping dead with *"Please insert Prince of Persia
Disk in drive B:"*. The message names a floppy; the program was looking at the
hard disk.

**A comment that was true when it was written, one section away from the
change that made it false.** `dos_fh_name` dropped a drive letter under
*"a program that names its own drive is naming ours"*, which is correct for a
box with one volume. §96.6.1 made `AH=0Eh` really switch drives three waves
later, and nothing re-read the line above it. SPEC.md 96.6.2 is the fix and
`tests/dostrap/drvname.asm` the measurement.

**What makes this class expensive is that it is a confident wrong answer, not
a refusal** — the same shape as §96.22's `AH=44h` and §96.21.8's `AH=36h`, and
the third time this plan has met it. A search of A: came back with B:'s own
directory and `CF` clear, and a search of `C:` came back successfully **on a
machine with no hard disk**. There is no instrument inside the guest that can
see that: the trace ring records the name *after* the strip, the registers are
what a working call returns, and the program is the only thing that knows it
asked about a different disk. The probe asks by running the pattern and
printing which file came back, and the reference is a machine.

**Three self-inflicted bugs on the way in, and the shape of two of them is
worth keeping.**

1. `dos_drv_sel` mounts through `dos_be_goto`, whose first act is to write
   `[dos_betgt]` — so setting the back end's target and *then* switching
   volumes ran every cross-volume READ as a directory goto. It returns, so the
   caller read a byte count out of whatever was left in `DX:AX` and called the
   file empty.
2. `dos_fh_fill` carries the file OFFSET in `AX`, four lines below where the
   volume seemed like a natural thing to read. `mov al, [si+FH_VOL]` ate its
   low byte — and **`FH_VOL` is 0 for A:, so the cross-drive arm under test
   read perfectly and every ordinary read on B: came back empty.** The new row
   passed its own headline assertion while `dosfile` went red. A fixture whose
   value is zero is a fixture that tests nothing, and it chose the arm that
   mattered most.
3. `BP` is the `INT 21h` frame in this file and `[bp]` is the program's own
   `DS`; it is not a scratch register anywhere below the gate.

**A handle here is a NAME, which is the part that needed real design.** It is
re-resolved at every window, so a copy off B: onto C: would read the
destination back into itself; `FH_VOL` binds each handle to its volume and the
bracket lives in `dos_fh_fill` and `dos_fh_flush` rather than at the handler,
because `dos_fh_take` flushes **another** handle's window on the way past. A
find walk needed the same treatment in `DTA_VOL`, `AH=4Fh` being handed
nothing but the DTA.

**+264 bytes of package image and 16 of package bss; no kernel byte.**

### 15.9 ...and then wave 5 ran out of INT 21h to implement

With the drive letter reaching the drive it names, `INSTALL.EXE` gets to
*"Currently copying 'Prince of Persia Disk' to C:\PRINCE"* and stops. The
trace is four lines (SPEC.md 96.29) and the answer is not a missing call:

```
4E findfirst  "B:Prince.exe"   -> AX=0000 CF=0    §96.6.2 working
29 parse-FCB  AX=2901          -> AX=0001 CF=1    unimplemented
29 parse-FCB  AX=2901          -> AX=0001 CF=1
4B exec       AX=4B00          -> AX=0002 CF=1    file not found
```

**It shells out.** `COMSPEC\0/c\0command.com\0` sits in its data segment
beside `'copy '` and `'del install.exe > NUL'` — Microsoft C's `system()`
verbatim — so the two `29h` calls are it building the child's FCBs and the
`4Bh` is it trying to run `COMMAND.COM`. **Our answer of 2 is correct**: there
is no shell on this machine and the environment does not claim one.

So the write wave's own scope is finished for this program before its write
path was ever reached: nothing it asked for is unanswered, and what it wants
next is a *program*. §96.29 records the door's shape — a one-shot `/C` shell
with `COPY`, `DEL` and an exit code, over an `AH=4Bh` that already works — and
deliberately does not propose it.

### 15.10 ...and then the shell was built, and the installer finished

§15.9's wall is gone and **the write wave's own goal is met**: `INSTALL.EXE`
runs to completion and Prince of Persia is on the hard disk, verified on the
destination volume rather than from the exit codes — `C:\PRINCE` holds all 28
game files at byte-exact sizes and `INSTALL.EXE` has deleted itself.

What made it small is the thing §96.29's sketch did not have: **there is no
`COMMAND.COM` on the disk.** `AH=4Bh` recognises the name and runs a built-in
itself, so nothing is loaded, no arena block is taken, and a program may shell
out as often as it likes. SPEC.md §96.30 is the contract and
`apps/dos/dosh.inc` the implementation; the cost is **zero kernel bytes** and
`dos.o88` +2,751 image / +867 bss.

**Three findings from it, in the order they will matter again:**

1. **Inside the fsx bracket the heap is the DOS program's**, so
   `OSAPI_FILE_COPY` answers `FERR_FULL` — measured, every `COPY` failing with
   6 while every `MOVE`, `REN` and `DEL` passed, because only the copy needs
   memory. The buffer comes from the DOS arena instead, where DOS itself takes
   it. §5's *"an exclusive fullscreen program cannot reach the RAM the drivers
   gave back for it"* is the same finding one layer out, and the sound
   driver's ~14 KB is the better source on the machines that have it
   (§96.30.6).
2. **The read-ahead cache is not an alternative**, and it looks like one. A
   copy's payload never enters it — §18.95.1 skips the fill precisely for a
   request that covers the whole chunk — so there is no second buffer to
   avoid, and staging through it would undo the gate that keeps a streaming
   copy from flushing the directory chunks.
3. **The last bug was the MACHINE.** `os8088_xt_hdd` has 40-cylinder drives,
   so eleven files of the 720KB source were out of reach; §96.11.4 turned that
   into end-of-file, the copy silently truncated four files and then failed on
   the fifth. `os8088_xt_hdd_720` now exists because no profile had both a
   hard disk and an 80-cylinder drive, which is why this trap keeps landing.

**`AH=29h` was implemented anyway, and the reason is worth keeping**: a wrong
answer is worth more to remove than a missing one. It is the fourth time in
this plan that the defect was §96.22's shape — a call answered with the wrong
*kind* of thing rather than refused — and this one told a program that
`PRINCE.EXE` contained wildcards. Eleven inputs measured against IBM DOS 3.30
(`tests/dostrap/parsefcb.asm`), and **four of them could not have been
guessed**: wildcards expand to `?`, an invalid drive answers FFh *and still
writes its number*, the name is upper-cased, and a PATH advances `SI` by two
and leaves the name blank.

**Two mistakes on the way in, both about scope rather than logic.** A global
label placed beside the new routine re-scoped every `.local` in `dos_int21`
below it and the whole dispatch stopped resolving — the hard rule about label
hygiene biting *inside* one routine. And `SI` is returned through the gate's
banked slot, which already holds the SI the program came in with, so the
advance is **added** and not stored: storing it left every caller's pointer at
the same low address, with every other column correct.


### 15.11 ...and then the console was built, and two OLD defects came out with it

Wave 7 landed as SPEC.md §96.33 and §70.8.12–13. **The interpreter already
existed** (`dosh.inc`, written for `AH=4Bh` at §96.30) and Telnet already had an
80x25 console, so the wave is mostly two *lifts* — `apps/os88con.inc`, which is
Telnet's terminal made into a shared include, and `dosc.inc`, which is only
input, output and the prompt. Everything after that was field reports, and the
three that cost most were all the same shape: **a thing COMMAND.COM answers
BEFORE its table**, which a verb table has no row for. A bare `B:` is a drive
change (§96.33.6). A bare name with no extension is a SEARCH, `.COM` then
`.EXE` (§96.33.7). And `VER` says what this actually is rather than borrowing
two other companies' names.

Two older defects surfaced because the console is a **second door into the
package**, and neither is wave 7's:

1. **`[dos_fhome]` was initialised inside `OSAPI_ARG_FILE`'s success arm**
   (§96.6.3.1). §96.6.3 had already fixed this once — a sentinel meaning *no
   drive* cannot be `.bss`'s zero, because zero is A: — and the console reaches
   the package down the `.idle` arm, which skipped the one store. So a program
   started by typing its name ran with the box believing it had come from A:,
   asked `AH=19h` which drive it was on, was told the wrong one, and printed
   *"Please insert Prince of Persia Disk 1 into Drive A:"*. **The same symptom
   §96.6.3 is named after, through the door that skipped its fix.** Visible
   without a game: run anything off B: and the prompt comes back `A:\>`.
   The rule it makes is worth more than the byte — *an initialiser that only
   one entry path executes is not an initialiser* — because the second door is
   always added later, by somebody reading the arm they are adding.

2. **The memory page's two figures were the same number**, and so was what a
   launch got (SPEC.md §66.10.4). That one is not in this package at all: §66.10
   rests on *"a cache is genuinely unmovable"*, §18.95.7 withdrew that sentence
   when it took the 64KB page head off `MEM_P_DIRW`, and `mem_cp_plan` reaches
   `mem_cp_drop` only from its `.pinned` arm — so a cache that can move is
   moved, never dissolved, and `OSAPI_MEM_AVAIL_LVL` answered identically at
   every rank. §50.6.6's floor, which this box is the only consumer of, had
   never once done anything. Fixed with a third walk mode (`mem_cp_room`) for
   **+18 bytes of `.text`**, and the DOS arena on a 640KB machine goes
   **453K → 485K** when the box is told to take the cache.

   **The failed first attempt is the part to keep.** Making the *plan* dissolve
   what the *run* packs is the obvious spelling and it **hangs the machine**:
   `mem_compact`'s termination argument is that the plan and the run count the
   same movers, so a plan that counts a dissolve the run declines to make
   answers *"I did work"* for ever and `mem_claim`'s retry loop never exits.
   Measured as a 485KB claim on a 530KB heap with `[dos_arena]` still 0 after
   600 guest seconds — a hang, not a slowdown, and the comment that says why
   was already in the file.

**And one instrument trap is now written down rather than re-learned**
(docs/DOS-DEBUGGING.md, *Traps*, first entry). A `DOSTRACE` build claims 21 KB
of heap for its ring, **before** the arena, so a program near the edge refuses
under the tracer and runs perfectly without it. That had been mis-diagnosed as
*"the box is short of memory"* **eight** separate times, with the cost written
down in three places each of which was read *after* the wrong conclusion — so
`tools/os88dosdbg.py` prints it on stderr at the top of every `trace` and
`build` now. A line the run emits cannot be left unopened.

### 15.12 The `OSAPI_FILE_TRUNC` option — measured, and REFUSED for now

**SUPERSEDED: a truncate SHIPPED, at 106 bytes and not 285** (SPEC.md
18.4.7.5). The second consumer this section asked for arrived — streamed
Compress (SPEC.md 22.22.5) writes past a cut it cannot know in advance — and
the door that fit was not a slot of its own but `OSAPI_FILE_WRITE_AT` with a
count of 0, which is DOS's own spelling and which that body was already
refusing *after* doing every part of the lookup a truncate needs. So the 271
bytes below were mostly a second copy of `dskw_wabody`'s front, and the 14 of
cell and thunk are not spent at all. `kern_big` only, like the door. **The DOS box
uses it** (SPEC.md 96.11.6.2): the new end rounded down to a cluster, the file
cut there, the kept part of that cluster appended back — 52 package bytes, no
temporary and no free space - and on `kern_small` or a redirected volume the
cut refuses before touching the file and the copy below still runs. The
costing below is kept as written.

`AH=40h` with `CX=0` is *"the file ends HERE"* (SPEC.md 96.11.6.2), and the
shrinking direction is the one thing in the DOS box's file layer that no
published slot can do: `OSAPI_FILE_WRITE_AT` grows a file to the end of what
it has allocated, `OSAPI_FILE_APPEND` grows it further, and **nothing on this
machine can make a file smaller.** Neither can any of our own programs — a
Note Pad or a Word that saves a shorter document rewrites every byte of it,
and always has.

**So the slot was written and measured rather than argued about.** The body
sits beside `dskw_wabody` and borrows its whole shape (mount, `DVK_FILE`
refusal, `dskw_name83`, `dskw_find`, `dskw_pmask`), then: refuse a new size at
or above the current one; `ceil(new / cluster)` clusters to keep; walk to the
last one kept; **store the directory entry FIRST** (§18.4 — a crash after it
leaks clusters, where a crash before it would leave the entry naming a chain
that had been freed); mark that cluster EOC; `dskw_free_chain` from its old
successor; flush. `dskw_free_chain` and `dskw_setfat` already exist, and
`dskw_dbody`'s delete is the same commit order one step simpler.

Built into the kernel with and without it **at one commit**, and read off
`kernsize`'s sections line rather than a rung:

| | |
|---|---|
| `.cold` | **+271** |
| `.text`, `.bss` | **byte-identical** — every scratch word it wants (`dwr_off`, `dwr_clb`, `dwr_end`) is `WRITE_AT`'s, and the two cannot be in flight together |
| published as a slot | **+14 `.text`** — an 8-byte cell and a 6-byte thunk, exactly what `OSAPI_FILE_WRITE_AT` cost |
| the DOS package | ~70 for the door and the shrink arm |
| **resident total** | **285 bytes** |

It crossed the `.cold` rung, which had 308 bytes left when the measurement was
taken — noted because whoever revisits this should have the fact, not because
it decides anything (CLAUDE.md's byte rule).

**It is refused on the ledger and not on the merits.** 285 resident bytes on
every machine for ever, so that a DOS box can answer one call the way DOS
does, is a bad trade *while there is a package-side answer* — and a DOS box
that grows the kernel two hundred bytes at a time is how a machine that boots
on 128KB stops doing so. What shipped instead is `dos_fh_shrink`: the kept
prefix copied out under a temporary name and the two swapped, **336 package
bytes and four of instance `.bss`**, over five doors that were all already
there (read-at, write, append, delete, rename). It is measurably worse in two
ways the slot would not be — it needs the kept prefix's own size in FREE SPACE
and it copies every kept byte, so truncating a 200KB file is a 200KB copy
where DOS rewrites one directory entry — and both are absent from its two
cheap arms (nothing kept, and a prefix that fits the window).

**What would reopen it** is a second consumer. The moment anything else on
this machine wants to shorten a file — a text editor saving over a longer
document, a log that rolls, the FTP server's `ALLO`/`STOR` — the 271 bytes
stop being the DOS box's and start being the file system's, and the package
copy stops being the cheaper answer. It is written here rather than in SPEC.md
for that reason: SPEC.md says what the machine does, and this is a price tag
somebody may want to pay later.
