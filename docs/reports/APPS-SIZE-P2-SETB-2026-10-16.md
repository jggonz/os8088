# Apps size pass 2, set B - the apps disk: network and office (2026-10-16)

docs/plans/HANDOFF-APPS-SIZE-P2.md's set B: `browser`, `ftpd`, `telnet`,
`OS88NET.COM`, `texpad`, `artful`, `audio`, `calc`. A MEASUREMENT, true of the
tree it was taken on and of no other.

## Tree and box

- Cut from the pass branch `claude/apps-size-2-disk-reclaim-6rn5hh` at
  `dc1890ba` (the handoff plus the set B claim). Worked on
  `claude/practical-edison-ny7yqm`.
- Built and measured in the 4-core cloud container. MartyPC built from the
  tree's own pin; every cycle count below is MartyPC's 4.77 MHz 8088.
- `python3 tools/os88diskuse.py --save` at the cut point and `--diff` at the
  end; every cluster figure is 1,024-byte clusters on the 360KB images.

## Method

One agent per package, each in its own git worktree off the cut point, briefed
with the handoff's 4.3 whole plus the package's own constraints. Host-side
testing only during the work; one look-boot each; every agent named the rows
covering its package and the session ran the union once at the end (below).

**Two owner decisions were taken mid-set and are part of this report:**

1. **OS88NET.COM's block mode (`/I:`) is REMOVED.** NET.DRV has published no
   `DSV_BLK` since SPEC.md 62.10, so nothing on os8088 could reach it; the
   file header and SPEC.md 62.10/62.12 say both ends would have to be
   restored to serve an image again, and which commit to recover from.
2. **Fourteen package builds shipped LZB instead of LZ4 - thirteen at the
   landing** (Arkanoid went back to LZ4 when set C's code pass took it to 5
   clusters either way; `lzbclust` caught it) (SPEC.md 20.13.5):
   ArtfulType, Arkanoid, Calculator (both arms), Cyclone, Dot Delirium,
   Missile, Sheet, Solitaire, TameGram, Tank (both arms), TeXPad, WireBall -
   each because LZB saves it a cluster on the 360KB disks it rides. Paint,
   Note Pad and the Task Manager stay LZ4 by the owner's word, whatever it
   would save them, because they open the common documents and must stay
   quick. Nine of the fourteen belong to other sets' packages; only their
   Makefile rule changed, no source. `soak -k lzbclust` is the new gate that
   the choice stays true.

## The table

File bytes as shipped (`build/<pkg>.o88` / `build/os88net.com`), before
(`dc1890ba`) and after; image + bss from each package's header.

| package | file before | after code pass | after LZB | 360KB disks (copies) | clusters per copy | image + bss before -> after | heap claims |
|---|---:|---:|---:|---|---|---|---|
| browser | 17,832 | **17,467** (-365) | - | apps, network (2) | 18 -> 18 | 26,885 -> 26,130 | unchanged |
| `MEDIA/BROWSER.HTM` | 2,184 | **2,046** (-138) | - | apps, network (2) | 3 -> 2 | - | - |
| ftpd | 12,291 | **11,864** (-427) | - | apps, network (2) | 13 -> 12 | 26,624 -> 25,600 | unchanged |
| telnet | 12,361 | **12,123** (-238) | - | apps, network (2) | 13 -> 12 | 21,186 -> 20,896 | unchanged |
| OS88NET.COM | 11,575 | **10,695** (-880) | - | apps, network (2) | 12 -> 11 | 40,585 -> 39,004 under DOS | none |
| texpad | 16,492 | 16,346 (-146) | **14,272** | apps, office, small, smallapps (4) | 17 -> 14 | 29,843 -> 29,579 | unchanged |
| artful | 15,387 | 15,075 (-312) | **13,217** | apps, office, small, smallapps (4) | 16 -> 13 | 29,467 -> 28,887 | unchanged |
| audio | 10,939 | **10,181** (-758) | - | apps (1) | 11 -> 10 | 18,868 -> 17,875 | unchanged |
| calc | 4,329 | 4,262 (-67) | **3,866** | apps, office (2) | 5 -> 4 | 5,255 -> 5,130 | none |
| calc, APP_SMALL | 3,353 | 3,175 (-178) | **2,891** | small, smallapps (2) | 4 -> 3 | 3,849 -> 3,609 | none |

The code pass alone is **-3,509 file bytes** across set B. LZB on the
fourteen builds is the rest:

