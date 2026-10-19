# ksp12 agent "imgmount" - mounting a `.IMG` as a drive, SPEC.md 18.101

Kernel size pass 12. Concept: SPEC.md 18.101 and 22.28 - mounting a `.IMG`
as a drive, the Disk window's bar re-cut (File / Edit / Nav / Disk), the
desktop icon's Unmount, the empty-space menu and the stored diskette icon.
Its unsquashed commits are on `origin/elendilon-old`: `c356413`, `cc8d977`,
`d69e19b`, `f84001b`, `066c054`. It had never been size-passed, and it is on
`main` (#252), so its ABI is FROZEN. Branch `ksp12-imgmount`, cut from
`kernel-size-12` at `df8603a1`. Figures are assembled bytes from
`tools/kernsize.py --json` (resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`) and the UNPACKED module image
(`os88drv.image_unwrap`), base against tip, all measured on this branch.

## BASE AND TIP

| | base `df8603a1` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` / `.bss` / `.cold` | 43,738 / 5,212 / 38,690 | 43,736 / 5,210 / 38,632 | -2 / -2 / -58 |
| **kern_big resident** | 93,574 | **93,512** | **-62** |
| kern_small resident | 60,413 | 60,413 | 0 (byte-identical `.text`/`.bss`/`.cold`) |
| kern_emu resident | 93,833 | 93,771 | -62 |
| `KERN_SIZE` big / small / emu | 99,328 / 62,464 / 99,840 | same | no rung moved |
| **`DISKOPS.DRV` image, kern_big** | 10,352 | **10,084** | **-268**, and its whole-KB claim **11 KB -> 10 KB** |
| **`DISKOPS.DRV` image, kern_small** | 7,184 | **7,168** | **-16**, exactly 7 KB, so its claim **8 KB -> 7 KB** on the 128KB machine |

`mod_need` claims the image size rounded UP to whole KB (`mem_bytes_kb_x`,
`kernel/mod.inc`), so both module figures are worth a KB of heap for as long
as `DISKOPS.DRV` is loaded - a clone, a Write Image, a compress or a mount.
**kern_small's 7,168 has NO margin**: one byte added to that module (by
anybody - the round agent's `compress.inc` hunk rides the same image) puts
the claim back at 8 KB. kern_big's 10,084 has 156 bytes to the 10 KB edge.

Against the brief's figures for the concept (+355 resident on kern_big, -28
on kern_small, `DISKOPS.DRV` +1,078): **-62 resident is 17% of it**, and
-268 is 25% of the module's growth. Below 30%: the concept's own conditional
code is about 440 resident bytes (the brief's +355 is net of what the menu
re-cut deleted), and the largest block of it - `dsk_xfer`'s `.img` arm and
its swap, ~130 bytes - is a re-entrant transfer loop with very little slack
in it. What was taken is everything sound I could find; REFUSED says what
was looked at and left.

## PARITY

Every multi-routine run after a change sits at an EVEN shift
(`tools/os88rdiff.py kernel <base>`, the shift runs): the routines left at an
odd shift on the tip are eleven single routines of this concept's own (UI
commands and the module's mount verbs), none hot. Three choices were made
for that alone and are named where they stand:

* `align 2` after `dsk_img16` in `.text` (the run merge is -3, so +1 pad):
  the 435 `.text` routines after it, the `icon_draw`/`ico_core` and
  `kent_`/`kret_` ladders among them, stay where they were.
* `dsk_img_swap` indexed by BP (26 bytes) rather than BX (25): the 76
  `.cold` routines after it include `dsk_runcap` and the FAT walk.
* `align 2` before `clo_start` in the module: the verb table is an odd
  number of bytes under the ladder on both kernels, and the cloner's and
  the compressor's loops follow.

## TAKEN

Three commits. Resident on kern_big unless stated; kern_small's resident is
untouched by all of them (everything resident below is `%ifdef OS88_IMG`,
kern_big alone, or assembles identically on kern_small).

### batch 1 (`d12daac1`) - resident -54, `DISKOPS.DRV` -246 big / -12 small

* **`.img` keeps the claim in DX** (`kernel/disk.inc`). `[dsk_imgcur]` was
  written once per transfer and read once per piece and by both swaps; DX
  holds the same word for nothing, because nothing in `.img` writes DX and
  the inner `dsk_xfer` banks it (`kentc_bp`). The LBA's high word moves to BP
  (`mov bp, [es:bp+4] / adc bp, 0`), which the swap spends anyway.
  `.bss` -2, `.cold` -10 net of the BP form.
* **The run table's dword is BIASED**: host LBA MINUS the run's first image
  sector (SPEC.md 18.101.1 updated first). A sector's host LBA is then DI
  plus the dword, one 32-bit add, and `.img` loses its `sub ax, [es:bp]`
  (-4). The module half pays nothing: `clo_mruns` tests whether a cluster
  extends the run in hand by comparing ITS bias with the last record's - the
  same thing exactly, contiguous iff `LBA - covered` is equal - so
  `clo_mnxlo`/`clo_mnxhi` and their stores go (module -15). Kernel-to-own-
  module interface: both halves changed together; `tests/imgmount.py` reads
  `IMG_FLG` and the row, never the runs.
* **`count * 512` by `xchg cl, ch / shl ch, 1`** (-2; CH is 0, a piece being
  inside one buffer), and **`jmp short .out` for `jmp .success`** (-1: CF is
  0 off the `or si, si`, and `.success`'s one test, `[dsk_bnc_si]`, is 0
  after every inner call - the note above `.img` already says so).
* **`fm_imgcall` is `fm_cldrop` and `fm_clone_bad`** (`kernel/files.inc`,
  22 -> 10): the success path's "drop unless a clone or join holds it" IS
  `fm_cldrop`, and the refusal (toast, then the same drop) IS
  `fm_clone_bad`. The success CF is `fm_cldrop`'s - 0 off the `cmp` of a live
  word, or `mem_free_x` releasing the module's claim - and `fm_c_unmnt` is
  the only reader.
* **Mount Image... shares Write Image's window test** through a new
  `fm_c_dlg` entry with the proc in AX (`fmv_winof` keeps AX); on kern_small,
  which has no `fm_c_mount`, `fm_c_wimg` assembles as it was.
* **Unmount Image's volume is read by the module**: a new verb `CLV_UNMNTW`
  (9) is `mov di, [fm_vp] / mov al, [di+FS_DRV]` falling into `clo_unmnt`,
  so `fm_c_unmnt` is `mov dl, CLV_UNMNTW` and the remount (22 -> 14). The
  remount stays RESIDENT and after the drop, as before, so the heap the
  listing sees is the same heap.
* **`fm_mnt_done_x`'s `fm_svp_set` moves into `clo_mntdlg`** - after
  `mod_need` and before anything that reads `[fm_vp]`, so if anything it is
  sturdier (a load that repainted would republish `[fm_vp]`).
* **`fm_bar_gate`**: its push/pop of BX round the image test go (-2):
  nothing after reads the window, and `kretfc_di` restores it.
* **`sc_rclick`** chooses the popup's item in CX while it tests the zone
  (`desk_select` and the lock keep CX) instead of re-testing the zone (-2).
* **The `.IMG` icon**: the mask run and the picture's first row are the same
  word, so one run lays seventeen (-3, +1 pad); the icon pass answers in ZF,
  which both arms already leave (`dsk_is_img`'s compare, the fill's last
  EQUAL `cmp`), so `.no: stc / ret` goes (-2); and `.dp` loads
  `[assoc_docslot]` before the branch rather than jumping round it (-2).
* **`DISKOPS.DRV`'s verb ladder is a table** (`clo_vtab`, `jmp dx`): eight
  bytes a verb against two. DX is the only register the verb sees changed,
  no verb reads it on entry, and every one of the seven `clo_call` sites and
  `fm_imgcall`'s three reloads DX or leaves through `kretc_di` (read, site by
  site). No bound, as before in effect: every caller names a constant.
