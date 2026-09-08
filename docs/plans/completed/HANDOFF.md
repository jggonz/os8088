# Handoff — the Note Pad latency round, and the purgeable memory work

State as of `elendilon` @ `ae9fb06`. Everything below is merged, built and
booted; nothing is left uncommitted.

**Read first:** docs/plans/completed/NOTEPAD-NOTES.md §6 (the six ways the apparatus lied) and
§7 (the latency budget). Between them they carry six wrong diagnoses that each
survived a casual look, and most were failures of the *harness*, not the code.

---

## 1. The target

**Every user-triggered interaction except opening a document should finish
inside one typematic repeat, ~100 ms** (SPEC.md §2951). Below that, a held key
cannot outrun the editor and no backlog can build.

Measured on MartyPC's cycle-accurate 4.77MHz 8088, README.TXT (15,428
characters, 781 rows) in a 16-row × 29-column window — the smallest realistic
case, and a wider window is worse:

| interaction | at the start | now | budget |
|---|---|---|---|
| `Up` that scrolls | 5,150 ms | **380 ms** | 100 |
| `Down` that scrolls | 4,802–5,088 ms | **250–410 ms** | 100 |
| arrow inside the view | 104–252 ms | 104–252 ms | 100 |
| typing at the front | 414 ms | **257 ms** | 100 |
| raise of an obscured window | 1,026 ms | **0 `W_PAINT` calls** (wall figure owed) | 100 |
| `Up`/`Down` with nothing to do | 4,956 ms | **145 ms** | 100 |

**Nothing is at budget yet.** The remaining costs are no longer seeding
problems — SPEC.md §27.13 removed the last of those.

---

## 2. What shipped

| SPEC.md | what | verified by |
|---|---|---|
| §27.7.9 | a walk that cannot be seeded must still be BOUNDED | matched A/B, 12–19x on `Down` |
| §27.12 | the buffer move is `rep movsb`, not a byte loop | buffer read back from guest RAM, 0 differing bytes |
| §27.13 | the row index — a row outside the view without walking to it | content pixel-identical to a full repaint in 3 of 4 states; the 4th is NOTEPAD-NOTES §5.2.1 and predates the work |
| §50.6 | purgeable claims | builds, boots, no behaviour change with one consumer |
| §11.96 / §11.96.1 | the raise cache, opt-in via `WF_SAVEU` | zero `W_PAINT` on a covered raise; restore pixel-identical |
| §13.4 | `OSAPI_EVQ_PENDING` (earlier in the round) | — |

Two host-side instruments landed under `tools/notepad/` (§5 below).

---

## 3. Open, in the order worth doing

### 3.1 Verify `wm_hide` drops the raise cache — DO THIS FIRST

The only thing shipped in this round that is **not verified**. SPEC.md
§11.96.1: a hidden window's background painter finds itself invisible and
*skips drawing*, so it never reaches `wm_clip_set` and never drops the cache,
while its state goes on changing. `wm_hide` drops it for that reason.

The test was inconclusive: a scripted minimize-and-restore reported no
`W_PAINT`, which is either the hook working through some other path or the
click missing the minimize box, and the harness cannot tell those apart.

It is **safe as it stands** because Note Pad is the only window that opts in
and its promise holds across a hide too. **It must be verified before a second
application opts in** — a Timer or a Bounce is exactly the case that breaks.

Suggested method: breakpoint `wm_su_drop` and `np_paint`, click the minimize
box (find it with a screenshot, do not trust the coordinate), restore from the
dock, and require `wm_su_drop` to have fired and `np_paint` to have run.

### 3.2 Bound pass 1 for a caret move — ~155 ms → ~6 ms

`np_redraw`'s first pass lays out from the caret's row to the bottom of the
view to find which signatures moved: `(vrows − caret row) × ~6 ms`, so ~96 ms
with the caret near the top of a 16-row view. **For a caret move it is
provably unnecessary** — no character moved, so only the row the caret left
and the one it arrived on can differ, and `np_move` knows both (`[np_ckpr]` is
the new one).

The care needed: pass 1 also sets `[np_rowsn]`/`[np_rowsok]` from where it
stopped, so a shorter walk shrinks what `np_rows` describes and pushes traffic
onto §27.13's index. That is probably fine now the index exists — but it
changes an invariant two other sections rest on, so it wants its own A/B.

### 3.3 A one-row scroll draws ~186 ms