| LZB on another set's package | LZ4 | LZB | clusters per copy | copies |
|---|---:|---:|---|---|
| Sheet | 37,029 | 32,189 | 37 -> 32 | office |
| WireBall | 14,315 | 12,009 | 14 -> 12 | apps, games, small, smallapps |
| Dot Delirium | 16,305 | 14,138 | 16 -> 14 | games, small, smallapps |
| Cyclone | 11,410 | 9,899 | 12 -> 10 | apps, games, small, smallapps |
| Missile | 10,689 | 9,219 | 11 -> 10 | apps, games, small, smallapps |
| Tank | 13,559 | 12,150 | 14 -> 12 | apps, games |
| Tank, APP_SMALL | 14,109 | 12,670 | 14 -> 13 | small, smallapps |
| Arkanoid | 5,285 | 4,637 | 6 -> 5 | apps, games, small, smallapps - **back to LZ4 at the landing**: set C took it to 5,031 LZ4 = 5 clusters, LZB 4,444 = 5 |
| Solitaire | 4,259 | 3,843 | 5 -> 4 | apps, games |
| TameGram | 3,175 | 2,939 | 4 -> 3 | apps, games, small, smallapps |

**What the disks gained** (`os88diskuse.py --diff` against the cut point):

| 360KB image | free clusters |
|---|---|
| `os8088-360.img` (system) | 5 -> 5 (set B rides none of it) |
| `apps360.img` | **1 -> 23** |
| `office360.img` | 51 -> 63 |
| `games360.img` | 80 -> 92 |
| `small360.img` | 55 -> 72 |
| `smallapps360.img` | 154 -> 171 |
| `network360.img` | 279 -> 283 |
| `media360.img` | 145 -> 145 |
| **total** | **+84** |

## Hot paths

- **calc**: `cal_key`, entry to `ret`, 56 typed keys covering every key
  class: 2,106,376 -> 2,042,978 cycles (-3.0%); display fields identical
  after every key on both builds.
- **texpad**: typeset 2,713,429 -> 2,686,757; full paint 6,852,033 ->
  6,794,791; preview ArrowDown about -5%; letters, arrows and Backspace
  faster on every interrupt-matched key. **One change makes the keystroke
  path slower, and is a defect fix** (below): +5,976 cycles = 1.25 ms a key.
- **artful**: no hot routine edited; `tools/os88rdiff.py --hot` passes on all
  83 keystroke, render, caret and drag routines, byte-identical at an EVEN
  shift (a removed routine had moved them odd; a cold routine moved into the
  gap restores the parity for no bytes).
- **ftpd**: one `nop` restores every hot label's base parity after the pass
  removed an odd number of bytes above them.
- **browser, ftpd, telnet, OS88NET.COM**: no cycle counts - MartyPC has no
  NIC. Every per-byte loop is untouched; every warm-path change removes
  instructions. OS88NET.COM's command dispatch became a 55-entry table:
  ~100 clocks for any verb, faster than the old chain for every verb os8088
  sends (predicted from the 8088 timings, not measured).
- **audio**: nothing a playback runs per sample or per block was touched.

## Launch cost

