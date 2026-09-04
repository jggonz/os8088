# The Wire — the online software library

**Status: design record, binding on the implementation.** The Wire is a
desktop icon that opens a window listing every program the project publishes,
fetched over `ETHER.DRV` from `os8088.com`, with a picture, a description, a
recommended-machine filter and two actions: run it now from memory, or add it
to a disk. A networked XT with one 360KB floppy reaches the whole collection.

The interface contracts this file pins move into SPEC.md as the work lands
(§26 for the desktop zone, §21 for the launcher slot, a new §87 for the
package) and SPEC.md then owns them. Until then this file is the authority,
and it stays afterwards as the record of *why*.

Brand, fixed by the user and not to be reworded:

| where | text |
|---|---|
| desktop icon caption | `Wire` |
| window title | `The Wire` |
| one-line description (About card, website) | `Online Software Library` |
| tagline (About card, website hero) | `Software by wire.` |
| status while connecting | `Connecting to Wire...` |
| status while fetching | `Loading from the Wire...` |
| status when the catalog is up | `Available on the Wire: N programs` |
| a new entry's mark | `New on the Wire` |
| no driver / no link / no answer | `Wire connection unavailable` |
| the run action | `Load Program` |
| the save action | `Add to Disk...` (dialog lead: `Add this program to your disk`) |

Three dots, not an ellipsis character: the machine's font is ASCII 0x20..0x7E.

---

## 0. The verdict, and what the exploration found

Four facts decided the shape (the exploration reports are the evidence):

