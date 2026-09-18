# THE PATH WAVE: one answer, three customers, and the trap that makes it quadratic

**STATUS: COSTED, NOT BUILT.** Everything below is read out of the tree rather
than measured on a machine; the one number that needs a 4.77MHz 8088 is named
in §6.

It exists because the same question has now been fought three times in three
places, and each time the answer was *"build a stack"*.

---

## 1. What the kernel will not tell a package

**A package cannot find out where it is standing.** `OSAPI_FILE_HERE` answers
a **cluster** and a volume index, and a cluster is not a path.

Walking up is refused at the source, by design: `dsk_find_x` filters the raw
directory sectors and four lines into its entry loop has

```
    cmp al, '.'
    je .skip                    ; the on-disk dot links (SPEC.md 19)
```

so **neither `.` nor `..` is ever reported**. Both cells go through it -
`api_file_find` and `api_file_find_raw` join at `api_ff_fence` and differ in
the size field and nothing else. `OSAPI_FT_UP` exists because `dsk_synth_up`
builds an up-entry for `disk_mount`'s **LISTING** (§19.5), a different
structure a package cannot reach.

The kernel has no such problem: `dsk_dotdot_x` reads a subdirectory's first
sector and takes entry 1's `FstClusLO`, range-checked. §19.2 states the
consequence as a design property - *"going up needs no path stack and no
memory of how the user got here: the disk itself records the parent, and that
is why there is no path string anywhere in os8088."*

**True of the kernel. Not true of anything else, and that is the gap.**

## 2. Three customers, each of whom paid separately

