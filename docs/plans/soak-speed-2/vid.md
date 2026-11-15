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

| shared row | legs (the rows they were) | declared before (sum) | after |
|---|---|---|---|
| vidpage2sh | vidkpart vidpagemx vidpagely vidpagelymx vidpagebn vidpagebnmx | 262 | 220 (predicted) |
| vidpage3sh | vidpage3mx vidkpart3mx vidpage4mx vidpagefmx3 vidpagefmx3o | 326 | 270 (predicted) |
| vidpage4sh | vidpage4v vidpagefmx4 vidpagefmx4o vidpagefv4 | 385 | 330 (predicted) |
| vidpagepoolsh | vidpagepool4v vidpagepoolf | 275 | 250 (predicted) |
| vidpagensh | vidpage8n vidpage6n vidpagefv8n vidpagefv6n | 420 | 370 (predicted) |
| vidpagehercsh | vidpageherc vidkparth | 110 | 95 (predicted) |
| vidfskeys8sh | vidfskeysvga8 vidfskeysmodex vidfskeysflip | 200 | 170 (predicted) |
| vidfskeys4sh | vidfskeysvga4m3 vidfskeysink vidfskeysvga4pl vidkpartfs | 340 | 290 (predicted) |
| vidfskeyscgash | vidfskeyscga4 vidfskeysc160 vidfskeysc512 vidfskeystext | 240 | 200 (predicted) |
| vidplaycgash | vidplay vidplayring vidplay3 vidplaybig vidplaycomp | 195 | 150 (predicted) |
| vidv2vgash | vidv2 vidv2mx vidv24 | 180 | 140 (predicted) |
| vidv2enc1sh | vidv2pgmx vidv2flmx vidv2pg4 | 510 | 380 (predicted) |
| vidv2enc2sh | vidv2lp vidv2ly4 | 340 | 280 (predicted) |
| vidv2hercsh | vidv2herc vidv2shd | 100 | 85 (predicted) |
| vidmodexsh | vidmodex2 vidmodexfl vidmodexflh vidmodexflo | 270 | 230 (predicted) |
| vidmodexsh2 | vidmodexflc vidmodexrk vidmodexrs | 190 | 160 (predicted) |
| vidcgash | vidcga4 vidcga4p vidc160 vidc512 vidtext | 180 | 120 (predicted) |
| vidcgavgash | vidcga4vga vidc160vga vidtextvga | 120 | 90 (predicted) |
| vidcgahercsh | vidtextherc vidtexthercno | 50 | 40 (predicted) |
| vidscreensh | vidscreen vidscreeno | 300 | 280 (predicted) |
| vidscreennsh | vidscreenb vidscreenn vidscreennc vidscreenno | 195 | 170 (predicted) |
| videmssh | videms videmshyb videmsslow videmswrap | 260 | 200 (predicted) |
| vidpalsh | vidpal vidpallayer vidpalloop vidpalrep0 | 220 | 170 (predicted) |

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

(Filled in after the verification pass - see below.)

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
