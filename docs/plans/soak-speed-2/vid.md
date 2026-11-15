# SOAK-SPEED 2 - the video player's rows (vid*)

The second soak-speed pass, stream "vid": the 256 soak rows named `vid*`
(about 4.5 h declared). The first pass (docs/plans/completed/SOAK-SPEED.md)
already took the per-row waits out of most of them - quick boots, 0.05 s
polls, `ui_done`, the encoder's cut-frame sums, the vidly* clip cache - and
its ledger says what is left in nearly every one: **the boot, and the plays
themselves**. A play is real time and is the subject. So this pass is about
the BOOT, which was paid once per row although dozens of rows boot the same
machine off the same system disk and differ only in the clip on B:.

## 1. One boot for several rows' legs (`tests/vidshare.py`)

`python3 tests/<script>.py --shared NAME="ARGS" NAME="ARGS" ...` runs each
`NAME` - the row it used to be - through the script's own `main(argv, boot=)`
unchanged, with `boot` the shared one: the first leg boots the machine
(`os88ui.boot`, exactly as the row did), the motor is waited off, and the
desktop is fork-snapshotted (`os88marty.Forks`, SOAK-SPEED 3); every leg then
gets a FRESH restore of that desktop with its own disk swapped into B:
(`Marty.mount`). Every leg starts from the same bytes a boot of its own would
have reached; only the disk differs.

- **The swap is seen** because an XT drive has no change line and os8088
  takes a new medium at the first access after the motor has stopped
  (SPEC.md 18.9.1): the snapshot is taken 3 guest seconds after the boot, past
  the 2.03 s motor-off. Volumes are told apart by their serial, which
  os88disk.py derives from the content; a leg whose disk has the boot disk's
  serial is REFUSED rather than run against the first disk's cached
  directory. A swap that went unseen anyway cannot pass: every leg compares
  what it plays against its own clip, which the other disk does not hold.
- **The legs keep their identity.** Each leg's output is headed
  `=== NAME: ARGS` and ends `=== NAME: ok|FAIL (host s)`, a leg that raises is
  a FAIL of that leg and the rest still run, and the last line names every leg
  that failed. A leg is still runnable alone, as before:
  `python3 tests/vidpage.py --kpart 400`.