| customer | what it built | what it cost |
|---|---|---|
| `apps/ftpd` | `FD_CDMAX`, a 16-level descent stack, because `CDUP` needs one | its own comment: *"CDUP NEEDS A STACK BECAUSE THE KERNEL CANNOT ANSWER IT"* |
| `apps/dos` | the launch directory became the program's root, path maintained by `AH=3Bh` | DOS-EXEC-PLAN §15.2: the walk *"was abandoned"*, and `AH=47h` answers a string it keeps itself |
| `apps/tank` (and docs/plans/NAV-COST-PLAN.md's other sites) | walks `SYSTEM` -> `APPDATA` on every save | ~6 seconds, measured by the fork owner |

Three independent arrivals at the same shape is the strongest evidence
available that this is the problem's actual shape and not an oversight. It is
also the argument for a slot: **the kernel holds the answer and each package
is paying to re-derive it.**

## 3. The cost is NOT what it looks like, and this is the section to read

The instinct is that a walk costs one disk revolution per level, so that four
levels is seconds and the design needs a cache in front of it. **Checked, and
it does not.** Every part is already answered from memory:

- **"Is this the same disk?" is asked ONCE PER MOUNT, not once per level.**
  `[dsk_sigcur]` - the position-sensitive sum of the boot sector (§18.8.2) -
  is computed by the mount that already read that sector. Nothing in a walk
  recomputes it.
- **A same-volume move is a WORD.** `OSAPI_FILE_GOTO_Q`/`_QM` inside the
  volume you are on is *"a WORD, no I/O at all"* (§19.2.2); across volumes it
  keeps the BPB and the FAT window.
- **The no-op test reads the BIOS MOTOR BIT.** `dsk_here_ok` (§18.9.1) does
  not merely compare: it reads `0040:003F` and uses a still-running motor as
  **physical evidence** that the floppy has not been swapped. That is a memory
  read, and it is a stronger test than a cached signature rather than a weaker
  one. **But read §3.2 before believing it covers a walk - it does not.**
- **Directory sectors come out of a CACHE.** §19.2.3's window - `MEM_P_DIRW`,
  16KB purgeable, eight runs, keyed on volume + `[dsk_sigcur]` - answers the
  second climb of a `..` chain from memory. `dsk_dotdot_x` reads through it.
- **`inst_vol_enter` is free when nothing moved**: six compares, and a mount
  *"is paid only when something really did move the volume underneath this
  app."*

So os8088 already has DOS's three speed mechanisms: a resident per-volume
parameter block (the BPB + FAT window survive a quiet mount, as a DPB does),
a walk that never re-validates mid-operation, and `BUFFERS=` (§19.2.3).
**There is no missing infrastructure.**

### 3.2 ...and the LBA 0 claim that MEASUREMENT KILLED

**This section used to say that an unbracketed walk re-reads LBA 0 at every
level. `tests/pathcost.py` measured it and it is false**, and the correction
is kept in place because the reasoning was careful and still wrong, which is
the kind worth being able to recognise again.

The reasoning was: `dsk_here_ok` can only answer "no-op" when the caller is
ALREADY STANDING at that exact cluster - true - a walk moves every level, so
it never is - true - therefore every level reaches `disk_mount`, and a floppy
mount outside §18.9.3's batch bracket re-reads LBA 0. **The last step does not
follow.** Six same-volume `OSAPI_FILE_GOTO_QM` calls measure at **0 reads and
0 sectors**, exactly as §19.2.2's first sentence always said: *"inside the
volume you are already on it is a WORD, no I/O at all."* The boot-sector
re-read the batch bracket elides is per **volume switch**, and a path walk
stays inside one volume.

So the batch bracket is **not** part of this design, `OSAPI_BATCH_BEGIN` is
not the missing call, and docs/plans/NAV-COST-PLAN.md's third Tank Attack cost
is withdrawn - its other two stand.

What the slot is worth is what §4 says and what §1's three customers say: a
package cannot walk up AT ALL. The measured numbers are 3 reads for a
three-level path and **0 for the second walk of the same chain**, which is
§19.2.3's window.

#### 3.2.1 The original section, kept as it was written

The bullets above are each true and together they are **misleading about a
walk**, which is worth stating loudly because the misreading is the natural
one.

`dsk_here_ok` answers *"is a quiet chdir to (DL, AX) a no-op?"*, and it can
only say yes when **we are already standing at that exact cluster**. A walk
moves at every level, so it never is. Every level therefore takes
`dsk_chdir_q` -> `dsk_chdir_x` -> `disk_mount_x`, and for a floppy outside a
batch that **re-reads LBA 0** to recompute `[dsk_sigcur]`.

So the pathological shape is real after all, and it is exactly:

```
    is the disk the same?   (a read of LBA 0, a seek and a revolution)
    ..
    is the disk the same?   (again)
    ..
```

**The cure is published and nothing uses it.** `OSAPI_BATCH_BEGIN` /
`OSAPI_BATCH_END` (§18.9.3) set `dsk_bpbok` = 2, the BPB and its signature are
reused from the bank, and LBA 0 is not read again for the life of the bracket.
The SDK states the saving in the same terms: *"on a copy or an install [it is]
one call and one revolution per switch, and was 41 of one install's 199"*.

The bracket **nests and cannot be left open** - any `gfx_unlock` ends it, and
that is every way out of a locked run of kernel code - so a caller cannot hold
one across a moment when the user could reach the drive. A walk holds no gfx
lock and does not unlock, so a bracket taken around one survives it.

**No walker in the tree takes it.** Not `apps/tank`, not `apps/ftpd`'s
`fd_walk`, not the DOS box. That is a second finding of the same size as
docs/plans/NAV-COST-PLAN.md's `GOTO_QM` conversion, and it stacks with it: the
conversion stops each level paying for a scan, a sort, an icon harvest and a
cache flush, and the bracket stops it paying for LBA 0.

### 3.1 The trap: `GOTO_Q` makes exactly the bad shape

There is one way to get the pathological *"check, step, check, step"* walk,
and it is a two-letter mistake.

`OSAPI_FILE_GOTO_Q` moves the machine but **not the calling instance's own
record**. Every name-taking cell begins with `inst_vol_enter`, which re-stands
the machine in the instance's folder - so the next `FILE_FIND` **undoes the
step**, and a walk built on `Q` re-stands on every call. Within one volume
that is still free, which is worse rather than better: it is silent, and it
produces a walk that goes nowhere.

`OSAPI_FILE_GOTO_QM` (§74.1) moves the instance with it, so the next cell
resolves where the walk left it. **`QM` is the slot a walk is built on**, and
`Q` is for a copy loop that wants to come home.

This is the same distinction docs/plans/NAV-COST-PLAN.md turns on, one
consequence along, and `apps/tank`'s comment is the record of somebody
refusing `Q` correctly and never being told `QM` had arrived.

## 4. What IS expensive: the API boundary and the ordinal restart

The walk's real cost is not the disk, and naming it decides the slot's shape.

To learn its own name at each level, a package must find the entry in the
parent whose first cluster (+16) matches the child's. `OSAPI_FILE_FIND` is
**ordinal-based and restarts the directory walk on every call**:

```
    mov bp, cx                  ; BP counts the ordinals still to skip
    ...
    call dsk_dirw_start_x
```

So enumerating a parent of K entries is K far calls, each re-walking from
entry 0 - O(K) API crossings at 46.7us apiece and O(K^2/16) window lookups.
The window makes those lookups memory rather than revolutions, which is why
this is milliseconds and not minutes, but it is still work done K times to
answer one question.

**A kernel-side path builder walks each level's directory once**, with no API
crossing per entry and no ordinal restart. That is a constant-factor argument
rather than a complexity one: O(K) internal steps against O(K) far calls plus
O(K^2/16) lookups.

**It is no longer the main argument, though.** §3.2's LBA 0 re-read is one
real revolution per level on a floppy, which is worth more than every far call
in the walk put together - and unlike the far calls it is invisible, because
nothing in the published API mentions that a walk wants a bracket.

## 5. The slot

One call, the whole path, which is also what DOS does - `AH=47h` is GETCWD and
not GETPARENT, and for this reason.

```
OSAPI_FILE_PATH   ES:DI = a buffer, CX = its size
                  out CF=0, the NUL path from the volume root written there,
                      CX = its length
                      CF=1, AX = FERR_* (FERR_TOOBIG if CX was short)
```

Four things it must settle, none of them expensive:

0. **IT TAKES THE BATCH BRACKET ITSELF** (§3.2), and this is now the strongest
   single argument for the slot. The correct sequence is three slots deep -
   bracket, then `GOTO_QM` and not `GOTO_Q`, then find - and **no walker in the
   tree assembles it**. A slot that brackets internally is one a caller cannot
   get wrong, where advice in a comment is something three packages have
   already each got wrong differently.
1. **It walks and RESTORES.** The caller's instance must stand where it did,
   which is `OSAPI_FILE_HERE` then `inst_vol_mark` - both already exist.
2. **A bounded buffer, and the bound is the caller's.** `FD_CDMAX` is 16 and
   FAT12 has no depth limit, so the refusal path is real and must name itself
   rather than truncate (§47).
3. **A corrupt `..` is a refusal, not a crash.** `dsk_dotdot` already
   range-checks against `[dsk_maxclus]`; the loop above it needs its own guard
   against a `..` cycle, which a corrupt disk can present.
4. **It is a `.cold`/module candidate** (§2.8). Nothing on a boot path needs
   it, and its three customers all call it at most once per operation.

## 6. What has to be measured before it is built

One number, on a 4.77MHz 8088 under MartyPC: **what a four-level path costs
today**, built out of the published slots, warm window and cold. If that is
tens of milliseconds then §4's argument is correct but the prize is small and
the slot should be judged on the three packages' code rather than on speed. If
it is hundreds, the slot pays for itself on `apps/tank` alone.

`tests/dosdir.py`'s gate program already makes a subdirectory and stands in
it, so the fixture exists.

## 7. What this plan does NOT propose

- **No directory cache.** §19.2.3 is built, measured and keyed correctly.
- **No `OSAPI_VOL_SIG`, and no per-package banked cluster.**
  docs/plans/NAV-COST-PLAN.md §4.3 records why: the kernel already banks that
  data against that signature for every package at once.
- **No change to `dsk_find_x`'s dot filter.** Reporting `.` and `..` to
  packages would put two entries into every listing every caller then has to
  skip, to serve a question this slot answers directly.