- **LZB, measured on ArtfulType** (MartyPC 5150, GLaBIOS, kern_big, 360KB
  floppy, `ld_run_body_x` to `lz_decomp_x`'s return):

  | launch | LZ4 | LZB |
  |---|---:|---:|
  | first | 1,600.0 ms | 2,140.7 ms (**+541**) |
  | again | 1,219.2 ms | 1,630.7 ms (**+411**) |

  The decode adds 764 ms for 18.6 KB of image; the smaller file gives
  224-352 ms of reading back. The other thirteen builds are ESTIMATED from
  the same rates at about +26 ms per KB of image net: roughly +130 ms
  (Calculator), +150 (Arkanoid), +330 (Cyclone), +340 (Missile), +440
  (Tank), +500 (TeXPad), +520 (Dot Delirium), +1.06 s (WireBall), +1.26 s
  (Sheet). More off a hard disk, where the read saving is smaller.
- **audio** builds its IMA step table at entry: ~4 ms.
- **OS88NET.COM**'s stub decodes 3,895 fewer bytes; its startup clear grew by
  2,657 bytes of `rep stosw`, under 2 ms.
- Nothing else moved work to an entry proc.

## User-visible changes

- OS88NET.COM no longer accepts `/I:`; `/?` no longer lists it.
- `BROWSER.HTM` lost its DOCTYPE, `<meta>`, `<html>`/`</html>` and `</body>`;
  it renders identically on the machine and in `htmsim` on all three
  adapters.
- Nothing else, apart from the defects fixed below.

## Defects

**Fixed:**

- **browser**: every GET form submitted to the FIRST form's action (AL still
  held the form's method byte). Pages with one form, like FrogFind, never
  showed it.
- **ftpd**: Server > Sockets built its rows in the buffer the worker
  composes PASV/EPSV/PWD/SIZE/MKD replies in, so a menu pick mid-reply could
  garble the reply on the wire.
- **telnet**: `tz_infodone`'s digit loop reloaded its bound with 10 on every
  digit, so a 63-byte name in the ZFILE info block read `[tz_sseg]` as size
  digits.
- **OS88NET.COM**: a command-line argument over 79 characters overran its
  80-byte buffer (DOS's tail is 127).
- **texpad**: the preview's full-pane fallback filled white and then dithered
  over every pixel (a double draw). And **typing after a window drag drew at
  the old position** - over the title bar and the desktop - because a dragged
  window is replayed without a paint (SPEC.md 11.96.12.0) and `tp_onkey`
  drew from geometry cached at the last paint. The fix re-reads the geometry
  on every key: **+1.25 ms a key**, the one change in the set that slows a hot
  path. It is commit `30d2bcdb`, separable, and the owner's to keep.
- **calc, APP_SMALL**: `h`, which has no pane to open there, cleared
  `[cal_wait]`, so `12 +` then `h` brought `12` back into a field that was
  waiting for a new number.

**Found, not fixed:**

- `nw_idle` in `drivers/net/nwslv.inc` is never called, so OS88NET.COM's TCP
  stack is not pumped between commands - a defect or dead code. Shared with
  NET.DRV, so outside the pass.
- **ftpd** on CGA comes up 155 px tall instead of `FD_H` = 176, hiding the
  newest log line. Probably older than this pass; not diagnosed.
- **audio**: a 2 px dead lane between transport buttons since the move to
  the os88ui gesture library (SPEC.md 86.7 corrected to say so).
- **browser**: an empty or `?`-only form action resolves against the page's
  directory, not the page itself - the general resolver's handling of `?x`.
- **texpad**: `tp_draw_srow` fills each edited row white and then letters it
  opaquely - a double draw on every keystroke; fixing it is a hot-path
  redesign. And SPEC.md §69 carries stray Word subsections §68.14/§68.15.
- **telnet**: `tz_end`'s no-post arm calls `con_markall` twice.
- **`build/lzdrv360.img` does not build**: 362 clusters needed of 354 (363
  at the cut point, so it predates this set). It is the `lzdrv` rows'
  fixture - the shipped system disk with RAMDISK.DRV swapped - and the
  system disk itself has 5 clusters free. The `lzdrv*` rows were left out of
  this set's soak for it.

## Wins no set could take

| where | what | estimate | carriers |
|---|---|---|---|
| `apps/os88line.inc` | `os88line_edit` has no opt-out (`%define OS88LINE_NOEDIT`) | ~125 packed bytes a carrier | browser, ftpd, telnet, sheet, weave, loom's `lmpvmod`, SAVER.DRV, ETHER.DRV (only dos and deskcal call it) |
| `apps/os88cp437.inc` / `os88con.inc` | the CP437 face's box-drawing and shade rows (0xB0-0xDF), 221 packed bytes, which `tools/cp437font.py` already draws by arithmetic, built at launch | ~120 bytes a carrier | telnet (apps, network), DOS (system disk and others) |
| `lplink.inc`, `ethsock.inc`, `nwslv.inc` | `mst_hello`, `eth_v_bulk`/`_sktab`/`_abort`, `nw_idle` are unreachable in OS88NET.COM - an `ETH_NORAW`-style switch | ~163 packed | OS88NET.COM only |
| `OS88_REGION_MOVABLE_NOKEEP` | with no argument emits its own `jmp short` + `ret`; a carrier with a bare `ret` can name it | ~3 bytes a package, no include change | every package |
| `apps/os88spkfx.inc`, `os88spk.inc` | ~3.3 KB of audio's image between them; unsized | - | audio, every speaker player |
| `tools/os88sfx.py` + stub | OS88NET.COM's packer: 10,588 against zlib's ~10,500 and LZMA's ~9,900; a repeat-last-offset code might win a few percent | unestimated | OS88NET.COM |

**And one the pass did not ask about**: `soak -k lzbclust` with
`--candidates` lists the LZ4 packages LZB WOULD save a cluster for. On the
final tree: Browser (18 -> 16), The Wire (11 -> 9), FTPD, Telnet, Audio,
Chart, DeskCal, Gorillas (one each), isoTron (17 -> 15), DOS (31 -> 28),
Tracker (26 -> 24), MIDIRack (31 -> 27), Video (41 -> 36), 1942 (30 -> 26) -
plus Paint, Note Pad and the Task Manager, which the owner keeps LZ4. Not
every one of those rides a 360KB disk; the row counts 1KB clusters of the
file whatever disk it is on.

## Lessons for the other sets

- **LZ4 already packs a repeated idiom to ~3 bytes**, so factoring out
  save/restore sequences, epilogues or duplicated arithmetic barely moves the
  FILE and has more than once GROWN it (calc +4, texpad +6, OS88NET +9 and
  +42, browser +4). Deleting unique code is what pays. Measure the packed
  file, every time.
- **Removing an odd number of bytes above hot code flips its parity**, which
  costs an 8086 or 286 fetch cycles; `tools/os88rdiff.py --hot` finds it, and
  moving a cold routine into the gap fixes it for free.
- **A Makefile rule change is not a prerequisite**: moving a rule from
  `$(OS88PKG)` to `$(OS88PKGB)` left every existing tree LZ4 and reporting up
  to date. `LZBPKGS` stamps the list for that reason.
- **The agents' worktrees were cut at `a123653b`**, not the session's HEAD;
  each agent fast-forwarded to `dc1890ba` before its first edit. A session
  running this handoff should tell its agents to check their base.

## Coverage

Run once, after the last agent merged, on HEAD `b104e049` (the session's
union of every agent's named rows, the handoff's shared rows - `appsmall`,
`smallreq`, `movable`, `dispapps`, `regapp`, `regwork`, `lz*`, `assoc*` - and
at least one launch row for each package the LZB change reached):

- `make -j4`, `make small`, `make smallapps` green; the fast tier 47/47 at
  every merge.
- **`os88soak.py start`, 106 rows, 42:51 at width 4: 103 ok, 3 FAIL, 0 SKIP**
  (`build/soak/20261016-140656/`). `lzdrv*` were left out - their fixture
  does not build (Defects, above).
  - **`lzship` was a REAL defect in this set, and is fixed**: `make zset
    ZFMT=lz4` pairs packing with a kernel built `COMPRESS=lz4`, which carries
    the LZ4 decoder alone, and the fourteen LZB builds were packed LZB there
    all the same - CALC.O88 came back `LD_EBAD` from its own kernel. The LZB
    packer now follows `COMPRESS` (`PKGZBFMT`) and its stamp carries it.
    Re-run on the fix: **ok** (68.9 s). `lzship-lzb` was ok in the soak.
  - **`btnall` is PRE-EXISTING**: `os88bisect.py classify --base dc1890ba
    -n 2` - 2/2 failed at HEAD and 2/2 at the cut point, on the same leg. It
    closes each package through a menu named after the WINDOW TITLE, and the
    browser's title is `os8088 Web Browser` where its menu is `Browser`;
    neither string moved in this set.
  - **`lzbig` passes at HEAD**: classify, 0/2 failed on a run alone - it
    failed in the soak on its pointer-motion watch (`the arrow moved 10
    times... wanted >= 20`), every one of its byte-for-byte legs green, and
    nothing in this set reaches the kernel's Compress verb.
- **`make test-full`: 54/54**, 61.3 s.
- **New row `lzbclust`** (soak): every LZBPKGS package still saves a 1KB
  cluster over LZ4, and is LZB on disk. Red on purpose both ways (a name
  added to the list with an LZ4 file; Solitaire's small arm packed LZB,
  4 clusters either way). 1.2 s.
- `tests/ftpd.py` (unregistered, QEMU; FTPD's agent named it as the script
  that exercises most of that package's pass): **all assertions passed**,
  including `FTPD.CFG` written and read back byte-exact and STOR's upload
  read off the image by an independent reader.

## Addendum - after the landing, at the owner's word

Two more changes on the same day, measured on the tree set B landed on
(sets A, B and C merged, `9896ba84`):

- **`OS88LINE_NOEDIT`** (SPEC.md 83.1.2): `apps/os88line.inc`'s
  `os88line_edit` is now opt-out, and the eight carriers that never call it
  define the gate. File bytes: Browser -130, FTPD -122, Telnet -118, Sheet
  -117, WEAVE -113, `LOOM.WPV` -160, `SAVER.DRV` -125, `ETHER.DRV` -128. DOS
  and DeskCal, the two callers, are unchanged.
- **Six more packages ship LZB**, the owner's pick from `lzbclust
  --candidates`: Telnet (12 -> 11 clusters), Audio (10 -> 9), Chart (9 -> 8),
  Gorillas (13 -> 12), The Wire (10 -> 9, on every system disk - the first
  LZB package there) and Browser (17 -> 15). Nineteen builds in `LZBPKGS`.

Coverage: one scoped soak of the 71 rows the two reach (`build/soak/
20261016-153606/`) - **69 ok, 2 FAIL, 0 SKIP**. `btnall` is the
pre-existing failure above. `apspkcovox` failed on the screen saver coming up
during its boot (`[blk_on]` set past 722 guest seconds), before Audio was
launched; `os88bisect.py classify` passes it 0/2 failed on HEAD alone, so it
was intermittent. `tests/ftpd.py`: all assertions passed.