1. **There is no desktop-icon mechanism.** The desktop zones ARE the volume
   table (`dsk_vtab` rows with `DV_FLAGS` bit 0, SPEC §26.1); no Trash, no
   application icon, and `OSAPI_VOL_ADD` is fenced to drivers. So the kernel
   gains a second zone species a DRIVER registers (the shape of
   `OSAPI_VOL_ADD`, §2), `ETHER.DRV` registers and paints the Wire's, and
   the click is the shape of `ui_tm_open` (SPEC §28's chip-menu launch of
   `TASKMGR.O88` by name from the boot volume's `SYSTEM/`).
2. **There is no way to launch a package image already in memory.** The
   loader (`kernel/loader.inc`, SPEC §21) is a disk pipeline end to end, and no
   `OSAPI_*` slot launches anything. "Load Program" needs one new slot,
   `OSAPI_PKG_RUN`, which is the loader's back half with the disk read
   replaced by a copy.
3. **The site already serves plain HTTP/1.0** with `Content-Length` and
   `Connection: close` on port 80 with no redirect to TLS (verified with
   `curl --http1.0` against `os8088.com`), so the machine talks to the
   origin directly, the way the browser does, with the name resolved inside
   `NETV_OPEN`. No proxy, no TLS, nothing new on the wire.
4. **`apps/wire/` is taken** — it is WIREFRAME (SPEC §78), an instrument
   built by `all` and driven by three soak rows. It is not renamed. The
   package is `apps/thewire/thewire.asm` → `THEWIRE.O88`, header name
   `The Wire`. Nothing the user sees carries the file name.

**Modular by requirement.** Most XTs have no network card, so a machine
without one must not pay in kernel space or RAM: the kernel's whole share is
a generic driver-registered desktop zone (≤ 200 bytes) and the memory
launcher's delta over the disk loader (≤ 150 bytes, cold); the icon, its
painter and the launch name live in `ETHER.DRV`; the package is on the disk
and in RAM only while it runs. What it costs is measured, not estimated, and
stated in SPEC §26 and §21 when it lands: `tools/kernsize.py` before and
after, both kernels. The image rung had 284 bytes and the cold rung 66 when
this was written, so a rung may still be crossed; the number is reported.

---

## 1. Architecture — four parts, three repos' worth of boundaries

```
 kernel/desk.inc            kernel/loader.inc          apps/thewire/            ../os8088-web
 ┌──────────────────┐       ┌──────────────────┐       ┌──────────────────┐     ┌──────────────────┐
 │ the Wire zone    │ dbl-  │ OSAPI_PKG_RUN    │       │ THEWIRE.O88      │ HTTP│ /wire/catalog.bin│
 │ (paint, hit,     │ click │ (image in memory │◄──────│  catalog, list,  │◄────│ /wire/pic/*.PIC  │
 │  double-click)   │──────►│  → running       │ WM_   │  picture, filter,│     │ /wire/pkg/*.O88  │
 │                  │ launch│  instance)       │ONWAKE │  Load / Add      │     │ /wire/  (page)   │
 └──────────────────┘       └──────────────────┘       └──────────────────┘     └──────────────────┘
        K1                          K2                          P                        W
```

- **K1** and **K2** are the only kernel changes. Neither knows what HTTP is,
  and neither knows the word Wire: K1 is a generic driver-registered zone
  that `ETHER.DRV` fills, K2 a launcher any package may call.
- **P** is an ordinary shipped package, on the SYSTEM disks (all four
  geometries) in `SYSTEM/` beside `TASKMGR.O88`; not on `kern_small` (no
  NIC there, `SMALLOMIT`'s reason). It is the only reader of the catalog
  format on the machine and the only caller of `OSAPI_PKG_RUN` today.
- **W** publishes the catalog, the pictures and the packages as static
  assets, and replaces the site's Applications page with a `Wire` page.
- The catalog format (§4) is the contract between P and W. The OS repo owns
  it: `tools/os88wire.py` packs, verifies and dumps it (the fixture the OS
  tests fetch is built with it), and the website's own packer is an
  independent second writer of the same bytes. `os88wire.py --verify` run
  against the site's published `catalog.bin` is the cross-check, the way
  `tests/unit/t_wab.py` is the independent second reader of a `.WAB`.

---

## 2. K1 — a driver-registered desktop SERVICE zone (SPEC §26.x)

**The kernel learns nothing about the Wire.** Most XTs have no network card,
and a desktop icon whose glyphs and painter sit in every kernel would charge
those machines for a feature they cannot use. So the kernel gains one small
GENERIC mechanism — a desktop zone a DRIVER registers — and `ETHER.DRV`
(and `NET.DRV`, the parallel cable, through the same shared include) is what
registers the Wire's. No driver, no zone, no bytes: the glyphs, the caption
and the launch name are the driver's, in RAM only on a machine that loaded it.

- **`OSAPI_DESK_SVC`** — the second new slot, fenced to drivers like the
  `OSAPI_VOL_*` family (a package gets CF=1 and nothing else):
  ```
  in   AL = 1 add / 0 withdraw
       ES:SI = (add) a record in the DRIVER's segment:
               +0  12  caption, NUL-terminated (<= 11 chars; 'Wire')
               +12 13  the 8.3 file the zone launches from the boot
                       volume's SYSTEM/, NUL-terminated ('THEWIRE.O88')
               +25  1   the driver's DRVC_* class
               +26  1   the verb the kernel calls to PAINT the zone's icon
  out  CF = 1 refused: not a driver, or a service zone is already registered
       (ONE record, stated as the limit; room to grow is a count, later)
  ```
  The kernel keeps `desk_svc` in `.bss`: the record's segment and offset (0 =
  none). Withdraw on detach (the driver's own teardown) and `OSAPI_VOL_PAINT`
  or the equivalent repaint so the zone leaves the glass.
- **The zone** takes the index after the last volume in the same column flow
  (§26.1's wrap included), the same `desk_zone_rect` / `desk_dmg_zones` /
  `desk_zone_hilite` / damage-mask paths as a volume, and is not drawn when
  eight volumes exist (the one-byte mask — a fact, stated in SPEC).
- **Painting:** `desk_draw_zone` on the service zone calls the driver's paint
  verb through the kernel's own driver-call path with CX,DX = the icon's
  top-left and the gfx lock held (desk_paint's context), then letters the
  caption itself from the far string (copy ≤ 12 bytes to the stack, one
  `font_str`), white rect hugging the caption (§26.4). The DRIVER draws the
  glyph: 32 rows on VGA/EGA/Hercules, 14 on CGA (§26.4, read `[vid_h]`),
  with the icon slot or `gfx_*` calls it already has access to, black
  outline, white body — a spool of cable with a plug, or a handset.
- **Double-click** (§26's `DESK_DBLT`) → `ui_sys_open` with SI = the file name
  copied out of the driver's record: **refactor `ui_tm_open` into a
  name-taking routine** shared with the chip menu's Task Manager item
  (single instance by the package's header name — read it from the running
  instance table; the launcher already resolves the file; restore if
  running, else quiet-mount the boot volume, `SYSTEM/`, `ld_run_name`,
  restore the user's folder; failure notice lead `Cannot open <caption>:`).
- **`kern_small` leaves the species out** (`%ifndef KERN_SMALL`; it has no
  NIC driver and the Wire is in `SMALLOMIT`). Knob kernels must assemble.
- **Budget:** the kernel's resident share ≤ 200 bytes across `.text`/`.cold`
  including the slot; measure with `tools/kernsize.py` before and after and
  put both lines in SPEC §26.x. The driver's share (`drivers/wirezone.inc`,
  included by ETHER.DRV and NET.DRV: the record, the two glyphs, the paint
  verb, the attach-time register and detach-time withdraw) is measured too,
  stated in SPEC §72/§62's size notes.
- **Look at it on all three adapters before calling it done** (§26.4, §47.2),
  with the driver loaded (`make ethertest`'s disk) — and confirm a plain
  `os8088.img` boot shows NO zone and the kernel's bytes are the only cost.

## 3. K2 — `OSAPI_PKG_RUN`, slot `KERNEL_SEG:0x04F8` (SPEC §21.x)

The 158th slot; `osapi_table_end` moves down one and `OSAPI_TABLE_LEN`'s
assertion with it; `tests/unit/t_api_abi.py` decodes the table out of
`kernel.bin` against the SDK, so the `%define` and the table entry land
together.

```
OSAPI_PKG_RUN   KERNEL_SEG:0x04F8
  in   ES:SI  = a package image, byte for byte what the .O88 file holds,
                in a claim of YOURS (any segment; it is COPIED, never adopted)
       DX:CX  = its length in bytes (DX = high word)
       DI     = a NUL-terminated 8.3 name in YOUR segment, <= 12 chars —
                what the new instance is told it was launched from
                (OS88_HEADER's SI on entry, SPEC §20.2); e.g. 'HELLO.O88'
  out  CF = 0: AX = 0, the instance is registered, published and its window
               shown, exactly as a Disk-window double-click leaves one
       CF = 1: AL = LD_* (SPEC §21.4's codes): LD_EBAD a header that
               ld_check_hdr refuses, or flags bit 2 set (a package with
               PARTS reads them from its own FILE, §20.12, and has none here);
               LD_EBIG over APP_MAX_SIZE; LD_ENOMEM no region or no instance
               record; LD_EABORT the entry proc declined
  context: UI TASK ONLY, from a window callback or an OSAPI_WM_ONWAKE
           handler. The gfx lock state on entry is whatever
           loader_run_x's caller holds — pin it after reading kernel/ui.inc
           around line 590, and say so in SPEC.
```

- Implementation: split `ld_run_body_x` so the region-reserve / header-check
  / bss-zero / entry-call / register-publish-show half is a routine both the
  disk path and this slot enter; this slot replaces "read the file into the
  region" with a far `rep movsb` from ES:SI. The disk-swap re-check is moot
  and is skipped. **No relocation, no adoption of the caller's claim** — the
  region is claimed top-down by `mem_claim_hi` like every package region
  (§20.1: its base IS its CS).
- The new instance's current directory is the CALLER's at the time of the
  call (§19.2.1), so a package with an overlay or sidecars run this way would
  look for them in the Wire's folder and refuse cleanly; the Wire never
  offers Load Program for one (§7).
- Cold segment. The split makes the back half SHARED with the disk path, so
  the resident delta is the copy loop and the argument marshalling: budget
  ≤ 150 bytes. Measured cost into SPEC §21.x. It stays resident (a module
  for 150 bytes costs more in thunks than it saves), and it is the ONLY
  Wire-shaped byte a cardless machine carries in RAM.
- **Capability gate `tests/pkgrun/`** (`make pkgrun`, the `mseg`/`covl`
  shape): a test package that reads `HELLO.O88` from beside itself into a
  claim, calls `OSAPI_PKG_RUN`, and reports on its own window what came back;
  then corrupts the magic and asserts `CF=1, AL=LD_EBAD`; then a flags-bit-2
  image and asserts the same. `tests/pkgrun.py` drives it under QEMU and
  asserts a `HELLO` instance exists via `OSAPI_SYS_SNAPSHOT`'s record or the
  window title on the glass. Register the row (soak, `needs=("qemu","pkgrun")`
  or whatever capability name fits `tools/os88test.py`'s pattern).

## 4. The catalog — `catalog.bin`, format version 1

Little-endian throughout. Fixed offsets so the 8088 reads with `mov` and
never parses. Every text field is ASCII 0x20..0x7E, NUL-padded to its width,
and the writer refuses anything else. The reader refuses on any check below
with `Catalog not understood` and keeps the window usable.

```
HEADER, 32 bytes
 +0   4   'WIRE'
 +4   1   format version, 1
 +5   1   record size in 16-byte units, 16 (= 256)
 +6   2   record count N (1..255)
 +8   2   header size, 32
 +10  2   sidecar table offset from file start
 +12  2   sidecar table entry count S
 +14  8   catalog date 'YYYYMMDD'
 +22  10  zero

RECORD i, 256 bytes at 32 + 256*i
 +0   8   stem, uppercase A-Z 0-9 _ -, space-padded ('HELLO   ');
          the package file is /wire/pkg/<STEM>.O88 and the picture
          /wire/pic/<STEM>.PIC
 +8   24  title, e.g. 'Browser', 'Tank Attack' (<= 23 chars)
 +32  1   kind: 0 program, 1 game, 2 utility, 3 document, 4 update,
          5 shared file.  Only 0..2 are used today; the field is the room
          the brand leaves to grow. A reader shows kinds it does not know
          as programs.
 +33  1   tier: 0 = 8088/8086, 1 = 286, 2 = 386, 3 = 486+ — the machine
          this program is RECOMMENDED for. The filter 'X' shows tier <= X.
 +34  1   flags
          bit 0  WF_DISK    needs its files on a disk: an overlay, parts
                            (header flags bit 2) or sidecars. Load Program
                            is refused with that reason; Add to Disk works.
          bit 1  WF_PIC     /wire/pic/<STEM>.PIC exists
          bit 2  WF_NEW     new on the Wire (the site marks its newest)
          bit 3  WF_FLOPPY  only as a floppy image from os8088.com — its
                            files live in a folder tree the Wire does not
                            create (RunCPM's A/0/). Both actions refused.
 +35  1   sidecar count n, 0..8
 +36  2   first sidecar index into the table (meaningless when n = 0)
 +38  4   size of <STEM>.O88 in bytes
 +42  4   total bytes, the .O88 plus every sidecar — Add to Disk refuses
          before touching the disk when OSAPI_FILE_DFREE is short
 +46  2   zero
 +48  64  icon: 16 mask words then 16 data words, bit 15 = leftmost,
          the package's own OS88_ICON16 block (file offset 32..95 when
          header flags bit 0 is set) or the site's generic program icon.
          Drawn per list row with OSAPI_ICON_DRAW.
 +112 140 description: 5 lines x 28 bytes, each NUL-terminated within its
          28 (<= 27 chars). PRE-WRAPPED BY THE WRITER; the machine wraps
          nothing. Unused lines are all-NUL.
 +252 4   zero

SIDECAR TABLE entry j, 16 bytes at (header +10) + 16*j
 +0   12  NUL-terminated 8.3 name, uppercase, e.g. 'WEAVE.OVL'
          — fetched from /wire/pkg/<NAME>, written beside the .O88
 +12  4   size in bytes
```

Limits the reader enforces: whole file ≤ `WIRE_CATMAX` = 16,384 bytes
(the claim is made before `Content-Length` is known); N ≤ 255; sidecar
indices in range; every size ≤ `WIRE_FILEMAX` = 64,512 (63 KB: one claim,
one `OSAPI_FILE_WRITE`) — a larger file gets `WF_FLOPPY` from the writer,
which is the site's job and the verifier's check.

`tools/os88wire.py`: `--pack manifest.json --pkgdir DIR --out catalog.bin
[--picdir DIR]` writes a catalog from a JSON manifest (the same schema the
website uses, §8) and the package files; `--verify catalog.bin [--pkgdir]`
checks every rule above and, with `--pkgdir`, that sizes and icons match the
files; `--dump` prints it. `--pic in.png --crop X,Y --out STEM.PIC` cuts a
picture (§5) from a screenshot with a stdlib-only PNG reader (the site's
captures are 1-bit or 16-colour PNGs, zlib is in the stdlib).
`tests/unit/t_wire.py` round-trips pack → verify → dump on a fixture and
checks that `apps/thewire/thewire.asm`'s `WC_*` record-offset `equ`s equal
the tool's (the "constant mirrored in two files" pattern the fast tier runs).

## 5. The picture — `<STEM>.PIC`

1,024 bytes, raw: **128 x 64 pixels, 1 bit per pixel**, 16 bytes a row, bit
7 of a byte its leftmost pixel, **1 = ink (black)**, 0 = paper. No header;
the reader refuses any other `Content-Length`. It is a 1:1 crop of a real
screenshot (the site's captures are what the machine drew), never a scaled
one — a scaled 16-colour UI is mush, a crop is the program.

Drawing it: `OSAPI_GFX_BLIT1` first (framebuffer bit order, x and width
multiples of 8 — 128 is; convert polarity to what §5's slot wants for the
adapter), and when it answers CF=1, `OSAPI_GFX_BLIT4` through a nibble-
expanded scratch row, which is correct on every adapter and slow only where
the fast slot is refused. Measure both on the XT figures in PERFORMANCE.md
and put the numbers in §87.

## 6. HTTP profile — what the Wire sends and accepts

Request, exactly:
```
GET /wire/<file> HTTP/1.0\r\n
Host: <host>\r\n
User-Agent: os8088 Wire 1.0\r\n
Connection: close\r\n
\r\n
```
Response: status from columns 9..11 of the first line (`br_hdrb`'s scan is
the model), `Content-Length:` required (case-insensitive header match on the
first 14 bytes; the value bounds the body and drives the progress figure),
headers end at CR LF CR LF, body until the peer closes or `Content-Length`
is reached. **No chunked decoding, no redirects, no keep-alive** — a 3xx or
4xx is reported as `The Wire did not answer (NNN)`. Any body longer than its
claim is truncated and the transfer marked failed. One socket at a time.

Host, port and path prefix come from `WIRE.CFG`, read at launch from
`SYSTEM/APPDATA/` (SPEC §19.9 — the package's own state, not a document);
one line, `host[:port][/prefix/]`, default when absent `os8088.com:80/wire/`.
The gate's disk carries `10.0.2.2:8092/wire/`. The name goes to `NETV_OPEN`
as is — resolution is the driver's (`drivers/net/netpkg.inc`'s contract).

## 7. P — the package `apps/thewire/`

**Size:** the 360KB system disk has **17 free clusters** today. The image is
≤ 12,288 bytes so the disk keeps ≥ 4 KB free; `os88disk.py` refuses the image
otherwise, which is the enforcement. bss ≈ 2.5 KB: a 1,024-byte receive
staging buffer (`NETV_RECV` wants ES:DI in the package's own segment), the
1,024-byte picture, the line/scroll blocks and the state.

**Memory (UI task claims everything, §20.6 rule 7):** the 16 KB catalog claim
at launch; per transfer, one claim of the exact size from the catalog, freed
after the launch or the write. Peak on a Load is Wire + catalog + file claim
+ the new instance's region, ≈ 12 + 16 + 2 x size KB.

**Tasks:** the browser's division (SPEC §71.1). The UI task claims, sets
`[wr_req]`, kicks the worker; **one worker** (`OSAPI_TASK_SPAWN` retried until
granted, `OS88_STACK_192`) opens, polls `NETV_STATUS`, sends, drains into the
staging buffer and copies to the claim, and paints the status cell and the
picture under the lock with the obscured/clip tests of §20.6 rule 5. Done or
failed → `OSAPI_WM_WAKE`; the `OSAPI_WM_ONWAKE` handler (UI task, no lock) is
the only place that calls `OSAPI_PKG_RUN`, `OSAPI_FILE_WRITE` or
`OSAPI_MEM_FREE`. The handshake is `apps/ftpd`'s one byte — every argument
written before the flag, the flag cleared last — and a generation counter
checked INSIDE the store loop (SPEC §71.11's lesson), so a selection change
mid-picture cannot land bytes in the wrong buffer.

**States** (`[wr_state]`): IDLE, CONNECT, SEND, HEAD, BODY, DONE, FAIL; and a
request kind (`[wr_kind]`): CATALOG, PIC, FILE. Strings by state:
`Connecting to Wire...`, `Loading from the Wire...` (+ ` NNK of MMK` in the
same cell as bytes arrive), `Available on the Wire: N programs`,
`Wire connection unavailable`, `The Wire did not answer (NNN)`.

**Window** — title `The Wire`, content 384 px wide, sized from `[vid_h]` so
it fits the CGA's 200 rows with at least 6 list rows and takes 12 on VGA:

```
 Show: (*) All  ( ) 8088/8086  ( ) 286  ( ) 386  ( ) 486+        <- os88ui radio row
 +-------------------+ +----------------------------------+
 | [i] Browser    NEW| | [ 128 x 64 picture, or the      ] |
 | [i] Calculator    | | [ words 'No picture'           ] |
 | [i] Hello         | | Browser            8088   15K    |     title, tier, size
 | [i] Minesweeper  ^| | A web browser for the IBM PC.    |     5 x 27-char lines
 | [i] Paint        #| | Plain HTTP, ...                  |
 | [i] Solitaire    v| |                                  |
 | ...               | | [ Load Program ] [ Add to Disk...] |
 +-------------------+ +----------------------------------+
 Available on the Wire: 31 programs                              <- status cell
```

- List pane 160 px (icon + 17 chars), rows 12 px, `OSAPI_ICON_DRAW` for the
  icon, one `OSAPI_FONT_RUN` per row (never erase-then-letter, §6.1), the
  selection an XOR fill; a `NEW` tag right-aligned for `WF_NEW`. os88ui
  scroll bar with `OS88UI_SCROLL` + `OS88UI_SBDRAG`; keys: up/down move,
  PgUp/PgDn, Home/End, Return = Load Program.
- **Filter** = five radios (`os88ui_glyph`), `All` default; radio X lists
  records with tier ≤ X, in catalog order. Changing it repaints the list pane
  only. The machine's own tier (`OSAPI_CPU_INFO`) is shown as a fact in the
  detail pane when it is below the program's — `This machine: 8088` under the
  tier line — and never refuses anything (§60.2: the tier is information).
- **Detail pane:** picture (or `No picture`), title, tier word
  (`8088`/`286`/`386`/`486+`), size in K, the five description lines, then
  the two buttons. A selection change repaints the detail pane and the two
  rows whose selection changed, nothing more (PERFORMANCE.md Part 5).
- **The two buttons and §47's one predicate, three consumers:** one routine
  answers, for the selected record, "may Load?" / "may Add?" with a reason
  string; the painter greys on it (`OSAPI_GFX_PEN`), the click refuses on it,
  and the reason is what the status cell says when a greyed button is
  clicked. Reasons: no selection; no catalog; a transfer in progress;
  `WF_DISK` → `Needs its files on a disk - use Add to Disk`; `WF_FLOPPY` →
  `Available as a floppy from os8088.com`; disk free < total → `Needs NNNK
  free on the disk` (checked in the Save completion, where §38's DX:CX and
  `OSAPI_FILE_DFREE` make it knowable before the first byte moves).
- **Load Program:** claim → fetch `/wire/pkg/<STEM>.O88` → ONWAKE →
  `OSAPI_PKG_RUN` with `DI = '<STEM>.O88'` → free → status `Loaded <title>
  from the Wire` (toast too, `OSAPI_TOAST`). An `LD_*` refusal is said in
  the status cell in words.
- **Add to Disk...:** `OSAPI_FILE_DLG` Save, default name `<STEM>.O88`
  (§38.6 — from a click, lock held, completion much later; the dialog moves
  the instance's directory to the chosen folder, so every later write lands
  there). Completion: free-space check, then the chain: the .O88 under the
  chosen name, then each sidecar under its own; each file is claim → fetch →
  ONWAKE writes with `OSAPI_FILE_WRITE` → free → next. Status `Adding <title>
  to your disk... 2 of 4 files`; on the last, toast `<title> added to your
  disk`. A failure mid-chain leaves what was written and says which file
  failed; there is no undo on this system (§22).
- **Picture on selection:** when the worker is idle and the selected record
  has `WF_PIC`, fetch its `.PIC` into the bss buffer and draw it; a second
  selection change while it is in flight bumps the generation and the worker
  drops the bytes. A record without a picture draws `No picture` at once.
- **Refresh** (File menu): re-fetch the catalog. **About**: `OSAPI_ABOUT_SET`
  (SPEC §12.2 — every package has one; the lesson in CLAUDE.md) with the
  lines `The Wire`, `Online Software Library`, `Software by wire.`, the
  catalog date, `os8088.com`.
- **No driver, no link:** `net_find` CF=1 or `NETV_STATE` without `NSTF_SOCK`
  at launch → the window opens with the status `Wire connection unavailable`,
  the list empty, both buttons greyed, and the detail pane carrying three
  lines a person can act on: `The Wire needs ETHER.DRV and a network card.`
  / `Control Panel > Drivers turns it on.` / `Or browse it at os8088.com/wire`.
  Refresh retries. Never a modal alert for an absent card.
- Menu: `File`: `Refresh`, `Load Program`, `Add to Disk...`, `Close`.
  `MENU_DIS` prefixes follow the same predicate as the buttons.
- Every string in one `wrtxt.inc`; every record offset a `WC_*` equ mirrored
  by `tools/os88wire.py` (§4).
- **Look at the window on Hercules and CGA before calling it done** — the
  greyed buttons are checkerboards there, the radio dots are rings, and the
  CGA window must fit.

## 8. W — the website (`../os8088-web`)

- **`data/wire.json`** — the one place the library is described. Per entry:
  `stem`, `title`, `kind`, `tier` (0..3), `flags` (`new`, `floppy_only`),
  `files` (the `.O88` first, then sidecars; names as on the OS disks),
  `summary` (one sentence), `description` (the five catalog lines are
  wrapped from it at ≤ 27 chars, and the writer refuses a summary that needs
  more than five), `page` (rich HTML for the site), `screenshot` (path under
  `public/img/`), `crop` (`[x, y]`, the picture's top-left in that
  screenshot), `spotlight` (a `/spotlight/<name>/` link or null).
- **`tools/wire.py`** — called by `build.py`: packs `public/wire/catalog.bin`
  and `public/wire/pic/<STEM>.PIC` from `data/wire.json` and
  `public/wire/pkg/`, byte for byte per §4 and §5 (stdlib only — CI is a bare
  Python 3.12), and refuses a manifest that names a package file that is not
  there, a description that does not wrap, a file over 63 KB without
  `floppy_only`, or an entry whose files carry a sidecar without a `WF_DISK`.
  Deterministic output, committed under `public/` like every other build
  product (the deploy job's `git status --porcelain public/` check).
- **`tools/release.py`** — copies every file `data/wire.json` names from
  `<os-repo>/build/` into `public/wire/pkg/` (the `.O88`s and the sidecars:
  `.OVL`, `.WSM`, `.WPV`, `.WAB`, the C64 `README.TXT`/`COPYING` if listed),
  the way it copies floppy images into `public/disk/`. Refuses a name it
  cannot find. The catalog date is the release date.
- **`site/wire.html`** at `/wire/` replaces `/applications/`: nav entry
  `("Wire", "/wire/", 1)` in `tools/build.py`'s `NAV` in place of
  Applications; `public/_redirects` gains `/applications/ /wire/ 301` (both
  slash forms); the footer dock link follows; `site/applications.html` and
  its output are removed. The page: the hero (`The Wire` / `Online Software
  Library` / `Software by wire.`), a short block on what it is on the machine
  (the desktop icon, `ETHER.DRV` and a card, `Load Program` versus `Add to
  Disk`, the 360KB-floppy point), **the same filter** — `All`, `8088/8086`,
  `286`, `386`, `486+` — as buttons that filter the cards client-side with a
  no-JS fallback of showing all, and one card per entry rendered by
  `build.py` from `data/wire.json` through a `<!--WIRE_LIBRARY-->` marker
  (the `RELEASE_HISTORY` pattern): the 640x480 screenshot in a `.win.shot`
  figure, title, tier, sizes, the rich `page` HTML, a `NEW` mark, a
  spotlight link, and a `Download <STEM>.O88` link (plus sidecar links) to
  `/wire/pkg/` — the first time the site hands out a package on its own.
  Voice per `BRIEF.md` §1 and `reference/magazines/`: numbers, no banned
  words, every block in a `.win`.
- **`public/_headers`**: `/wire/pkg/*`, `/wire/pic/*` and `/wire/catalog.bin`
  get `Cache-Control: public, max-age=600` and `nosniff`. `robots.txt` may
  disallow `/wire/pkg/`. **Nothing may redirect `http://` to `https://`** for
  `/wire/*` — the machine cannot follow one; add the sentence to README.
- **README** gains a `The Wire` section (data file, tools, what the machine
  fetches, the plain-HTTP rule). `linkcheck.py` must pass over the new page.

Initial tiers, to be sanity-checked against each program's own SPEC/spotlight
prose and flagged as reviewable in the PR:

| tier | programs |
|---|---|
| 0 8088/8086 | HELLO, MINES, CALC, NOTEPAD, PAINT, BROWSER, TELNET, FTPD, FRACTAL, RECORDER, PIANO, ARTFUL, TEXPAD, SOLITAIR, ARKANOID, MISSILE, TAMEGRAM, CYCLONE, TANK, CHART, SHEET, FROTZ, WORD (+WORD.OVL, WELCOME.DOC), CWORD (+CWORD.OVL, WELCOME.RTF), WEAVE (+WEAVE.OVL, WEAVE.WSM, FORM/SHEET/PONG .WAB) |
| 1 286 | TRACKER, MODPLUG, AUDIO, LOOM (+LOOM.OVL, LOOM.WPV, the Weave trio) |
| 2 386 | RUNCPM (`floppy_only` — its `A/0/` tree) |
| 3 486+ | C64 (+C64.OVL, README.TXT, COPYING; parts → `WF_DISK`) |

`TASKMGR` is a system package and is not listed; WIREFRAME and the
`tests/` packages never are.

## 9. Tests on the OS side

| row | tier | what |
|---|---|---|
| `tests/unit/t_wire.py` | fast | pack → verify → dump round trip; `WC_*` equs mirrored; the writer's refusals |
| `tests/pkgrun.py` (+ `tests/pkgrun/`, `make pkgrun`) | soak | `OSAPI_PKG_RUN` runs HELLO from memory; refuses a bad magic and a parts image |
| `tests/thewire.py` (`make thewiretest`, `ethertest`'s shape: SYSTEM.CFG asking for `ETHER.DRV`, `SYSTEM/APPDATA/WIRE.CFG` naming `10.0.2.2:PORT/wire/`) | full if the 10-minute budget holds, else soak, and say which | QEMU + a host HTTP server serving a fixture catalog packed by `os88wire.py` from `build/hello.o88`, `build/mines.o88` and a tier-3 `WF_DISK` entry with one sidecar. Double-click the desktop zone; catalog loaded and 3 rows; filter `8088/8086` → 2 rows; select HELLO, Load Program → a `HELLO` instance; select the tier-3 entry → Load Program greyed, Add to Disk → Save → both files on B: byte-identical, read back on the host with the suite's independent FAT12 reader |
| the zone's look | functional check, not a row | screenshots on VGA, Hercules and CGA with the Wire icon selected and unselected (ETHER.DRV loaded), a plain boot showing no zone, and the Wire window on all three |

Every row registered in `tests/suite.py` or exempted with a reason
(`tests/unit/t_registry.py`).

## 10. Shipping and docs

- Makefile: `thewire.bin`/`thewire.o88` rules (Telnet's shape, `-I
  drivers/net/`); `SYSAPPS` gains it (system disks, four geometries, in
  `SYSTEM/`); `SMALLOMIT` gains it; `thewiretest` and `pkgrun` targets; the
  360KB cluster arithmetic re-stated where `Makefile:2374` states it.
- SPEC.md: §26.x (the zone), §21.x (the slot, and the loader split), §87
  (the package, the format, the HTTP profile, the numbers), §19.9 (WIRE.CFG),
  §24.3 (what the system disk carries). **Update the SPEC before the code**,
  then `python3 tools/os88index.py` and the doc gate.
- `apps/os88api.inc`: the slot's `%define` and a `WIRE_*`/`WC_*` block is NOT
  there — the format lives in `apps/thewire/wcat.inc` and the tool.
- CLAUDE.md's document table gains this file; README gains a short section
  and a line under the system disk's contents.
- The release skill (`.claude/skills/release-os8088/SKILL.md`) gains step 4c:
  `tools/release.py` now also fills `public/wire/pkg/` and the site build
  packs the catalog; check `public/wire/catalog.bin` changed when a package
  did, and run `python3 tools/os88wire.py --verify` on it from the OS repo.

## 11. Deferred, with the arithmetic

- **A catalog over 16 KB** (≥ 60 entries): a second page, `catalog2.bin`,
  named by a header field that is zero today. Not needed for years.
- **Folder-tree sidecars** (RunCPM's `A/0/`): a path field in the sidecar
  entry and `OSAPI_FILE_MKDIR`/`GOTO` in the chain. Two programs want it;
  `WF_FLOPPY` covers them.
- **Kinds 3..5** (documents, updates, shared files): the byte exists; the
  package draws them as programs and greys Load Program on a non-package.
- **A shared `apps/os88http.inc`**: the browser's client is entangled with
  its bss; the Wire's `wrhttp.inc` is written so it could move to `apps/`
  when a third client wants it.
- **Resuming a broken transfer**: no `Range:`; a failure is retried whole.
