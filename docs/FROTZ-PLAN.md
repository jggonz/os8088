# FROTZ-PLAN — a Z-machine for os8088

The design record for `apps/frotz/`, the fifteenth shipped package: an
interpreter for Infocom's Z-machine, windowed, with sound and pictures, and
its own story floppy in drive B:.

SPEC.md §59 is the binding contract; this file is the reasoning behind it and
the log of what the design had to give up. Read §59 first if you only want to
know what the code promises.

## 1. What this is, and what it is not

It is **not a port of Frotz.** David Griffith's Frotz is C, and this tree has
no C toolchain and does not want one — `nasm -f bin` flat binaries, no linker,
deliberately (CLAUDE.md). Porting it would have meant an ia16 cross-compiler,
a libc, and a 32-bit-arithmetic runtime, to arrive at a program that could not
use `OSAPI_WM_CREATE` without being rewritten around it anyway.

So this is an independent implementation of the **Z-Machine Standard 1.1**
(Graham Nelson) in 8086 assembly, written against the specification, and named
in the Frotz tradition the way `dfrotz` and `wfrotz` are. The About box and
`docs/` say exactly that, because a program called Frotz that is not Frotz is
a misattribution if it does not.

What it owes Frotz is the shape of the thing: Windows Frotz is a real windowed
application with menus, a status line, styles, sound and Blorb cover art, and
that is the target here — not a terminal in a box.

## 2. Versions

All of v1–v8. The split that matters is not the version number but three
mechanisms:

| | v1–v3 | v4–v5, v7–v8 | v6 |
|---|---|---|---|
| packed address × | 2 (v1-3), 4 (v4-7), 8 (v8) | | |
| status line | interpreter draws it | story writes an upper window | none; story owns everything |
| windows | lower + 1-line status | lower + upper (character grid) | 8 windows, **pixel** coordinates |
| pictures | no | no | `@draw_picture` &co. |

v7 differs from v5 only in the routine/string offsets, which is two words in
the header and one add in the unpacker, so it costs nothing to support.

**v6 is the expensive one and it is in scope by decision**, not by accident:
it doubles the window model and adds the picture subsystem. Nothing this disk
can legally ship exercises it — every v6 game (Zork Zero, Arthur, Journey,
Shogun) is Activision's and 300KB+ with separate picture files — so it is
verified against a v6 story compiled here by `inform -v6` and against
synthesised `.mg1` fixtures, and the disk carries a note saying so. A feature
verified only by its own tests is worth less than one a shipped game exercises,
and pretending otherwise would be the kind of claim §47 exists to forbid.

## 3. Memory: the one decision everything else follows from

**The whole story file is resident. There is no paging.**

The alternative was to page static and high memory off the floppy. Priced with
PERFORMANCE.md's own number — `int 13h` costs **~400ms** on the target machine,
near enough whatever it moves — a single turn of a v3 game touches high memory
hundreds of times to decode its text alone. That is minutes per turn. Paging
is not a slower design here, it is a broken one, and the honest move is the one
§47 names: **refuse, with the reason on screen**, rather than ship something
that technically runs.

So the sizing is arithmetic anyone can check:

```
heap after the 92KB kernel        640KB machine   ~549KB
                                  256KB machine   ~165KB
```

…except that is the heap, not what a story gets. `HEAP_SEG` is `0x1640`, i.e.
linear 91,136, so the heap is 551KB and 167KB respectively — and Frotz's own
region, about 50KB, is an ordinary claim out of the same heap. What is left
for the story is **~501KB** and **~117KB**, and the resident set is

```
  roundup1K(story) + dynamic memory (the save buffer)
                   + dynamic memory again (undo, OPTIONAL)
                   + 16KB scrollback
```

