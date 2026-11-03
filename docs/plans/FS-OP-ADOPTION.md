# OSAPI_FS_OP adoption: where one operation should be one bar

**BUILT on branch `toast-n-progress` (2026-11-01), except the rows listed
under *Not taken* below.** The mechanism is SPEC.md 12.8.8-12.8.10. Its costs are in
`docs/reports/TOAST-QUEUE-OP-PROGRESS-COST-2026-10-31.md`. This file is the
survey of every place that could use it, taken 2026-10-31 by reading the tree.
No row has been measured or driven.

**What it changes.** Today every file transfer is a phase, and the widget's
bar rescales to each one (SPEC.md 12.8.3). The chrome lives for one lock hold.
`OSAPI_FS_OP` (DX:CX = the whole operation's bytes, 0 = over) keeps one bar
across the phases, and **parks** it on the menu bar between the owner's
callbacks. Two shapes benefit:

- **(a) Several transfers in one callback.** Today the bar fills N times;
  with the op it fills once.
- **(b) Transfers spread over timer or wake callbacks.** Today the icon
  blinks once per chunk; with the op it is one parked bar.

Inside an fsx bracket or behind a fullscreen window the widget never shows,
so there is no effect there.

## What was built, and what was not

Every STRONG and WORTHWHILE row below is adopted, and so are the MARGINAL
rows B1 (Browser downloads), F1 (FTPD RETR) and R3 (RealDMG state), because
each cost a few bytes. The six follow-ups are built: writes count, the lock
is not required, nesting is refused, a kernel owner (0FEh), the driver case
(it resolves to the kernel owner, because `OSAPI_DRV_USE`'s DRVU_CALL clears
the stamp), and `os88_fs_op()`.

**Not taken:**

- **V2**, the Video Player's paused bank: a parked bar during a pause
  misleads.
- **F1's STOR half**: FTPD parses no ALLO, so it knows no size.
- **L1**, Loom: there is no total without four FILE_FIND walks.
- **WireFleet's half of PX**: it is 3 bytes under APP_MAX_SIZE, so it defines
  `OP_NO_FSOP`.
- **K6**, Compress/Uncompress: it needs a scale its phases do not count
  against.
- **K8**, Paste of a set: deferred by the owner.

**Two known gaps:**

- **The Video Player's in-bracket hold fill is not counted.** The widget does
  not arm there, so after a play the bar trails the player's own card.
- **Every kernel or driver operation shares owner 0FEh.** While a same-drive
  clone or a rejoin is parked at a prompt, other kernel file work counts into
  its bar.

**A shipped package that calls 0x04EF needs Wire level 7.** Because the parts
library calls it, that means every package with a carve or a lazy fetch.
Those are declared in the website's `data/wire.json`, not in this tree, and
`OP_NO_FSOP` is how a package stays below level 7.

## Kernel follow-ups the survey found (all built)

1. **Fix the SDK note.** `apps/os88api.inc` says the bar counts "your file
   reads". **Writes count too.** The write paths call `fpg_begin` and
   `dsk_xfer` steps every sector. A save or a download is 1x its size, a copy
   2x.
2. **The lock is not required.** `fpg_arm` already arms lock-free (it banks
   the cursor debt), so PiXEL's, System Check's and FTPD's lock-free wakes
   are covered. As today, a lock-free arm stays armed until the next
   `gfx_unlock` anywhere. Drop "the lock held" from the contract.
3. **Nesting.** The parts library (row P1) runs inside packages that want a
   wider operation of their own (PixelStein, WireFleet, DOS). A begin from a
   caller that **already owns** an op should refuse (CF = 1). A library must
   end only an op its own begin opened. That is a few bytes in `osapi_fs_op`.
4. **A kernel owner.** `[fpg_pown]` = 0FFh means "none", so kernel and
   driver callers cannot park. Three cases need a reserved kernel owner id:
   - same-drive clone (K3): a swap prompt ends the hold every trip
   - rejoining a split set (K7)
   - a Paste of a set across overwrite questions (K8)
5. **A driver door.** Install's copy engine (HDDTOOL.DRV) and the RAM disk
   image (R1) run on behalf of a package or of the Control Panel. Whether
   `inst_caller` names the right owner from inside a driver verb is
   **unverified**.
6. **A C wrapper.** `os88_fs_op()` in `apps/cc/os88.h` and `os88thunk.asm`
   is needed for rows P1 (through `crt0.asm`), L1 and every future C caller.

## The sites

STRONG = a visible defect a user waits through today. WORTHWHILE = a real
improvement but small or rare. MARGINAL = little to see.

### Packages, shape (b): the icon blinks per chunk today

| # | Site | Operation | Total known up front | Today -> with the op | Verdict |
|---|---|---|---|---|---|
| V1 | `apps/video/video.asm` `vp_xopen` :5240, `vp_ontimer` :9121 -> `vp_xstep` | the background hold load into XMS, 32KB a tick (a 4MB `.V88` is ~128 ticks) | yes, `[vp_xcap]` from the size probe | blinks ~128 times -> one parked bar | **STRONG** |
| X1 | `apps/xtmp3/x3hold.inc` `x3h_begin` :42, `x3h_tick` | the whole MP3 read before play on a 286+ | yes, `x3h_size` | blinks per pass -> one bar | **STRONG** |
| W1 | `apps/waverack/wrsave.inc` `ws_go` :283, `ws_step` | Save / Save As of a WAV, one disk call a tick (10MB ~1,280 calls) | yes, ~2 x `ws_bytes` | blinks every tick -> one bar | **STRONG** |
| W2 | `apps/waverack/wrenv.inc` `we_rescan` :311 | the envelope scan on open, 16KB a tick | yes, the header's frames | blinks per gulp -> one bar | **STRONG** |
| P2 | `apps/pixel/pxpump.inc` `px_open` :1134, `px_pumpfill` | opening a picture over 32KB, 16KB slots on lock-free wakes | yes, `px_head` | blinks per wake -> one bar | **STRONG** |
| T1 | `apps/thewire/wrarc.inc` `wr_arcwrite` :599 | Add to Disk / Load Program of an archive, one write per wake | yes, `WA_TOTAL` | blinks per entry -> one bar | **STRONG** |
| S1 | `apps/syscheck/scan.inc` `sc_man_chunk` :308 | System Check summing the manifest (~100-200KB), lock-free wakes | yes, `SC_MANKB` | blinks per wake -> one bar | WORTHWHILE |
| Z1 | `apps/telnet/tezm.inc` `tz_commit` :2152 | ZMODEM download, 4KB a wake (windowed only; nothing changes in the fullscreen bracket) | yes, the ZFILE size | blinks per 4KB -> one bar | WORTHWHILE |
| X2 | `apps/xtmp3/x3conv.inc` `x3c_write` :2263 | Decode First to WAV, minutes to hours | approximate, from the duration | blinks per half -> one bar, ending roughly | WORTHWHILE |
| P3 | `apps/pixel/pxwrite.asm` `pu_pump` :1955 | Save As | exact for BMP and PIX; an upper bound for GIF, PNG and PCX | blinks per slot -> one bar | WORTHWHILE |
| B1 | `apps/browser/brdownload.inc` `br_dl_onwake` :1397 | the Download Manager saving to disk | from Content-Length, when sent | hard disk only: blinks per cluster -> one bar (a floppy cluster is under `FPG_WARM`) | MARGINAL |
| F1 | `apps/ftpd/ftpd.asm` `fd_do_read`/`fd_do_write` | a remote client's RETR/STOR | RETR yes, STOR only with ALLO | blinks for the whole transfer at a console nobody is waiting at | MARGINAL |
| V2 | `apps/video/video.asm` `vp_pstep` :8693 | a paused play's bank read-ahead | the bank's size, not the file's | a bar during a pause may mislead | MARGINAL |

### Packages, shape (a): the bar fills N times today

| # | Site | Operation | Total known up front | Today -> with the op | Verdict |
|---|---|---|---|---|---|
| R2 | `apps/realdmg/realdmg.asm` `rw_load` :790 | opening a Game Boy cart with the XMS bank cache (a 1MB cart is 64 bank reads, plus AOT/JIT parts) | yes, `rw_romkb` plus the part table | 64+ fills -> one | **STRONG** |
| P1 | `apps/os88partsbody.inc` `op_load` :45 / `op_read` :715, `op_fetch` :1681 | a package's eager parts at launch, a lazy part on a command; 32KB a take | yes, the part table | one fill per 32KB -> one. Visible only for spans over 32KB (RealDMG core and tables, XTMP3 front and art, Word, Scribe) | WORTHWHILE: **one change reaches ~25 packages**, and needs follow-up 3 |
| V3 | `apps/video/video.asm` `vp_rload` :10186 | Play of a RESIDENT file: both blocks loaded before the bracket | yes, `BK_PACKED` x 2 | 2-6 fills -> one | WORTHWHILE |
| M1 | `apps/midirack/mrwt.inc` `mwt_load` :163 | the wavetable bank on first play, 32KB chunks | yes, `mr_bankkb` | N fills -> one | WORTHWHILE |
| PX | `apps/pixelstein` `pxl_entry`, `apps/wirefleet` `wf_entry` | the entry proc's parts plus level, art and PCM | yes, the tables | 3-5 fills -> one | WORTHWHILE, needs follow-up 3 |
| D3 | `apps/dos/dosload.asm` `dsl_entry` :197 | launching DOS.O88, parts 0 and 1 | yes, the part table | ~3 fills -> ~2 (the kernel's own image read stays separate) | WORTHWHILE |
| G1 | `apps/win16/win16.asm` `w16_wake` :745 | opening a Windows program: its part, the NE, the face | mostly, via the zero-capacity size probe | 3+ fills -> one | WORTHWHILE |
| D1 | `apps/dos/dosh.inc` `dsh_c_copy` :790, `dsh_stream` | COPY / XCOPY at the **windowed** prompt, 8KB a chunk | per file, from `dsh_fnd`; a set needs a pre-walk | hundreds of fills -> one per file, or one per set | **STRONG**, but the DOS core may name no OSAPI cell (SPEC.md 96.4), so it needs a `DBE_*` door |
| D2 | `apps/dos/dosh.inc` `dsh_c_type` :1368 | TYPE of a long file | yes, `dsh_stat` | per-8KB fills -> one | WORTHWHILE |
| I1 | `apps/install` -> HDDTOOL `hd_inst_sys` :964, `hd_icopy_tree` :1709 | Install to hard disk: the whole system floppy, then the apps disk | not today; ~2 x (capacity - `OSAPI_FILE_DFREE`) is a cheap over-estimate | hundreds of fills -> one per disk | **STRONG**, needs follow-up 5 |
| E1 | `apps/browser/browser.asm` `br_savesourceems` :6739 | Save Source of a page over 64KB | yes, `br_srclen` | 3+ fills -> one | WORTHWHILE, rare |
| L1 | `apps/loom/lmproj.c` `ovl_openproj` :595, `lm_saveall` :331 | open or save a Weave project, up to 4 files | no; 4 `FILE_FIND`s would give it | up to 4 fills -> one | MARGINAL, needs follow-up 6 |
| R3 | `apps/realdmg/rwstate.inc` `rw_stwr`/`rw_strd` | save or load state, 3-4 calls | yes | 3 fills -> one when windowed | MARGINAL |

### The kernel (in-hold `fpg_opbegin` unless noted)

| # | Site | Operation | Today -> with the op | Verdict |
|---|---|---|---|---|
| K1 | `kernel/diskops.inc` `clo_both` :1975 | two-drive disk Clone | ~12 fills on a 360KB disk -> one | **STRONG** |
| K2 | `kernel/diskops.inc` `clo_imgrun` :1872 | Save Img / Write Img | 2 fills per pass -> one | **STRONG** |
| K5 | `kernel/hiber.inc` `hbm_xwrite` :3995 | kern_mega hibernate, 32KB appends | ~20+ fills -> one (in real mode it is already one write) | **STRONG on kern_mega** |
| K3 | `kernel/diskops.inc` `clo_key` :1348 | same-drive clone across swap prompts | per trip -> one, which needs the parked form | WORTHWHILE, needs follow-up 4 |
| K7 | `kernel/compress.inc` `cmz_jloop` :2376 | rejoining a split set across floppies | per disk -> one | WORTHWHILE, needs follow-up 4 |
| K8 | `kernel/filecp.inc` `fcp_step` :908 | Paste of a folder or a multi-select set | one bar per file -> one per set. Needs a size pre-pass, nesting (follow-up 3) and the parked form across questions | WORTHWHILE, deferred by the owner (2026-10-31) |
| R1 | `drivers/ramdisk/rdimg.inc` `rd_preserve`/`rd_load` | save or restore the RAM disk image, 32KB chunks | ~9 fills -> one | WORTHWHILE, needs follow-up 5 |
| K6 | `kernel/compress.inc` `cmz_stream` :1759 | Compress / Uncompress | already hand-rolls one bar (`cmz_rearm`), which flickers to each chunk's scale mid-I/O; the op would need U + U + the output, and the output is not known | MARGINAL, needs a scale phases do not count against |

### No change needed

These are single transfers, sit inside a bracket, or are streaming playback:

- **Documents.** Every document open and save in Sheet, Chart, Paint, Note
  Pad, TeX Pad, Scribe, Word, CWORD, Weave, Sticky Notes and Frotz is one
  transfer. So are The Wire's flat files and catalog, and the Browser's page
  load and saves.
- **Media and data.** Tracker, ModPlug, CDRack, Artful, imgtest and
  ExciteBike load in one transfer.
- **Inside a bracket.** Lemmings, 1942 and DrMarco's asset loads, Skies'
  worlds, the Video ring prefill, and every DOS program.
- **Streaming playback.** Audio, Video Live, and WaveRack playback and
  recording.
- **Emulators.** C64, APPLE2 and RunCPM program loads.
- **Kernel internals.** Package and driver loads, module loads, SYSTEM.CFG,
  ASSOC.DAT and the desk files are single transfers. FORMAT has no file
  phase.

**A design note for the future:** the planned Apple II Disk II image
(143,360 bytes) will need 3+ reads. Design it with the op.

**Duplicated progress.** Rows V1, X1, W1, W2 and S1 already show their own
progress text, so their two counts should move together.