- **Module state** the scripts mutated per run (`STOPS`, `KSECS`, vidfskeys'
  text geometry, vidvga8's `H //= rows`) is reset at the top of every
  `main()`; `pkg_syms` (an assembly of the player) is made once a process.
- **Not shared**: kern_mega legs (Bochs has no fork snapshot; the ported rows
  `vidpage`, `vidfskeysvga4`, `vidmodex`, `vidvga8`, `vidplayvga` stay rows
  of their own, so `tests/megaport.txt` still finds them), `--xip` (86Box),
  vidv2's `--refuse` (a boot function of its own), and anything on another
  machine or system disk (`Shared` refuses a leg whose boot differs).

### The registry (`tests/suite.py`, `_shared`)

The original `Row(...)` entries are untouched; `_shared` (after the port
lists, so the ports are made from the original rows) takes each group's rows
out and puts one row in the first one's place whose command is
`--shared NAME=ARGS ...` built from the rows' own commands, whose reason is
theirs joined, and whose `needs`/`wants` are their union. `secs` is the
group's own.

| shared row | legs (the rows they were) | declared before (sum) | measured wall, soak | declared now |
|---|---|---|---|---|
| vidpage2sh | vidkpart vidpagemx vidpagely vidpagelymx vidpagebn vidpagebnmx | 262 | 130.6 s | 165 |
| vidpage3sh | vidpage3mx vidkpart3mx vidpage4mx vidpagefmx3 vidpagefmx3o | 326 | 144.8 s | 180 |
| vidpage4sh | vidpage4v vidpagefmx4 vidpagefmx4o vidpagefv4 | 385 | 119.2 s | 150 |
| vidpagepoolsh | vidpagepool4v vidpagepoolf | 275 | 71.7 s | 90 |
| vidpagensh | vidpage8n vidpage6n vidpagefv8n vidpagefv6n | 420 | 146.8 s | 185 |
| vidpagehercsh | vidpageherc vidkparth | 110 | 43.9 s | 55 |
| vidfskeys8sh | vidfskeysvga8 vidfskeysmodex vidfskeysflip | 200 | 114.9 s | 145 |
| vidfskeys4sh | vidfskeysvga4m3 vidfskeysink vidfskeysvga4pl vidkpartfs | 340 | 229.9 s | 285 |
| vidfskeyscgash | vidfskeyscga4 vidfskeysc160 vidfskeysc512 vidfskeystext | 240 | 96.5 s | 120 |
| vidplaycgash | vidplay vidplayring vidplay3 vidplaybig vidplaycomp | 195 | 115.5 s | 145 |
| vidv2vgash | vidv2 vidv2mx vidv24 | 180 | 61.4 s | 75 |
| vidv2enc1sh | vidv2pgmx vidv2flmx vidv2pg4 | 510 | 123.3 s | 155 |
| vidv2enc2sh | vidv2lp vidv2ly4 | 340 | 82.6 s | 105 |
| vidv2hercsh | vidv2herc vidv2shd | 100 | 25.4 s | 35 |
| vidmodexsh | vidmodex2 vidmodexfl vidmodexflh vidmodexflo | 270 | 81.0 s | 100 |
| vidmodexsh2 | vidmodexflc vidmodexrk vidmodexrs | 190 | 64.6 s | 80 |
| vidcgash | vidcga4 vidcga4p vidc160 vidc512 vidtext | 180 | 42.0 s | 55 |
| vidcgavgash | vidcga4vga vidc160vga vidtextvga | 120 | 39.4 s | 50 |
| vidcgahercsh | vidtextherc vidtexthercno | 50 | 19.0 s | 25 |
| vidscreensh | vidscreen vidscreeno | 300 | 112.0 s | 140 |
| vidscreennsh | vidscreenb vidscreenn vidscreennc vidscreenno | 195 | 76.0 s | 95 |
| videmssh | videms videmshyb videmsslow videmswrap | 260 | 112.3 s | 140 |
| vidpalsh | vidpal vidpallayer vidpalloop vidpalrep0 | 220 | 113.3 s | 140 |
| vidrepeatsh | vidrepeat vidrepeatshd vidkpartrep | 180 | 63.1 s | 80 |
| vidprevhercsh | vidprevherc vidprevshd vidkpartpv | 214 | 131.0 s | 165 |

Groups are sized so no row is past ~6 minutes declared: a merged row costs
the lanes some packing granularity, which is the price of a boot.

## 2. vidv2's encoded clips cached (SOAK-SPEED 16)

`vidv2.encclip` (the six `--enc` rows) ran ffmpeg + `tools/os88venc.py` on the
same deterministic testsrc2 card every run: measured host-only on this box
(load ~18), **pgmx 99.4 s and hpa 10.9 s a miss, 0.2 s a hit**. It goes
through `os88fixture.cached` now, keyed on the arguments, `OS88_V2_BLOCKS`,
the encoder's sources and ffmpeg's version.

## 3. Polls (SOAK-SPEED 8)

0.1-0.5 s fixed polls in vidbigclus, vidcard, vidclmove, vidgrey, vidhdmake,
vidkeyclus, vidmove, vidplay, vidrepeat, vidresbig, vidresident -> 0.05.

## Measurements

All on this box with eight agents sharing four cores (load 8-18); MartyPC
guest seconds are load-independent, walls are not.

**One scoped soak** (`os88soak.py start -k ... --marty-jobs 1`, the 64 rows
the changes reach, kern_mega ports included): **61 ok, 0 FAIL, 3 SKIP**
(vidpage3xip, mega-vidcard, mega-vidpreview: no 86Box on this box), 59 min
wall. The 25 shared rows took **2,360 s** of wall; the 74 rows they replace
were declared **6,062 s** (declared, not measured - the honest before is
below) and are now declared 2,880 s (1.25x measured).

**Before / after, the same legs run alone and shared** (`OS88_WAITLOG` sums:
guest s waited; wall host s; back to back, same load):

| legs | alone (sum of N rows) | shared (one row) |
|---|---|---|
| vidcgash: vidcga4 vidcga4p vidc160 vidc512 vidtext | 99.9 s wall, 133.6 guest s | 58.4 s wall, 76.0 guest s (42.0 s in the soak) |
| vidfskeys8sh: vidfskeysvga8 vidfskeysmodex vidfskeysflip | 185.5 s, 218.9 guest | 135.1 s, 184.1 guest (114.9 in the soak) |
| vidpagehercsh: vidpageherc vidkparth | 85.9 s, 115.0 guest | 89.1 s, 100.5 guest (43.9 in the soak) |

A shared boot is ~6-11 host s once; a leg after the first starts in under a
second (vidcgahercsh: leg 1 17.1 s with the boot, leg 2 3.7 s). The saving
is the boot and launch per leg, so it is biggest where a leg is short
(vidcga*, -42% wall) and small where the leg is a long play (vidpage herc,
guest -13%, wall within noise).

**Broken on purpose**: the shared boot with `Marty.mount` stubbed out (the
swap never made, `.scratch-agent`-style monkeypatch, not committed) - leg 1
vidcga4 ok, leg 2 vidcga4p **FAILS** (the poster differs in 1,514 bytes,
every hold 16,000 pixels wrong), and the summary names it. So a leg played
against another leg's disk cannot pass.

**vidv2's cache**: hpa and pgmx cached == fresh encode, byte for byte
(md5 07e0a289... for hpa).

## Not done - for the next pass, by expected saving

1. **vidsound / vidspk (42 rows, ~1,700 s declared)**: each boots the XT-IDE
   VHD (28 guest s) for one play. They cannot use `--shared` as it is: the
   MARTYPC_WAV capture is opened at launch, and a fork restore inherits the
   SAME sinks, so legs would write one file. A harness verb to re-open the
   capture sinks on restore (debug_server.rs) would let them share one boot,
   with every leg's clip on the one VHD (it has the room).
2. **vidxip / vidbank / vidxms (QEMU, ~2,000 s)**: the same shape on QEMU; a
   QEMU `savevm`/`loadvm` fork would do what `Forks` does here.
3. **videnc (1,300 s, one host row)**: ~40 independent encoder legs in one
   `main()`; split into `--part N/M` rows it would pack across host lanes.