* **`clo_mount` rewritten in place**: the claim is made BEFORE the
  attribute is decided (the attribute word banked across it from the entry
  the stat staged), and the transport, `IMG_FLG`, the name, the folder, the
  first cluster and the attribute word go into the claim in ONE ascending
  `rep movsb` / `stosb` / `stosw` run (adjacency asserted); the folder is
  `[dsk_cwd]`, which `fcp_goto` (or the dialog, for `CLV_MNTDLG`) left
  current; the count rides SI into `clo_mruns` and the row; the label is
  passed from the claim's own copy of the name and CUT in the row after
  `dsk_vol_add` (whose `desk_reflow` only marks the cell); the read-only
  flag IS the attribute's read-only bit (asserted). Eight working cells go
  (`clo_mtot`, `clo_mclus`, `clo_mattr`, `clo_mres`, `clo_mflg`, `clo_mdir`
  and the two `mnx`), and with them `clo_mlabel` and `clo_mrow`. The error
  tail sits BETWEEN the checks and the build, the three verbs that share its
  exit are laid out above it, and `.notfat` below the build, so every
  refusal is a short `jcc` (ten were relaxed to five bytes).
* **`clo_mrowal` answers "is it an image's row" in ZF** (kind, then class:
  a free row keeps its class byte), which both walkers asked in line.
