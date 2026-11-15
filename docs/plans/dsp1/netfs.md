# Driver size pass 1: netfs (`drivers/net/`, `drivers/httpfs/`)

Agent **netfs**, branch `dsp1-netfs`, cut from `de93581a`. Products:
NET.DRV, OS88NET.COM (the DOS end, `os88net.asm` behind `os88sfx.asm`),
HTTPFS.DRV and HTTPPAGE.DRV. BRIEF.md is the brief.

**One thing blocks a green `make` and is not mine to fix** (CROSS-FILE, item
1): NET.DRV's image went from 4,185 to 4,071 bytes, across a 512-byte step,
so `kernel/driver.inc`'s `DRVM_IMG_NET equ 9` has to become 8. Until it
does, the fast-tier row `drvmem` fails, and it is the only row that fails.
With the figure set to 8 (checked by monkeypatching the gate, not by editing
the kernel), all 85 of t_drvmem's checks pass.

## 1. BASE AND TIP

"Image" is what a loaded driver claims (file + stripped bss); "file" is what
the disk pays. Clusters are `tools/os88diskuse.py --diff` against the base.

| product | image base -> tip | bss base -> tip | file base -> tip | clusters |
|---|---:|---:|---:|---|
| NET.DRV | 4,185 -> **4,071 (-114)** | 64 -> 144 | 3,473 -> **3,394 (-79)** | os8088-360: 7 -> 7 |
| OS88NET.COM | raw 14,959 -> **14,078 (-881)**; bss +150 | - | 10,784 -> **10,482 (-302)** | network360: 11 -> 11; util360: 11 -> 11 |
| HTTPFS.DRV | 5,553 -> **5,513 (-40)** | 432 -> 384 | 3,606 -> **3,584 (-22)** | os8088-360: **8 -> 7 (-1)** |
| HTTPPAGE.DRV | 2,350 -> **2,339 (-11)** | 176 -> 176 | 2,005 -> **1,995 (-10)** | os8088-360: 4 -> 4 |
| **total** | drivers **-165**, OS88NET raw -881 | | **-413** | **os8088-360.img free 37 -> 38** |

OS88NET.COM ships in `SYSTEM/DOS/` on `apps.img`, `apps120.img`,
`apps720.img`, `network360.img` and `util360.img` (`APPS_DOS`,
`NETWORK360`, `UTIL_DOS` in the Makefile); not on apps360 (§24.6). The
drivers ship on every system disk, kern_mega's included. The bss growth is
zeroed cells moved to where zeros are free (TAKEN 3, 10).

## 2. TAKEN

Hot paths here are the cable's per-nibble waits and byte helpers
(`lplink.inc`, which NET.DRV, OS88NET.COM and `tests/lptlink` all
include), and the per-block HTTP body loop. Every hot change is faster;
counts are the 8088's `max(clocks, 4 x bytes)`.

**lplink.inc (all three includers) - NET.DRV image -90, OS88NET raw -90**

1. **LP_WAIT's poll loses its `xor al, 0x80`** (HOT). The BUSY line is
   inverted in hardware; the branch is inverted instead of the bit, so the
   poll is 7 bytes rather than 9: 49 -> 41 clocks a poll, 40 -> 32 on the
   iteration that sees the strobe. Nothing after a wait reads AL's bit 7
   (lp_rnib shifts it out of the nibble and masks it). -2 a site, x4.
2. **The arming store is `mov [lp_dlset], cl`** (HOT): LP_SPIN's low byte,
   nonzero, read by lp_dlchk as "not armed yet". 4 bytes and 16 clocks
   against 5 and 20. An `%error` holds LP_SPIN's low byte nonzero. -1 x4.
3. **lp_dlchk hands CX back as LP_SPIN**, so the macro's re-arm goes (-3 x4,
   +3), and its tail is `shl ax,1 / cmc` for the modular sign test (-3).
   Cold: it runs once per 5,000 unanswered polls.