After §27.13's band bound, `np_scrollpaint` is one `OSAPI_GFX_SCROLL`, a strip
fill, a band fill, ~4 rows of layout, one row of text (~29 ms at
PERFORMANCE.md's ~1 ms a cell), two XOR bands and `np_sbar`. The lettering is
near its floor; **the blit and a full scroll-bar redraw for a one-pixel thumb
move are not.** Give them breakpoints of their own before attributing further
(NOTEPAD-NOTES §6.5).

### 3.4 The raise's wall figure

§11.96 is proven to make **zero `W_PAINT` calls**, so the 578 ms of glyphs is
definitionally gone — but there is no clean end-to-end number. Stepping frames
until the screen stops changing measures when Note Pad's **worker** settled,
not when the raise did. Bracket it kernel-side instead, and A/B it by patching
the `call wm_su_try` to three NOPs in the running guest — no rebuild needed.
(My first attempt used the listing's `.text`-relative offset and hit the wrong
instruction; confirm the byte reads `E8` before writing.)

### 3.5 Typing at the END of a note — the field report, reproduced

docs/plans/completed/NOTEPAD-NOTES.md §5.5 has the numbers. A keystroke at the end of the note
roughly **doubles as the page fills** (35 ms → 66 ms in a 16×29 window) and
then flattens — it is bounded by a screenful, not by the note — with the
keystroke that scrolls costing **190 ms**. One keystroke did two walks, six
`np_rflush` (three rows per pass, because `np_seedck` backs the seed up a row
for §27.11's word wrap) and **17 `gfx_fill`s**, which is 12.8 ms of arrival
before a glyph is drawn.

**The seed back-up is now skipped when it provably cannot matter** (SPEC.md
§27.11.1): 53.5 ms → 37–40 ms a keystroke, `np_rstart` 4.8 → 2.0. What is
left is the 17 fills, and the unbounded case §27.11.1 records — a run of
non-space longer than a row, where the seed goes back as many rows as the run
spans.

### 3.5.1 `NP_HCHUNK` = 4 is still an emulator number

It sizes a gfx-lock hold and a duty cycle, both of which the operator feels,
and only the 5150 can set it. `make npbench`, boot, double-click README.TXT,
Ctrl-B. `NP_HCHUNK ≈ (wanted hold in µs) ÷ (measured per-row µs)`, target a
hold under one 55 ms tick.

### 3.6 The 66 stale cells — RESOLVED, they were the harness

`pixcheck.py` decoded the framebuffer with `base64.b64decode`; the payload is
**hex**, and every hex character is legal base64, so it silently returned 4.5
bytes per pixel of nonsense. With the right decoder the check reads **0
differing bytes — incremental == full repaint**. docs/plans/completed/NOTEPAD-NOTES.md §5.2.1
and docs/plans/completed/NOTEPAD-NOTES.md §6.9. What remains is 227 pixels on row 0 at the toast's rectangle, which
may be a toast that is meant to persist; nothing else in the content differs.

### 3.7 `[np_rowsn]` is not capped to the array it indexes

Latent, documented at NOTEPAD-NOTES §5.3.1. `np_rows` is 60 words; the walk's natural-end
path stores `np_row+1`, so a 781-row note leaves it at 771. Nothing reaches it
today because every caller passes a visible row; `np_seedtail` clamps itself
because it is the first caller that takes its row *from* that word. Settle what
the word means across its four readers before adding a clamp.

### 3.8 Deferred by decision, not forgotten

- **A package-visible purgeable ABI** — handle-based, gfx-lock-only.
  docs/plans/completed/MEMORY-PLAN.md §3.1. Paint's undo image is the wanted consumer and the
  user specifically liked it: all the benefit, none of the loss.
- **Retrofitting the three existing kernel caches** (`MEM_K_ASC`,
  `MEM_K_FATW`, `MEM_K_SAVE`) as purgeable — each is a tag change plus one
  `mem_pg_own` row, and all three already have the "you cannot have it" path
  the contract requires.
- **The Task Manager `Purgeable` line** — a sum over `0xFExx` in the snapshot
  it already reads, so no ABI change. It is a *developer* instrument: from the
  user's side that memory is not taken.
- **Heap compaction** — not yet; §50.6.1's placement is what buys the time.

---

## 4. Things that will bite you

- **The clone is shallow in a fresh session and git will lie about ancestry.**
  CLAUDE.md's rule 0. `git fetch --unshallow` first.
- **`elendilon` moves under you.** This round merged it twice in ten minutes.
  The second merge brought `tools/os88sym.py` and 270 lines of
  `tools/os88marty.py` — worth reading before rebuilding anything by hand.
- **The upstream boundary hides collisions in files that AUTO-MERGE.** This
  round: upstream added seven API slots at `0x0340..0x0370` while this branch
  added one at `0x0340`. `kernel/kernel.asm` conflicted and was seen;
  `apps/os88api.inc` merged silently and ended up with two `%define`s for the
  same cell. Grep the new slot numbers after every merge.
- **A fix must be A/B'd against the symptom it claims.** §27.7.7 is correct,
  targets a real unbounded walk, and does nothing for the report it was
  written for. Two changes this round measured as *identical* to the build
  before them.

---

## 5. The instruments

**`tools/notepad/`** — host-side, drives a headless MartyPC through
`tools/os88marty.py`, adds no code to the guest, and every figure is a guest
cycle count. Read its README before using it; it carries six traps that each
produced a confident wrong answer.

```sh
make npbench                                     # the disk these drive
python3 tools/notepad/lab.py boot                # cold boot, open README.TXT
python3 tools/notepad/lab.py verify              # guest memory == the build?
python3 tools/notepad/lab.py press ArrowDown 20  # one line per keystroke
python3 tools/notepad/lab.py trace ArrowUp 3     # the breakdown
python3 tools/notepad/lab.py click 80 30         # price a raise
python3 tools/notepad/pixcheck.py                # pixels == a full repaint?
python3 tools/notepad/notecheck.py 0:x           # buffer == the file + edits?
```

**Re-cut the listing after every rebuild, and `verify`:**

```sh
nasm -f bin -w+error -DNPBENCH -I apps/ -I tests/ \
     -l build/npbench.lst -o /tmp/x.bin apps/notepad/notepad.asm
cmp /tmp/x.bin build/npbench.bin && python3 tools/notepad/lab.py verify
```

Skipping that is docs/plans/completed/NOTEPAD-NOTES.md §6.3, and it has been skipped twice.

**`tests/npbench.inc`** (`make npbench`, Ctrl-B) is the other instrument and
answers a different question: it times a routine from *inside*, in guest
ticks, and runs unattended on the 5150. `tools/notepad` times a real keystroke
from *outside* and can say which branch it took.
