# VIDEO-PAGES-PLAN - page-ahead on every page a screen has

**BUILT (2026-10-15): all five waves** - the format, the encoder
(`--pages`, `--page-keys`, SPEC.md 98.2.1.8.4), the player, Mode X at 320 x
200, flipped AND built ahead (SPEC.md 98.1.3.7.2, 98.3.8.5, 98.2.1.8.5) and
the NARROW sixteen-colour screens with eight and six pages (SPEC.md
98.1.3.2.5). What is left is VIDEO-PAGES-PLAN §9's open list. The encoder half waited for the
encoder's charge rework and the PicoMEM 1 calibration, and went in on top
of them (VIDEO-PAGES-PLAN §8 says what each wave came to). It is
SPEC.md 98.1.3.7's page-ahead taken from ONE
hidden page to as many as the screen has, plus the two things that only
more pages make possible: Mode X at 320 x 200 (four pages) and tear-free
play with scene changes built ahead (`--flip` and page-ahead at once). The
measurement it stands on is docs/reports/VIDEO-PAGES-2026-10-15.md, and
`tools/os88vidpages.py` is both that measurement's instrument and the
encoder half's prototype.

## 0. The ask, and the answer

The owner's ask (2026-10-15): Mode X has three pages, and 320 x 200 Mode X
would have four. Page-ahead uses two. Use the rest:
- page-ahead AND double buffering at once;
- more than one expensive frame preloaded before the clock starts;
- two transitions back to back after a stretch that had room for both;
- in 320 x 200 sixteen colours, seven pages ahead - a short burst of action
  on its own.

The owner's argument: *either the video has room to fill the other pages or
it does not use them, so there is no downside*. Also: *videos so far were
encoded to what works; a calm video with several sudden transitions does
not work today, so none has been tried.* And: *320 x 200 Mode X is worth
building, to be complete.*

**The measurement agrees on all four**, on clips made to be what does not
work today. A few corrections and costs come with it:
- **The STREAM has no downside.** A page is built only from the bytes and
  cycles today's forecast already says no later frame needs, nearest target
  first. No run in the report cut more frames with more pages.
- **The KEYS do.** A key carries every page that is mid-build, a picture's
  worth each. Seven pages make a seek read ~250 KB where today's reads
  ~78 KB. VIDEO-PAGES-PLAN §3.4 is the rule that keeps that to the pages with a target
  ahead.
- **Seven pages at 320 x 200 sixteen colours need a 40-byte row layout.**
  The player stores every sixteen-colour screen 80 bytes a row, so 320 x 200
  holds 4 pages today, not 8 (VIDEO-PAGES-2026-10-15 §1). VIDEO-PAGES-PLAN §7.
- **Tear-free AND built ahead on three pages is a 486's**, as the owner
  guessed: the frame after a built flip owes a whole-canvas latch copy,
  102% of a 25 fps period on the fast 286 and 66% on the 486. **On FOUR
  pages the copy moves into the calm, and the fast 286 does it** (`still`
  16 -> 2 cut frames). That makes Mode X 320 x 200 worth more than
  completeness (VIDEO-PAGES-PLAN §5, VIDEO-PAGES-PLAN §6).
- **The targets need a new rule whatever is built** (VIDEO-PAGES-2026-10-15 §2). Today a
  target is each cut RUN's first frame, and on a slow machine two close
  cuts are one run, so the second is never a target. Taking every cut
  frame instead spends pages on scenes still being drawn and made one case
  worse.

## 1. Pages each screen has

The report's §1 is the table. In short: Mode X 320 x 240 has **3** pages,
Mode X 320 x 200 **4**, and sixteen colours at 320 x 200 / 320 x 240 have
**4 / 3** as stored today and **8 / 6** with 40-byte rows. 640 x 350,
640 x 400 and a lone Hercules have 2. Every page sits inside one VGA plane's
64 KB, so every CRTC start address a page needs fits its 16 bits.

## 2. What it buys