4. **lp_sbyte / lp_sword / lp_rbyte / lp_rword answer with the last call's
   CF** (HOT): their helpers keep AX (BX), so the `pop/push` between the two
   halves, the `clc`s and the duplicate fail tails go. lp_sbyte -8 bytes and
   ~39 clocks a byte; lp_sword -8 and ~39; lp_rbyte -4 and 4 clocks; lp_rword
   -4. lp_rword keeps its second `jc`, so AX on a failure is the base's.
   lp_snib and lp_rnib drop a `clc` each (CF is 0 off the wait's `test`).
   Per byte sent the wire now costs at least ~90 clocks less.
5. Cold: lp_latch's verdict tail and `inc dx` pairs (CF-safe without
   pushf/popf), lp_init stores AL's 0, lp_turn as a two-edge loop,
   mst_hello reads magic_r where magic_q's walk ends (`%error` on the
   layout), lpl_scan/cand_add `jcxz`, `sbb/inc` for the verdict, no
   pointless `push cs/pop es`, no `push cx` round a call that keeps it.
   lpl_scan now returns lp_latch's last CF rather than 0: it was never an
   output and none of its three callers (net_attach, os88net `start`,
   lptlink) reads it.

Equivalence: a unicorn harness (`<scratch>/h/lpharn.py`) builds one
wrapper against the base and the tip `lplink.inc` and runs 22 scenarios
against a reactive cable partner - bytes and words both ways, a direction
reversal, a dead partner on each half of the handshake (stuck ack, no fall,
second-byte failures), the hello good/bad/short/dead, lp_latch on a present,
absent, half-working and input-mode port, lpl_scan over three BIOS tables.
Registers, CF, every OUT, the IN count, the tick reads and the transport's
state are identical in all 22; the harness goes red (14 of 22) with one
branch flipped on purpose.

**NET.DRV (`net.asm`, `netui.inc`) - image -24, file -43 beyond the above**

6. net_pick and net_connect sweep with `jcxz`; net_connect's no-partner
   end shares `.lost`'s tail; net_info and net_mount answer with the call's
   CF; `net_bye` is the goodbye net_detach and net_drop both wrote out. Cold.
7. The page's zeroed cells (net_cbuf, net_buf, net_rc, net_down,
   net_pgbody, nu_btlbl, nu_btflg: 22 bytes) and the transport's
   (`LPL_ZSTATE db`, 46 bytes) sit in the trailing state, so the file drops
   them: file -43, image 0, bss 64 -> 144.

Equivalence for 6: read, every entry and fall-through. The transport is the
harness's.

**OS88NET.COM - raw -791 beyond lplink, file -302 in all**

8. **Epilogue folds** (20 routines, by a script that checks every jump to
   the fail label is a `jc`, or an `stc` + `jmp`): `pops/clc/ret / .x:
   pops/stc/ret` -> `clc / .x: pops/ret`. Then `jc .lost / jmp .out` pairs
   became `jmp .lost` (6), and srv_first/srv_more/stat_file/drive_live/
   srv_put/srv_rname/send_body/recv_body/sink_body/rt_*/srv_isdir/
   open_handle/srv_keep/pin_or_scan/lpl_kbhit answer with the CF they
   already had. Raw -131 for the folds alone, file only -8 (the packer had
   them for free); the jump displacements were what paid (file -37).
9. **srv_write and srv_append are one body** (they differed in three
   lines), `[srv_wbad]` makes the answer by `neg/and`; **srv_verb holds the
   DOS function**, not an ordinal (-22); wr_gate's refusal AL is used where
   four callers reloaded it; **srv_drvent** is the root-entry builder
   srv_drives and srv_drv_nth both wrote (-43); srv_dfree keeps nothing
   lp_sword already keeps; parse_args' four flag switches share one store
   and lost their trampolines; path_join's nine compares are a table;
   hd_get's new slot is a hit; cpy_close closes the destination through
   close_handle's cell; crlf is puts; dos_ticks reads 0000:046C;
   puthex16's `daa` digits; report_ports/listen_once index by DI.
10. **The zeroed state is reserved, not emitted**: lplink's, lplslv's,
    nwslv's and pktdrv's cells (151 bytes) live in the `absolute` block,
    which `net_bss_clear` zeroes as the first call in `start`. ne_init
    clears its ring in one `rep stosw` (it also zeroes pk_plen, which is
    written before it is read).
