# ASSOC-SLOTS-PLAN — the resident association slots as a cache over `ASSOC.DAT`

**OPEN. Nothing in this file is built.** It is the third of three answers to
one report — *"I'm losing assoc icons on a hard drive, with a standard
two-disk 1.44MB install"* — and the only one that is a design rather than a
constant. The other two shipped on the `icons` branch:

- **The icon store grows** (SPEC.md 25.9.6): 5KB first claim, a kilobyte at a
  time up to 20KB / 247 rows, through `mem_regrow`. That was the reported
  defect.
- **`ASSOC.DAT`'s reader claims the file's own size** (SPEC.md 54.7.7), and
  the largest file is 8KB / 90 app rows where it was 5KB / 55.

What is left is the table those two feed, and it is the one that costs
RESIDENT bytes.

## 1. The table, and where it stands

`kernel/assoc.inc`: `ASSOC_NAPP` = 16 program slots at 20 resident bytes
each (stem 8, glyph 8, cluster 2, volume 1, and the folder byte), and
`ASSOC_NEXT` = 32 extension slots at 4 bytes each. Five programs and eight
extensions are built in (`ASSOC_NDEF`). **Nothing frees a slot in a session**:
a program that declares, or that registers at run time with
`OSAPI_ASSOC_SET`, holds its slot until the machine restarts, and a program
that finds the table full is dropped **silently** — its documents draw the
plain page and open nothing.

Counted off the build on 2026-10-30 (`build/os8088.img` + `build/apps.img`
walked, and `build/livepayload.txt` for the everything set):

| install | packages | programs that declare | new extensions | program slots | extension slots |
|---|---|---|---|---|---|
| the two 1.44MB disks | 50 | 11 | 20 | **16 / 16** | 28 / 32 |
| everything not retired (the live payload, plus Excitebike) | ~64 | 17 | 31 | **22 / 16** | **39 / 32** |

The declaring programs past the built-ins on the standard install are
RealDMG, Sheet, Browser, Pixel, FontView, Audio, TexPad, MIDIRack, XTMP3,
CDRack and Video; the everything set adds Word, CWORD (`.RTF`, a RUN-TIME
registration), Frotz, Weave, Loom and Apple II. **The standard install fills
the table exactly**, so the first twelfth declaring program a user installs -
any of those six - is the one that silently loses.

## 2. The two answers, priced

### 2.1 Raise the constants — the stopgap

`ASSOC_NAPP` 16 → 24 and `ASSOC_NEXT` 32 → 48: 8 x 20 + 16 x 4 = **224
resident bytes**, for ever, on every `kern_big` machine. It buys today's
everything set with two programs and nine extensions to spare, which is one
or two release cycles at this tree's rate. 32 / 64 is ~544 bytes and was
refused once already (`assoc.inc`'s own note, SPEC.md 54.2). Every raise is
the same conversation again, and each one is billed to whoever is standing
at the next rung.

What a raise must check besides the bytes: `ASSOC_IX` is `0x7F` (an
extension slot's program index is seven bits, so 127 programs is the hard
ceiling), `ASSOC_FIND` is `0xEF` and `ASSOC_FLUSH` `0xF0`+ sit above the slot
numbers in `[assoc_dapp]`, and §54.7.6.2's capture is sized `ICO_CAP_MAX` =
11 records - "every declaring program in the tree" when it was written and
17 today. A capture that overflows is not wrong, only slower (the flush reads
those headers again), but the comment that says "every" is now false.

### 2.2 Make the slots a CACHE — the lasting fix

The durable truth is already on the disk: every volume's `ASSOC.DAT` holds
each program's stem, folder, glyph and declarations (SPEC.md 54.7), and the
miss path already exists - an extension nothing claims is posted as
`ASSOC_FIND` and `assoc_run` goes and asks every volume (SPEC.md 54.4.3). So
the resident table does not need to hold everything the machine has ever
met; it needs to hold what is in use, and **evict** to make room.

**Eviction rule (proposed):** when a declaration finds no free program slot,
take the least-recently-used slot that is (a) not a built-in (`< ASSOC_NDEF`
is never evicted), (b) not running (no instance whose stem matches), and (c)
not STICKY (below). Its extension slots are freed with it. A document of the
evicted program then misses once, posts `ASSOC_FIND`, and the existing
search brings the program back - at the cost of one directory walk per
volume, which is what a first open already costs.

**The one hard part is the run-time registration.** `OSAPI_ASSOC_SET`
(SPEC.md 54.5) records a claim in RAM ONLY - CWORD's `.RTF` is the shipped
case - and marks the extension `ASSOC_STICKY`. Evicting it loses the claim
for the session, and no `ASSOC.DAT` row says it ever existed. Two ways out,
and the plan should pick one before any code:

1. **Never evict a slot that holds a sticky extension.** Cheapest; a machine
   with more run-time registrations than slots is the only one that still
   drops, and none ships.
2. **Write run-time claims through to `ASSOC.DAT`** at the next flush, so
   they survive both eviction and a restart. More behaviour, more bytes, and
   it changes what `OSAPI_ASSOC_SET` promises.

**Cost (ESTIMATE, not measured):** an LRU stamp per slot (one byte, 16
bytes) plus the victim search and the extension sweep - roughly **80-150
bytes of `.cold`**, resident. It is bounded: the table size stops being a
function of how many programs exist, so 16 can stay 16, or fall.

**What it buys that the stopgap does not:** the bill is paid once. The
alternative is 224 bytes now, and the same again the next time this table
fills.

## 3. Proposed order

1. Decide §2.2's sticky question (option 1 is the default).
2. Build eviction behind the existing table size, with a gate row that
   mounts more declaring programs than there are slots and asserts each one's
   documents still open (the evicted one through `ASSOC_FIND`).
3. Correct `ICO_CAP_MAX`'s "every declaring program" note, or raise it to 17
   - it is heap, at the head of the icon store's claim, and costs the first
   claim no row until it passes 22 records.
4. Only if 2 is refused: take §2.1's 224 bytes and say so in
   `docs/KERNEL-MEMORY.md`'s terms.

## 4. What else grows with the tree, for whoever comes next

| limit | today | binds at | answer |
|---|---|---|---|
| icon store rows | 60 first claim, grows to 247 | ~247 distinct icons machine-wide | raise `ICO_KBMAX` (row byte caps it at 253) |
| `ASSOC.DAT` app rows | 90 per volume | ~90 packages on ONE volume | raise `ASC_KB`; the reader follows the file, the flush's work claim (2 x `ASC_KB`) is the one that grows |
| `ASSOC.DAT` extension rows | 64 per volume | 64 extensions | the same file |
| program / extension slots | 16 / 32 resident | **now** | this plan |
| capture records | 11 | the 12th declaring package in ONE flush action | heap, cheap - §3 item 3 |