The report's §3 table, and its four findings:
1. **Back-to-back transitions**: two cuts 2 frames apart on the 5150's Mode
   X, 40 cut frames today -> 6 on three pages ahead (Mode X 320 x 200's
   count). Three cuts 5 frames apart, sixteen colours on the 5150, 79 -> 11.
2. **The preload**: key 0 holds every page's first target whole. The
   opening of a cut every 4 frames, Mode X on the slow 286, 25 -> 9 and its
   worst second 8.07% -> 1.47%.
3. **A burst**: one whole frame per page, built in the calm before it. A
   10-frame burst on the slow 286 at sixteen colours 320 x 200 goes 53 ->
   27 cut frames on seven pages.
4. **Tear-free and built ahead**: VIDEO-PAGES-PLAN §6.

## 3. The format

### 3.1 The header

- **Rendition byte 37 = 4: PAGE-AHEAD ON N PAGES**, with flag 8192 PAGE as
  3 has it. Today's player refuses any byte-37 value above 3 at open
  (`vp_parse`, the `.b3` arm), so an old player turns the file away in its
  own words. No flag bit is spent, and 32768 stays the last one free.
- **Rendition byte 39 = N**, the pages the file uses, 3 to 8. It is 0 in
  every file today. A player refuses a file whose N its screen cannot
  hold - on a 40-byte screen only by a reader that knows the layout, VIDEO-PAGES-PLAN §7.
- **Byte 37 = 5: FLIPPED AND PAGE-AHEAD** (VIDEO-PAGES-PLAN §6). N is at least 3.
- **Byte 37 = 3 is unchanged**: two pages, and today's file plays on
  today's player. An encode that never needs a third page writes a 3.

### 3.2 The records

- **Pages are named by their PHYSICAL index**, 0 to N-1, page i at
  i x [vp_page]. A relative name (the page after the shown one) would save
  nothing and would make a reader track a rotation as well.
- **A FLIP record (y0 bit 15) names the page it is drawn on and shows** in
  y0 bits 12-14. **A HIDDEN record names its page** in its own y0 bits
  12-14. The rows are bits 0-11 in a byte-37-4 file (4,095 rows; the
  tallest screen is 480). Today the rows are 0-14, so this changes the
  meaning of three bits only in a file an old player refuses.
- **A hidden record may chain**: its own y1 bit 15, which must be clear
  today, says another hidden record follows inside the same `len`. One
  frame then builds on two pages, which is what the model does when the
  nearest page is finished and spare is left (`PageAheadK.after`).
- Everything else holds: `len` covers the frame record and every hidden
  record, and the audio is still last, so every super-packet walk, `len`
  hop and audio read is unchanged (98.1.3.7's own argument, one level
  deeper).

### 3.3 The keys

A key is decoded onto every page the file uses, page 0 shown, and then its
chain of hidden records, each onto the page it names (98.1.3.7, page 1
there). KEYS IN PARTS (98.1.3.8) is unchanged, and a part's hidden records
are its band of each page.

### 3.4 The key rule: carry only pages with a target ahead

A key may leave a page out. **A page a key leaves out is UNKNOWN from that
key on**, in the encoder's model as in the player, until a build has
written every byte of it, in every plane. The encoder then builds a dropped
page from nothing rather than from what straight play would have left on
it, so a seek and straight play show the same thing.
- **Which pages a key carries**: every page assigned a target within the
  keyframe interval. In the report's runs, every page without one holds an
  old picture nobody will flip to, which is most of the ~190 KB a
  seven-page key carries.
- **What dropping costs**: a page built from nothing instead of from an old
  picture. That is near enough the same work after a scene change, and it
  is paid only by a page whose next target is past the next key.
- **Open**: whether a dropped page should take the key's own picture
  instead of "unknown". That would make the build incremental from the key,
  for a key's decode onto one more page at a seek. To be measured in wave
  1.

### 3.5 The targets: scenes, not runs

A target is each cut run's first frame, **plus every cut frame whose
picture differs from the frame before by more than 20% of its pixels**
(`--targets scenes`). The 20% is the measurement's, and it is not tuned:
wave 1 measures 10% and 30% on the report's clips, and the owner's real
clips decide it.

## 4. The player