11. **nwslv's OPEN/LISTEN/ACCEPT share one answer** (nw_hans: status then
    handle from AL/AH) and the four other socket verbs carry the status in
    AL; `nw_rep_h` and `nw_st` are gone. nw_ready is `cmp byte [eth_up],1`.

Hot paths here: send_body/recv_body/sink_body's byte loops call the faster
lp_* above; their exits lose a `clc`. Everything else is per command or
per run.

Equivalence: a unicorn harness (`<scratch>/h/dosharn.py`) runs the base and
the tip `os88net.raw` under a scripted DOS (an in-memory FS, every int 21h
function the program uses, errors on demand), a scripted MASTER speaking the
nibble protocol, and a fake packet driver. 19 scenarios: info and listings;
chdir/stat/read/readat including the fence (`..\X`, `A*B`, `.`, empty) and
unknown handles; enum; every write verb in normal, `/RO`, disk-full and
DOS-error runs; `/W` (drives as the root); `/?`, a bad folder, odd and
overlong arguments, a pinned port nobody answers; bye and a second hello;
the socket verbs with no packet driver, with one but no DHCP, and with the
stack forced up (listen/status/accept/send/recv/close succeed). Wire bytes,
console text, DOS call sequence and the final FS are identical in all but
one scenario, which is the defect fixed in DEFECTS FOUND.

**HTTPFS.DRV - image -40, file -22; HTTPPAGE.DRV - image -11, file -10**

12. hf_detach falls into HFV_UNMOUNT's body (its own `hf_abort` was a
    no-op: hf_unmount aborts first); CF already 0 at hf_open/hf_fill/
    hf_getc/hf_line/hf_body/hf_get/hf_valid/hf_dfree/hf_chdir/hf_stat/
    hf_open_file/hf_lenis/hp_need/hf_vadd; hf_valid's two bad-name pops are
    one; hf_rec is (8n + n) x 2 (one shift fewer, `%error` on HF_REC);
    the request strings start with the CRLF that ends the line before
    them (the request is byte-identical); the Range start is kept on the
    stack; hf_urlch spends hf_get's CX and uses `daa` digits. The body loop
    (hf_body, per block) only lost a `clc` at its exit; hf_getc (per header
    byte) lost one.
13. HTTPPAGE: hp_status reads hp_vol and hp_st as one word and both
    endings share their two spaces (the text drawn is identical); hp_btns
    builds its flags in AL. `%error`s hold both layouts.

Equivalence for 12-13: read. Every changed `clc`'s CF was traced to the
instruction that sets it; the request bytes are the same concatenation.

Builds checked at the tip: `make -j2` (green but `drvmem`), `make -j2
mega` (green), `build/lptlink.img`, `make dosstub`, and into scratch:
NET.DRV with `-DNET_TURN1` and `-DDRV_ORG=0xC000` (the HMA image `make`
also builds), HTTPFS/HTTPPAGE with HFDIAG, HFTRACE and both.

## 3. REFUSED

- **ne_init's ring clear removed** (26 bytes): ne_init runs once, on
  zeros, but its comment designs for a second bring-up. Taken as one
  `rep stosw` instead (-13).
- **The stub's `push ds / pop es`** (os88sfx.asm, 2 bytes): DOS enters a
  .COM with ES = PSP, but the stub is the one thing every loader runs - DOS,
  os8088's own (§96), tests/dosstub and t_sfx's model - and 2 bytes do not
  buy a new assumption on the program that reached the field twice without
  executing (§62.12.5).
- **`lp_dlset` arming through a helper call** (~11 bytes): +39 clocks per
  wait, four waits a byte. Hot.
- **dos_ferr as a table** (~5 bytes): worse packed.
- **net_draw_btn's tail jump** (1 byte): t_btnrules counts `call`s.
- **A shared hf_tcheck/HF_HDRT tail** in hfnet: one byte bigger.
- **lpl_ticks as direct far calls**: 2 bytes more.
- **setup_root's / ent_ispkg's shared tails** (1-2 bytes each): a jump
  displacement costs this packer more than it saves.