* `clo_mhost` leaves ES = the claim and BX = the stat's cluster, which every
  caller wanted next (`clo_unmnt` no longer re-reads `[dskw_raw+DE_CLUS]`),
  and banks nothing.

### batch 2 (`5a4e0082`) - `DISKOPS.DRV` -22 big / -4 small, resident 0

* The host test goes through `clo_mrowal` (-9). It is exact on every
  reachable state: the stat just read the row, so it is live and in range,
  and a BIOS row never carries `DRVC_IMG` (an image's row is found from 3
  up, `dsk_vol_add`).
* `clo_mfind` compares the cluster in SI, so DX:CX - the size, the caller's
  next question - is never touched and needs no bank (-2).
* `clo_msay1` is written at its one caller (-2).
* `clo_unmnt`'s `cmp al, DVOL_MAX / jae` goes (-4): both callers bound AL
  (`sc_rclick` below `DESK_SVZ`, which is `DVOL_MAX` - asserted there -
  and `FS_DRV` is a volume).
* **`clo_key` counts `CL_STEP` down** (`test` / `dec` / `sub` where it
  compared, -4, both kernels): DL is dead past the ladder - no target and no
  callee reads it (read: `clo_imgrun`, `clo_rd`, `clo_wr`, `clo_issrc`,
  `clo_fin`, `clo_nextvol`, `clo_fits`, `clo_xmsup`). This is the cloner's
  code, not this concept's; it is in no other agent's hunk this pass and it
  is what puts kern_small's image at exactly 7 KB.

### batch 3 (`b68d7d8c`) - resident -8