**ESTIMATED +250 to +400 bytes of `video.o88`, and no kernel byte.**
1. **The page table**: page i's offset, i x [vp_page], computed at the mode
   set, plus `[vp_shownp]`, the shown page's index. Every `xor ax,
   [vp_page]` in `vp_pgdec`, `vp_pghid` and `vp_decboth` becomes a lookup.
   Today's two-page toggle is the special case N = 2.
2. **`vp_pgdec`**: a flip's page comes from y0 bits 12-14, and a hidden
   record's from its own. `vp_pghid` loops while y1 bit 15 chains.
3. **`vp_decboth`**: the key's lists onto every page the file uses, then
   its hidden chain.
4. **The text box (98.3.13) lives on the SHOWN page only.** A flip takes it
   off the page being left (`vo_io` back to the save) before the new page
   is shown, and puts it on the new one, which `vo_flipon` already does. So
   `vo_drawn` keeps two bits whatever N is, and the save stays one box
   (two during the flip). Today the page left keeps its box until it is
   drawn into as the hidden page (`vp_pgoff`), which with N pages would
   mean a save per page.
5. **The retrace wait**: one per flip, as today. A burst flips on
   consecutive frames, so two flips can land in one call (two frames a
   call). The second wait is then inside the same call, at most a refresh
   (~14 ms at 70 Hz). It is paid only on frames that were built ahead and
   so have their budget unspent, but the model does not charge it. Wave 2
   measures it on MartyPC's VGA XT.
6. **Hercules stays at two pages.** It has no third.

## 5. Mode X at 320 x 200

**The fourth page, and a picture 17% smaller.** It is 13h's 400-line timing
on an unchained card: the same sequencer and CRTC writes as Mode X, without
the vertical retime to 480 lines.
- **The player**: after `OSAPI_FSX_MODE(FSXM_MODEX)`, `vp_scrset` writes a
  vertical-timing table for 400 lines - `vp_vt400`'s values with CR09 at
  41h (two scan lines a row, 13h's) - exactly as it does today for the
  sixteen-colour screens. **No kernel byte**: the kernel's FSXM_MODEX stays
  320 x 240, and the bracket's restore sets the desktop's mode as always.
  `[vp_page]` = 16,000.
- **The header**: byte 38, "the screen", gains a Mode X meaning: 0 = 320 x
  240, 1 = 320 x 200, with flag 1024 SCREEN beside it as for sixteen
  colours. **An old player refuses it at open**: `vp_parse` takes a
  non-zero byte 38 only from a VGA4 file, so a Mode X one goes to its
  refusal (`.b1`) and nothing is drawn wrong.
- **The encoder**: a `modex200` preset, the box 320 x 200 on the Mode X
  layout, with `--screen 320x200` meaning the same thing it means for
  sixteen colours.
- **What it buys alone**: a frame 17% smaller to decode and to stream, on
  every machine.

## 6. Tear-free and built ahead (`--flip` with page-ahead, byte 37 = 5)

Two pages flip every frame (98.3.8) and the rest are built ahead. The
frame after a flip to a built page draws on a back page that has to be
brought up to the frame now shown, and after a scene change that is every
row: a whole-canvas latch copy (98.3.8.1).
- **On THREE pages** (Mode X 320 x 240) that frame pays the copy: 66% of a
  25 fps period on the 486, over 100% on any 286 (VIDEO-PAGES-2026-10-15 §5). **It is
  offered only where the encoder's profile pays it inside the period.** On
  a 286 a byte-37-5 file on three pages refuses at the encoder, in its own
  words.
- **On FOUR pages** (Mode X 320 x 200) the fourth page is copied from the
  built one in the calm, in bands, out of spare cycles. The frame after the
  flip draws on the copy and pays nothing. This needs a **COPY record**, a
  hidden record kind: "rows y0..y1 of page s onto page d, through the
  latches" - `vp_lcopy` with two page offsets, which it nearly is already.
  It is a few bytes in the stream where a re-decode would be a picture's.
- **The schedule rule**: the encoder refuses a flip whose copy is unpaid
  past what the next frame can carry (`--copy-ok` in the model). A
  silent flipped play that overruns its period stays behind for good
  (98.2.1.1.1). Run without that rule on the fast 286, every frame after
  the first flip was cut.
- **Measured** (VIDEO-PAGES-2026-10-15 §5): three pages on the 486, `still` 15 -> 2 cut
  frames and `pairs2` 21 -> 14 (-> 5 on four pages). Four pages with the
  copy in the calm on the fast 286, `still` 16 -> 2 and `pairs2` 26 -> 18.

## 7. Sixteen colours at 40-byte rows (8 and 6 pages)

**The owner's seven-ahead case is this wave.** The 320-wide sixteen-colour
screens are mode 0Dh, whose own row is 40 bytes. The player widens it to 80
(`vp_scrset`, the Offset, 98.1.3.2.1) so every sixteen-colour screen shares
`lin80`. A 40-byte layout halves the page, doubling the count:
- **The format**: a layout (`lin40`) or a stride field. The encoder's
  geometry is per layout already (`os88vid` `LAYOUTS`), and the player's
  row addressing goes through `vp_rowaddr` and the layout table's stride
  (`vp_laytab`). How many places assume 80 is the first thing wave 5
  counts, and that count decides whether this is a wave or a rewrite.
- **THE COUNT (2026-10-15): a wave, not a rewrite.** The decoders never
  compute a stride - a record carries byte offsets in the layout's own
  memory image, so `vd_native` and `vd_wmap` need nothing - and row and
  origin addressing already go through `vp_laytab`, `vp_rowaddr` and
  `Geom.base`. What hard-codes 80 is the setup around them:
  - **the player, ~8 sites**: `vp_parse`'s VGA4-is-LIN80 refusal, the page
    (`rows x 80`), the RAM plane image `vp_plsp`, `vp_scrset`'s Offset
    (`mov ax, 0x2813` - conditional now, Mode X at 320 x 200 keeping its
    28h), `vp_v4pack`'s poster row (`mov ax, 80`), `vp_lcopy`'s two row
    multiplies and `vp_flcw`; plus a `vp_laytab` row and its neighbours;
  - **`os88vid`**: a LAYOUTS entry with its aspect and pixels a byte,
    `Geom`'s `bitplanes` test, the VGA4-is-LIN80 checks in the writer and
    the reader, the screen checks tying the layout to screens 1 and 2, and
    `page_bytes`;
  - **`os88venc`**: the VGA4-is-LIN80 refusals, the presets, and the flip
    copy's cost and report formulas (`rows * 80`) - which are the encoder's
    charges, so that line waits with the encoder half;
  - **Live and the window stay on LIN80**: a screen of its own refuses both.
  `MPAGE_MAX` is 8 in the format and in the player, and eight pages of
  8,000 is 64,000 bytes, so nothing else binds.
- **What it buys**: VIDEO-PAGES-2026-10-15 §3, the K=5 and K=7 columns. Three back-to-back
  cuts on the 5150 gain from 3 pages ahead (21 cut frames) to 5 (11).
  Bursts gain on every page: 10 frames of action on the slow 286 go 45 ->
  37 -> 27 at 3, 5 and 7.

## 8. The waves

Each wave lands whole with its gate, and each gate goes red when the thing
it is about is broken on purpose (docs/WRITING-TESTS.md §1).

1. **The encoder and the reader, host only.** **The FORMAT half is BUILT**
   (SPEC.md 98.1.3.7.1): `os88vid`'s writer, reader, `verify_v88`,
   `decode_at` and `--selfcheck`, which writes four-page VGA4 and Mode X
   320 x 200 fixtures with chained hidden records and refuses five broken
   ones. One thing the design missed and the format now has: **a KEY NAMES
   THE PAGE SHOWN after it**, because physical pages are not relative and
   a play from a key must stand on the page straight play stood on. **The
   ENCODER half is BUILT** (SPEC.md 98.2.1.8.4) - `PageAheadN`, two-page
   encodes identical in every record and key, `videnc` 5j its gate - and
   VIDEO-PAGES-PLAN §3.4's key rule came out as an OPTION and not the
   default (`--page-keys started`): every target is handed to a page the
   moment one frees, so an IDLE page is only the end of a clip's, and the
   page worth leaving out is one whose build has not begun - which saves
   keys (970 KB -> 802 on a close-cuts clip) for stream bytes (655 -> 744)
   and leaves the largest key where it was. As planned it was: `PageAheadK` from the
   prototype into `os88venc` behind `--pages N` (default: what the screen
   has, with `auto` running it against the plain encode as 98.2.1.8.1 does
   today). The `scenes` target rule (VIDEO-PAGES-PLAN §3.5), for one page as well. The key
   rule (VIDEO-PAGES-PLAN §3.4). `os88vid`'s Writer and Reader for byte 37 = 4, and
   `verify_v88` playing every page. **Gate**: `videnc` rows that write an
   N-page file and verify it, and that FAIL with a hidden record written to
   the wrong page and with a dropped page not rebuilt whole. Layers
   (98.1.9) refuse N > 2 in this wave.
2. **The player. BUILT** (SPEC.md 98.3.8.4), **+333 bytes** of
   `video.o88` against the 250-400 estimated, no kernel byte. The text box
   kept its two saves and made them movable (VIDEO-PAGES-PLAN §4, item 4).
   **Gates**: `vidpage3mx`, `vidpage4v` and `vidpagetext4`, each red with
   the chain stopped after one record, a key shown on page 0, or the text's
   save left where it was.
3. **Mode X 320 x 200. BUILT** (SPEC.md 98.1.3.1.1), **+103 bytes** and no
   kernel byte, and `os88venc --preset modex --screen 320x200`. **Gate**:
   `vidpage4mx`. The planned gate could not work: MartyPC scans Mode X's
   480 lines into 400, so the glass's first 200 rows are the same at
   either height, and the gate reads the CRTC's vertical display end
   instead (8Fh, red at DFh with the 400-line table left out).
4. **Flip and build ahead. BUILT** (SPEC.md 98.1.3.7.2, 98.3.8.5,
   98.2.1.8.5): byte 37 = 5, **+379 bytes** of player and no kernel byte.
   Two things the design did not have: a frame record must say which flips
   are to a BUILT page (y1 bit 12), because only those skip the bring-up,
   and the bring-up's two ways (the last record again, or its rows copied
   off the glass) only agree if a writer keeps one rule - after the last
   record's lists the page IS the page shown - which a reader checks. The
   COPY is a hidden record with y0 bit 15 and its header alone. Three things
   the encoder found: a build must spend what is left AFTER the frame's
   bring-up, or the first flip collapses the flip schedule for good; the
   player must keep and price the record's own lists only; and on two
   builders the nearest target keeps one for its copy, or the next target's
   build takes it and every flip is refused. The measured figures are the
   prototype's (§6). **Gates**: `vidpagefmx3`, `vidpagefmx4`, `vidpagefv4`
   and `vidpagetextf` on MartyPC, `videnc` 5k on the host, and
   `os88vid --selfcheck`'s four-page fixture - each red when broken on
   purpose (a COPY a bare `ret`, a BUILT page brought up, `vo_cfix` a bare
   `ret`, the calm's copy off, the bring-up skipped).
5. **Sixteen colours at 40-byte rows. BUILT** (SPEC.md 98.1.3.2.5),
   **+67 bytes** of player and no kernel byte. It came out as two new
   SCREENS (5 and 6, `--screen 320x200n` and `320x240n`) rather than a
   layout: the layout's bit-planes and every record are LIN80's, only the
   stride differs, and a screen byte past 4 is what a player before this
   refuses at open. The player keeps the stride in LIN80's own row of
   `vp_laytab`, which every row address already read, so of the count's
   eight sites five changed and the flipped copy's three are never reached
   (a NARROW file refuses byte 37 = 2). **Gates**: `vidpage8n` (seven scene
   changes three frames apart on eight pages, every hold exact, the CRTC's
   Offset 14h - red at 28h with the narrow Offset left out) and
   `vidpage6n`; `os88vid --selfcheck` writes an eight-page NARROW file and
   refuses an 80-byte canvas on one and a flipped one.

## 9. Open

- **Real clips: ANSWERED** by docs/reports/VIDEO-PAGES-REAL-2026-10-15.md -
  the owner's five through the shipped encoder on the Victory's profile.
  More pages never made one worse; in sixteen colours every page helped
  (LastExileOP 110 -> 90 -> 61 cut frames at two, four and eight pages,
  Thundercats 185 -> 144 -> 101), Mode X's third page helps the calm clips
  (Bebop 13 -> 9, GasGas 13 -> 8) and not the busy ones, which the
  per-frame ceiling cuts. The real clips do have close scene changes. The
  price is the keys: four to five times the front matter at eight pages.
  Still open: the same files timed on the machine, and the clip the
  feature is for.
- **The retrace wait on consecutive flips** (VIDEO-PAGES-PLAN §4, item 5), measured on MartyPC.
- **Keys**: VIDEO-PAGES-PLAN §3.4's open question. Measure the seek on the 5150's ST-225
  for the K=3 and K=7 files with and without the rule.
- **Byte 39 and what a reader refuses**: settled by wave 5 - a
  sixteen-colour file of 5 or more pages is a NARROW screen's, and the
  writer and the player both refuse more pages than a plane holds.

## 10. Keys: the page pool (2026-10-16)

**The owner's question**: do the keys' pages overlap - is the same page
stored in several keys? **Yes, mostly.** Every key of the 25 real-clip
encodes (docs/reports/VIDEO-PAGES-REAL-2026-10-15.md) decoded, each carried
page against the same page in the key before:

| clip, pages | carried | unchanged since the last key | KB |
|---|---:|---:|---:|
| Bebop, 16 colours, 2 | 46 | 27 | 469 of 806 |
| Bebop, 16 colours, 4 | 138 | 111 | 2,085 of 2,582 |
| Bebop, NARROW, 8 | 322 | 287 | 5,299 of 5,974 |
| LastExileOP, NARROW, 8 | 364 | 248 | 5,257 of 7,570 |
| TakeOnMe, NARROW, 8 | 147 | 66 | 1,879 of 4,081 |
| Thundercats, NARROW, 8 | 273 | 68 | 1,267 of 5,691 |
| GasGas, NARROW, 8 | 252 | 243 | 1,106 of 1,167 |

- **A repeat is always the same page standing still** - a picture never
  moves to another page and comes back, and pages inside one key are
  almost never the same picture (63 KB of all of them, GasGas's).
- **A page carried is mostly a page later shown.** Of Bebop's 322 at eight
  pages, 232 are flipped to before anything writes them, 89 are never
  touched again (1,750 KB) and 1 is rebuilt first. The never-touched ones
  are what `--page-keys started` leaves out already.
- **What a pool would save**, each distinct picture stored once as a key
  record of its own over black and a 12-byte reference per carried page:
  Bebop at eight pages 91% of the keys' pages (6,517 KB of front matter to
  1,094), GasGas 96%, LastExileOP 74% (8,269 to 2,702), TakeOnMe 45%,
  Thundercats 32% (6,328 to 4,512). The busy clips save least because their
  pages change between keys.
- **The bytes are not the same bytes**: a key's hidden record is a page's
  difference from THAT key's picture, so a repeat cannot be found by
  matching records - it is the picture that repeats.

**BUILT (2026-10-16)** - SPEC.md 98.1.3.8.1 is the format (flag KPOOL,
the pool after the key records, a reference in a key's last part's hidden
chain), 98.3.8.6 the player (+321 bytes), `--key-pool` the encoder's
switch, on by default. **MEASURED** by docs/reports/VIDEO-PAGES-KEYS-2026-10-16.md:
it changes the keys and nothing else - the same frames cut, the stream to
the kilobyte - and their front matter falls 83% on Bebop at eight pages,
66% on Last Exile, 23% on Thundercats. `--page-keys started` beside it
saves a few hundred KB more and costs frames on the busy clips, so the
defaults are `all` and the pool. Its cost is the
seek, which reads up to one picture per page where it read one key in parts
- about as many reads and as many bytes, from more places.