## 4. WANTS THE OWNER

Nothing reworded, removed or limited. The one behaviour change is the defect
fix below.

## 5. WANTS AN ABI BREAK

None. `netpkg.inc` and `hfpkg.inc` are untouched.

## 6. CROSS-FILE

1. **`kernel/driver.inc`: `DRVM_IMG_NET equ 9` -> `8`** (comment `4.5K` ->
   `4K`). NET.DRV's image is 4,071 bytes, ceil(4071/512) = 8, so the
   Drivers page column and t_drvmem both want 8; DRVM_NET equals it. One
   kernel byte. **The only failing row at this tip (`drvmem`).**
2. **`tools/guide/manuals.lock.json`**: guidelock warns NET_DRV,
   OS88NET_COM, HTTPFS_DRV and HTTPPAGE_DRV are stale. No manual-level
   behaviour moved (the disk-full fix is below a page's detail), so they
   want stamping unchanged: `python3 tools/guidelock.py stamp <KEY> --why
   "driver size pass 1: no behaviour a manual describes changed"`.
3. **Agent ether's stack inside OS88NET.COM** carries ~231 bytes of zeroed
   state in its image (eth_mac, eth_ip..eth_dip, eth_ipid..eth_lport,
   eth_raw..dhcp_wk, dns_try..sk_rxcap, dn_o_hdr): `ethstate.inc`/`dns.inc`
   could offer an `absolute`-placeable form as lplink.inc now does. Image
   -231 there, file a few bytes.
4. **`os88net.asm` includes ether's `ethsock/tcp/inet/dns/...`**: any
   bytes agent ether takes there also shrink OS88NET.COM.
5. **Attach-only code** (BRIEF section 0.2): NET.DRV's net_attach 25 + net_pick 44 +
   lp_latch 57 + cand_add 74 + lpl_scan 103 = **303 bytes** run only at
   attach. `DRV_H_KEEP` (SPEC.md 51.1.3) already lets a driver give a tail
   back, but the tail would have to be last, past the trailing state that
   is now 144 bytes of free zeros - those would ship. net_connect re-reads
   the scan's tables, so the tables stay. HTTPFS and HTTPPAGE have under
   10 attach-only bytes.

## 7. ROWS TO RUN

The transport (lplink.inc) and NET.DRV: `doscable`, `socktest`, `fsclass`,
`drvup`, `hmadrv`, `wirezone`, `thewire`, `wire`, `netinst`, `netinstbad`,
`netcoexist`, `netpromise`, `romsock`, `buildmatrix`, plus fast `drvverb`,
`drvmem` (after CROSS-FILE 1) and full `megapkg`.
OS88NET.COM: `sfx`, `doscable`, `dosnetarena`, `socktest`.
HTTPFS/HTTPPAGE: `httpmount`, `httpmountname`, `httpdrvtool`,
`httpinstall`, `install`, `installabi` (fast), `syscheck`, `syscheckmono`,
`drvup`. kern_mega: `megapkg`, `megacurdisk`.

## 8. LOOKED

No boot taken: attach changed only in cold code that the transport
harness exercises (lp_latch, lpl_scan), and the wrap-up came first.

## 9. DEFECTS FOUND

- **OS88NET answered a full disk as an I/O error** (fixed, 0 bytes).
  `dos_ferr` did `mov al, FERR_FULL` and THEN `cmp ax, 39`, so AX was 6 by
  the time it was asked about 39 and error 39 ("insufficient disk space")
  went to FERR_IO. Found by reading; the DOS harness shows it as the one
  difference (mkdir's error 39: base FERR_IO, tip FERR_FULL).

## NOT REACHED

- `nsk_addr`'s second `push cx` (1 byte); net_pick's provably redundant
  `clc` (1).
- hd_par's two tails and setup_root's (1-2 bytes each, likely 0 packed).
- A boot to look at NET.DRV's attach and the HTTP page.
- A NET.DRV harness through its OSAPI calls (its changes were read).