**Two things keep that affordable and both are worth naming.** There is no
resident pristine copy of the story: `@restart` and a save's compression
baseline both want the original bytes, and `OSAPI_FILE_READ_AT` reads them
back off the floppy on the UI task instead — a restart is rare, and a second
buffer the size of dynamic memory is not. And the undo snapshot is claimed
only if there is room: `@save_undo` is allowed to answer "cannot", and a story
that loses UNDO on a small machine is better than one that will not start.

Measured against that, on a 640KB machine:

| story | resident | fits |
|---|---|---|
| Bronze | 417KB (467 with undo) | yes |
| The Dreamhold | 434KB (474 with undo) | yes |
| Lost Pig | 337KB | yes |
| Curses / ZTUU / Photopia | 295 / 271 / 263KB | yes |
| **Anchorhead** | **565KB** | **no — on any real-mode machine** |

Anchorhead is why this table exists. It was on the disk until the arithmetic
was done, and 508KB of story plus a 41KB save buffer does not fit in 501KB.
No amount of 386 helps: extended memory is a data store no `CS:IP` can reach
(`OSAPI_XMEM_*` is explicit) and a story has to be directly addressable. So it
is not shipped — a file that could only ever produce a refusal is not a
feature, and the honest version of "we support Anchorhead" is this paragraph.

On a 256KB XT the four stories the 360KB disk carries all run — Mini-Zork
76KB, Zork 285 63KB, Adventure 92KB, Balances 100KB, against ~117KB — and
everything larger does not. Frotz says which and why **before it reads a
byte**: the size is in the directory entry, so `OSAPI_FILE_DLG`'s completion
hands over `DX:CX` and the refusal costs no disk I/O at all.

### 3.1 Addressing 512KB from 16-bit registers

A story address is up to 20 bits. A claim hands back a base **segment**, so:

```
byte at A   ->   segment = zf_sseg + (A >> 4),  offset = A & 15
```

`A >> 4` for a 20-bit A is a 16-bit result, so the conversion is a shift pair
and an add and never needs a 32-bit register (rule 1: 8086 only, no exception).

Two fast paths carry almost all the traffic:

- **`A < 65536` is one segment.** `[zf_sseg:A]` reaches it directly with no
  arithmetic at all. All of dynamic memory lives there by construction, and
  for v1–v3 (128KB ceiling) so does half of everything else.
- **The PC is a live `ES:SI`.** During execution ES:SI *is* the program
  counter — instruction fetch is `lods`, which is the cheapest thing the 8086
  has. It is renormalised (`ES += 0x800, SI -= 0x8000`) only when SI crosses
  0x8000, which is once per 32KB of straight-line code.

That choice is what makes the interpreter affordable, and it costs one rule:
**ES belongs to the story while the VM runs**, so every kernel call from inside
the VM saves and restores it, and every `[es:bx+W_*]` window read happens
outside the VM loop or after restoring ES to `KERNEL_SEG`.

Z-machine words are **big-endian**; the 8086 is not. Every word read is a byte
pair and an `xchg al, ah`. This is not negotiable and it is the single most
likely place for a subtle bug, so `zmem.inc` owns it and nothing else forms a
word by hand.

## 4. Execution: the VM is a worker task

A turn of a Z-machine game is tens of thousands of instructions. On the target
machine that is seconds. Running it inside a key callback would hold the gfx
lock for those seconds and freeze the clock, the mouse and every other window —
so the VM is a **background worker** (`OSAPI_TASK_SPAWN`, SPEC.md 20.6), the
same shape as Arkanoid's game loop and Fractal's renderer, hired from the first
`W_PAINT` because the entry proc runs before the instance is published.

That buys responsiveness and imposes the worker's rules, of which two shape the
whole program:

1. **A worker may not touch a file slot, `OSAPI_FILE_DLG`, or `OSAPI_MEM_*`.**
2. **The worker's stack is 256 bytes** (`SCH_STACK`; the SDK says 512 in one
   place and it is wrong), shared with the tick, mouse and sound IRQs.

Rule 2 means the VM keeps *its own* stack in a claim and the native call depth
stays shallow — no recursive descent anywhere, and Z-string decoding unrolls
its one level of abbreviation rather than recursing.