* **`fm_mnt_done_x` is `mov dl, CLV_MNTDLG` falling into `fm_imgcall`**
  (11 -> 2). A completion proc owes the chooser no register (SPEC.md 38.6;
  `fdlg`'s commit keeps ES and BP itself and leaves through its own frame),
  so the `kentc_di` / `kretc_di` round the call banked nothing anybody
  reads. `fm_c_mount` goes back to `mov di, [fm_vp]` from batch 1's
  `call fm_sel_ok` (+1) so that the `.cold` after it stays even.

### equivalence

* The biased table and the `.img` arm: `tests/imgmount.py`, whose TEST.IMG
  is FRAGMENTED into four runs stored out of order on a 1.44MB host, reads
  the image through every run and checks every write back off the host
  disk. **Broken on purpose** (`add ax, [es:bp+4]` for `[es:bp+2]` in
  `.icnt`): RED, *"TEST.IMG's row did not happen in 60 GUEST seconds"*.
* Paths no row reaches, proved by reading and recorded here: `Cannot mount
  from here` (an image on an image, or a `DVK_FILE` host), `Too fragmented
  to mount`, `No drive letter free` - each a short `jcc` into the tail with
  the stack balanced at the jump (`.nohost` from after `pop bx`, `.full`
  and `.badrun` with nothing pushed), the same string and the same claim
  free as before.

## REFUSED

* **Moving the Unmount remount (`fm_mount` on the host) into the module**
  (resident ~-17 more). It would run with `DISKOPS.DRV`'s 10 KB still
  claimed, where today it runs after the drop - the listing's own claims
  could then be refused or shed on a full heap where today they are not.
  `CLV_UNMNTW` takes the half of it that changes nothing.
* **Dropping `fm_c_unmnt`'s `jc` on a module that cannot be read** (-2):
  the remount would then run against whatever `mod_need`'s failed read left
  in `[disk_drive]`. A failure path's behaviour.
* **The swap indexed by BX** (-1): parity (above).
* **Reading the attribute after the claim** instead of banking it across
  (-3): `mem_claim_hi_x` can compact and shed, and nothing proves no shed
  path restages `dskw_raw`. Banked.
* **`cmp ah, '.'` / `jbe` for the label cut's two tests** (-2): it cuts at
  every byte below `'.'`, and `'-'` (0x2D) is legal in a name.
* **The run search by total count rather than sentinel offset** (0 or +2:
  every arrangement measured equal or worse), **the RO test hoisted out of
  the piece loop** (bytes equal, ES would need a second bank), **an unrolled
  word swap** (36 against 26) and **saving the image's geometry on the stack
  instead of swapping** (larger).
* **A shared resident "is volume X an image" helper** for `fm_bar_gate` and
  `sc_rclick`: 13 bytes to save 6 at each of two sites, -1, and a new
  contract. Not worth it.
* **`sc_umnt` pointing into `fm_items_disk`**: that word is rewritten by
  `fm_bar_gate`, so the desktop popup would show the last window's greying.
* **`fm_open_sel`'s mount test**: already 12 bytes over the shared
  `dsk_is_img`; the staged entry has no raw extension to test without the
  NUL walk (SPEC.md 19.1).

## WANTS AN ABI BREAK

None. Every interface this work touched is kernel-internal or between the
kernel and its own module (the `IMG_*` claim layout, the `CLV_*` verbs,
`clo_vtab`), and both halves moved together. `MEM_K_IMG` (published in
`apps/os88api.inc`) is unchanged.

## CROSS-FILE

* `kernel/files.inc` `fm_fmt_ok` and `fm_bar_gate`'s image test each look up
  the same row (`mov bl, [di+FS_DRV] / call dsk_vol_row_x`) on every bar
  press; one lookup feeding both tests would be smaller if `fm_fmt_ok`'s
  callers allowed it to return the row - ~4-6 bytes, not taken (not this
  concept's routine).
* `kernel/disk.inc` `dsk_vol_add`'s label copy (`mov cx, 7` + a byte loop):
  untouched; noted only because `clo_mount` now relies on it copying from
  ES:SI in any segment, which is its documented contract.
* `.img`'s past-the-end refusal (`.ibad` from the search) leaves
  `[dsk_ioerr]` as the previous transfer left it, so a corrupt LBA reports
  whatever error came before. Not a regression (the base does the same) and
  should be unreachable, the volume being sized by `DV_SECS` (not verified);
  recorded, not changed.

## ROWS RUN

All on MartyPC, one instance (`--marty-jobs 1`), `python3 tools/os88test.py
soak -k ...`:

* batch 1: `imgmount` alone (ok 80.0s), then **broken on purpose and RED**,
  then restored; then `imgmount wimgtrip diskclone modstr lzcomp czseq czto
  czjoin desksc assocglyph icostore msel fmtreach fmtlow` - **14/14 ok**
  (937.5s).
* batch 2: `imgmount diskclone wimgtrip czto` - **4/4 ok** (339.6s).
* batch 3: `imgmount wimgtrip` - **2/2 ok** (106.5s).
* The fast tier at every batch (inside `make`): **43/43**. `make -j2`,
  `make -j2 small` and `make -j2 emu` at every batch. The fast tier's
  `stkbalance` row (every kernel source) green at the tip.

## DEFECTS FOUND

None fixed. One observation, above under CROSS-FILE: `.img`'s refusal of an
LBA past the image's end leaves `[dsk_ioerr]` stale.

## FOR THE COORDINATOR

The base does not build: `docs/plans/completed/ksp12/BRIEF.md` lines 14 and 147 cite
a section-signed 1.2 (read by `checkdocs` as a SPEC.md heading that does not exist), and
`docs/INDEX.md` is stale for `BRIEF.md` - so `make` stops at `checkdocs`.
This branch was built with both repaired IN THE WORKING TREE ONLY and
neither repair is committed, so merging it changes neither file; `INDEX.md`
will want regenerating once the notes are in.