Rule 1 is the interesting one, and §4.1 is how it is answered.

### 4.1 The request handshake — how a worker saves a game

Every claim is made **before** the worker exists, on the UI task, at story-load
time: the story, the save staging buffer, the undo snapshot, the scrollback.
The worker never allocates.

Reading and writing files is the part that cannot be pre-arranged, because
`@save` and `@restore` are opcodes the *story* executes. The handshake:

1. The worker reaches `@save`. It stages the Quetzal image into the staging
   claim itself (a memory copy, which is allowed), sets `zf_req = REQ_SAVE`,
   prints `Press RETURN to choose a save file.` and waits on the ordinary
   input path.
2. The user presses RETURN. That is a **key callback, on the UI task**, which
   sees `zf_req` set and raises `OSAPI_FILE_DLG` in Save mode.
3. The dialog's completion proc — also the UI task — calls `OSAPI_FILE_WRITE`
   from the staging claim, writes the result to `zf_reqres`, and clears
   `zf_req`.
4. The worker wakes and takes the branch `@save` owes the story.

It costs one keypress, which is one fewer than the Infocom interpreters asked
for (they prompted for a filename), and every step happens on the task the
contract says owns it. `@restore` and `@restore_undo` are the same in reverse;
`@save_undo` needs no file at all and is pure worker work.

## 5. Windows

### 5.1 v1–v5, v7, v8

Two windows, exactly as the Standard describes, drawn into one os8088 window:

```
+--------------------------------------+
| MINI-ZORK I          Score: 0  Moves |  <- upper window / status line
+======================================+
| West of House                        |
| You are standing in an open field...  |  <- lower window, scrolls
|                                       |
| >open mailbox                      |#||  <- input line + scroll bar
+--------------------------------------+
```

The lower window word-wraps to the content width, pages with `[MORE]` when it
would scroll more than a screenful since the last input, and keeps a
**scrollback ring in a claim** so the scroll bar is real rather than decorative.
Scrolling uses `OSAPI_GFX_SCROLL` (which needs its x-range 8-pixel aligned and
may refuse, in which case the band is repainted) — never a full repaint, which
is PERFORMANCE.md Part 5's standing budget and the difference between a redraw
you can watch and one you cannot.

Text is the kernel's 8x8 font. There is no second font and no true bold, so the
Standard's styles map to what the adapters actually have:

| style | VGA | Hercules / CGA |
|---|---|---|
| roman | black on white | same |
| reverse video | swapped | swapped |
| bold | drawn twice, one pixel apart | reverse video |
| italic | underlined | underlined |
| fixed pitch | no change — the font already is | |

Colour (`@set_colour`, v5+) is honoured on VGA and ignored on the two 1bpp
adapters, where every colour rounds to black, white or a dither and a story
that colours by meaning would become unreadable.

### 5.2 v6

v6 replaces all of that with 8 windows addressed in **pixels**, each with its
own position, size, margins, font, colours and cursor, plus `@scroll_window`
and a picture layer under the text. It is a second window model, not a variant
of the first, and it is written as one: `zwin6.inc` alongside `zwin.inc`, with
the opcode layer selecting between them on `[zf_ver]`.

## 6. Pictures

Two sources, one drawing path:

- **`.mg1`/`.mg2`** — Infocom's own picture files, as shipped beside the v6
  games. **This plan said "RLE over a 4-bit palette; the decoder is small" and
  both halves of that were wrong.** It is an LZW variant with 9-to-12-bit
  codes and a 3,840-entry value/back-reference table, and the arithmetic does
  not fit the machine:

  - 11,520 bytes of LZW tables live for the whole decode;
  - the decoder emits one byte per pixel and cannot pack to 4bpp until a whole
    row exists, so a 320x200 Infocom picture wants a 64,000-byte canvas;
  - and the stream is read *at draw time*, which is worker time, where there
    is no file slot and no `OSAPI_MEM_*` — so the whole `.mg1`, 200–400KB,
    would have to be resident beside a 300KB v6 story on a 640KB machine.

  So `zpic.inc` parses the container and the directory — cheap, and it proves
  the file really is picture art and how much of it there is — then reports
  **no drawable pictures** and says why. That is the right answer rather than
  a cop-out: `@picture_data` is required to be able to answer "unavailable"
  (Standard 8.8.6.1) and stories handle it, whereas a story told a picture is
  200 pixels tall and then never shown it will lay a graphical interface out
  around blank space. Truthful and useless beats plausible and wrong.
- **`.PIX`** — a native archive built on the host by `tools/os88pix.py` from
  PNG, JPEG or a Blorb's picture chunks: a directory of numbered pictures in
  exactly the packed-4bpp layout `OSAPI_GFX_BLIT4` wants, so drawing one is a
  seek and a single blit rather than a decoder in the guest.

`.PIX` is why Bronze earns its place on the disk: its Blorb carries a JPEG
cover, `os88pix.py` turns it into `BRONZE.PIX`, and the picture path gets
exercised by a game that legally ships.

`@picture_data` answers truthfully when no picture file is present — the
Standard requires the "no pictures" answer and stories handle it — so a v6
story with its art missing degrades instead of failing.

## 7. Sound

`@sound_effect` maps onto whatever the machine has, checked with
`OSAPI_SND_CAPS` rather than assumed:

| | PC speaker | AdLib / SB |
|---|---|---|
| effect 1 (high beep) | `OSAPI_SND_TONE` | same |
| effect 2 (low boop) | `OSAPI_SND_TONE` | same |
| v5 sampled effects | nearest tone | FM approximation |

Sound is asked for from the worker. That is off the SDK's worker-safe list on
paper but Arkanoid already does it and documents why it is safe — the tone
self-expires via `snd_tick` and the grant is attributed to the asking task —
so this follows Arkanoid rather than inventing a second answer.

## 8. Saves

**Quetzal** (the IFF `IFZS` standard), because a save written on an XT in
1985's clothes then opens in Frotz on a laptop, and one written there opens
here. A raw dump would have been less work and buys nothing.

Dynamic memory goes in as **`UMem`, uncompressed**, and that is a memory
decision rather than a lazy one. `CMem` is a run-length encoding of the
current dynamic memory XORed against the *original* — so it needs both
resident at once, and §3 has just spent the second copy on not existing.
`UMem` is a legal Quetzal alternative that every interpreter reads, and it
costs disk rather than RAM: a Bronze save is 50KB, a Photopia save 13KB, a
Balances save 10KB, all of which the disks have room for. If the pristine copy
ever comes back for another reason, `CMem` is a small change on top.

The disk has a `SAVES` folder and the file dialog starts there.

## 9. The disk

`FROTZ.O88` plus stories, in folders, on its own floppy — not on the shipped
apps disk, which has ~100KB free on its 360KB geometry and would be swamped.

```
B:\  FROTZ.O88      the interpreter
     INFOCOM\       Mini-Zork I, both Samplers, Zork: The Undiscovered Underground
     CLASSIC\       Adventure (v3 and v5), Zork 285, Balances, Curses
     MODERN\        Photopia, 9:05, Bear, Lost Pig, Dreamhold, Bronze, Anchorhead
     ART\           .PIX picture archives
     SAVES\         empty; where the file dialog starts
     DOCS\          what each game is, who wrote it, and what it needs
```

**No story file is committed to this repository.** `tools/getstories.py` holds
a manifest of URL + SHA-256 + size and fetches into `build/stories/`, which is
ignored outright — the same decision `build/big.dat` made, for a stronger
reason: these are other people's games under other people's copyright. The
manifest is limited to what its authors released freely, which is why the
Infocom titles the brief asked for are represented by Mini-Zork I, the two
Samplers and Zork: The Undiscovered Underground rather than by Zork I–III,
Hitchhiker's, Planetfall and Enchanter — those are Activision's and still sold.
`STORIES=` puts your own copies on the disk beside them.

The library is 3,027KB and no floppy holds it, so each geometry ships a subset
chosen against its capacity and checked at build time:

| geometry | usable | ships |
|---|---|---|
| 360KB | 354KB | the v3 stories a 256KB XT can also run |
| 720KB | ~713KB | + Photopia, the Sampler, 9:05 |
| 1.44MB | ~1.42MB | + the rest that fits; the big v8 games ride disk 2 |

## 10. Machines

| target | machine | RAM | drives |
|---|---|---|---|
| `make xt-z` | IBM XT, 8088 @ 4.77MHz, SB 2.0 | 640KB | 360KB A:, 720KB B: |
| `make 386-z` | 386 + sound | 640KB conventional | two 1.44MB |

`xt-z` is the honest target: the machine this OS is for, with the full 640KB
because the story has to be resident, and a 3.5" DD drive for B: because 360KB
does not hold a library. `386-z` is the comfortable one — the same code, two
1.44MB drives, and the machine where Anchorhead and Bronze are worth trying.

Neither is a 256KB machine, and that is deliberate: `make xt` already is one,
and running Frotz there is how the refusal path gets tested.

## 10.1 The halt, and what it actually was

**A real story used to run its opening and then halt a command or two into
play**, with `unknown opcode ... es=<a segment below the story>`. This section
carried the diagnosis for two rounds and the diagnosis was wrong twice, so
what it says now is what it turned out to be.

It was not ES discipline and it was not a stray write into the bss. **The
program counter was correct.** `zx_jrel`'s backwards-branch fix-up subtracted
`0x1000` paragraphs from the segment and left SI where it had wrapped to, near
`0xFFFF`: a legal alias of the right byte, and a form `zx_step`'s guard rejects
and `zm_pcaddr` mis-reads. It bit only inside the story's first 64KB, and only
where a backwards branch reached further back than SI had walked — which is
every loop whose body contains a call, because `@ret` goes through `zm_seek`
and `zm_seek` leaves SI in 0..15. Adventure's `help` is one of them.

SPEC.md §59.12 has the whole account, including the part worth keeping: a
guard firing says an invariant was broken, not which one. Three rounds went
into hunting a register-discipline bug because the guard that caught it had
been written to catch register-discipline bugs.

## 11. Verification

The host has what it needs to check this properly, which is unusual for this
tree and worth using:

- **`inform`** (Inform 6.44, with PunyInform) compiles test stories at any
  version, so `tests/frotz/` gets a v3, a v5, a v8 and a v6 story that exercise
  opcodes rather than prose.
- **`dfrotz`** is a reference interpreter. Feeding it and Frotz the same input
  gives two transcripts to diff, which turns "does the object tree work" from
  an opinion into a test.
- The guest side **is built and is `make zh`** (SPEC.md §59.13): `apps/frotz`
  assembled with `-DZHARNESS`, which gives the interpreter a teletype on COM4 —
  story text out a byte at a time, keys back in, and four markers saying where
  it is. `tools/zharness.py` plays a story to a script over it and diffs the
  transcript against `dfrotz`; `make zcheck` does that for every story there
  is.

  It streams rather than writing the transcript to B: as this section first
  planned, and the reason is the failing case: a transcript on a floppy is only
  readable after the machine stops, so a story that hangs or halts leaves
  nothing to read — which is exactly the run worth reading. §59.12 was found on
  the third command of a scripted Adventure, with the whole transcript that led
  to it.

Three defects PERFORMANCE.md names as invisible in an emulator apply directly
here — a visible redraw, a double-draw flash, and input overrun — so the text
path is counted (`font_run` calls per line, `OSAPI_GFX_SCROLL` vs repaint)
rather than eyeballed, and checked on a 1bpp adapter before it is called done.
