# os8088 kernel split — `kern_small` and `kern_big`

**Research document with a working mechanism.** SPEC.md is the binding
contract for what the kernel *is*; this is the study of splitting it into two
builds off one tree, and the record of the plumbing that begins it. The
mechanism is in the tree and measured. **No feature has moved into either
build yet** — that is the point of starting here.

The ask, in the requester's words:

- Begin the split to **`KERN_SMALL`** and **`KERN_BIG`**.
- `KERN_SMALL` is a copy of the kernel **as it stands today, with nothing (yet)
  removed**.
- **The default is `KERN_BIG`** (added after the first round).
- `KERN_BIG` gets the dual-display support (docs/plans/completed/DUAL-DISPLAY-PLAN.md).
- It must **separate cleanly at build time**.
- It must **not cost `KERN_SMALL` a bunch of room**.

---

## 0. The verdict, up front

**It separates cleanly, and at the split commit it costs `kern_small` exactly
zero bytes — measured, not argued.**

```
kernsplit: small  80998 bytes (159 sectors)
kernsplit: big    80998 bytes (159 sectors)
kernsplit: the two builds are BYTE-IDENTICAL - no divergence yet
```

and against the kernel as it stood before any of this landed:

```
SMALL after the split: 80998 bytes vs 80998 before -> BYTE-IDENTICAL
md5  49e0987b1f9992280bbb98905755b3f0  (both)
```

Three findings:

1. **The build-time mechanism already existed and needed one knob.** `make
   field` has been building whole second kernels into directories of their own
   for as long as it has existed (`build/cgak`, `build/herck`, …) by recursive
   make with `BUILD=`. `make small` is that pattern with `KERN_SMALL=1` instead
   of `VIDEO=cga`. Nothing new was invented.

2. **The default is `KERN_BIG`**, and today big *is* the kernel that already
   existed, so `all`, the six shipped images, the field disks, every test and
   every `.o88` are untouched — the default build is byte-identical to the
   pre-split kernel. `make small` is the one you ask for. That is the right
   way round for the reason the budget guards are: big is what nearly every
   machine runs, and small is a deliberate product for the 128KB floor rather
   than a fallback nobody chose.

3. **The one real design decision is the ABI, and the answer is parity.** The
   two builds must publish the **same API table at the same offsets**, with
   `kern_big`-only slots present in `kern_small` as refusing stubs. It costs
   the small build 8 bytes per divergent slot and it is what lets **one
   `.o88` serve both kernels** — which is the property everything else here
   depends on, including `make small` not needing to rebuild the apps disks.

**What this is not.** Nothing has been removed from `kern_small` and nothing
has been added to `kern_big`. The split is a *seam*, not a divergence, and the
first thing through it is dual display.

> **Superseded, and left standing as the record of the split commit.** The
> paragraph above described the tree at step 0. Since then dual display has
> gone through the seam into `kern_big`, and the **first removal from
> `kern_small` has landed** — the extended-memory store above 1MB (SPEC.md
> §41.11), which took `kern_small` to **81,017 bytes / 159 sectors** against
> `kern_big`'s 86,011 / 168, with `kern_big` **byte-identical** to the build
> before it. §6's open decision 4 carries what that first removal established
> for the ones after it. The two builds are no longer the same bytes, so §0's
> "BYTE-IDENTICAL" figures are history rather than a current gate; the current
> gate is the one in §7's last paragraph, and it is unchanged.

---

## 1. What was built

| piece | what it is |
|---|---|
| `KERN_SMALL=1` | a Makefile knob → `-DKERN_SMALL`; **the default sends `-DKERN_BIG`**, so exactly one always arrives |
| the stamp key | `KERN_SMALL` is in `VIDSTAMP`, so changing it **deletes `kernel.bin`** and rebuilds. Without that, `make` sees an up-to-date kernel and you boot the other variant while reading the wrong source |
| `make small` | builds `kern_small` and its 360KB and 1.44MB system disks **into `build/smallk/`** |
| `make kernsplit` | builds both and reports, without producing floppies |
| `tools/kernsplit.py` | the reporter: sizes, the 512-byte rung, and byte-identity |
| `KERN_BUDGET` / `KERN_SMALL_BUDGET` | two named footprint guards, holding the same value today |
| `kernsize.py` variants | two baselines, one per product, and `--bless` allowed on either (§5) |

**`build/smallk/` and not `build/`, and that is the `cgak` lesson rather than
tidiness.** The Makefile already carries the note: *a `VIDEO=`-forced kernel
that reaches `build/` is a machine that boots the wrong card for everyone, and
that is a mistake that has been made.* A `kern_small` in `build/kernel.bin` is
the same mistake with a different symptom — the shipped image silently becomes
the other product, and nothing says so. The two builds keep separate stamp
files (`build/.video-auto` and `build/smallk/.video-auto-small1`), so they
cannot invalidate each other.

**Both defines are POSITIVE**, so no big-only site has to read `%ifndef
KERN_SMALL` — a double negative on the one conditional whose entire job is
*which build is this*, which is exactly where a reader gets it backwards.

**`kernel.asm` errors on BOTH and defaults to `KERN_BIG` on NEITHER, and the
asymmetry is the point.** Both is genuinely ambiguous. Neither is not: it means
"whoever assembled this did not care", and the only answer they can have wanted
is the shipped kernel.

It was an error for about an hour, and what that broke is worth recording:
`tools/os88sym.py` assembles a **temporary copy** of `kernel.asm` to read the
symbol map out of it, and has no business knowing which product is being
measured. It stopped working, and the failure surfaced in a *mouse script*
three layers from the cause. Verified after the fix: a no-define assembly is
byte-identical to a `-DKERN_BIG` one, and `-DKERN_BIG -DKERN_SMALL` still stops
the assembler.

**The guards are separate constants that hold the same value, deliberately.**
Separating them is the mechanism; *moving* one is a decision to take with
whoever asked for the feature that needs it, which is the rule every one of
the fourteen budget moves has followed. So this commit grants nothing.
`kernel.asm`'s own comment has been anticipating exactly this for three moves
— *"the day that second build exists, this figure is kern_big's and stops
being the one that has to be defended"* — and `KERN_SMALL_BUDGET` is now the
one that has to be.

### 1.1 The reporter is a measurement, not a tautology

`kernsplit.py` saying "BYTE-IDENTICAL" is worth nothing unless it can say
otherwise, so that was checked: an 8-byte `db 'BIGPROBE'` behind `%ifdef
KERN_BIG` was inserted, both builds run, and the reporter caught it —

```
kernsplit: same size, DIFFERENT BYTES - a divergence small enough to hide in
           the rung's padding. It costs the machine nothing yet and it is real.
```

— and then removed. **That case is the one to notice**: the probe moved
neither the byte count nor the sector count, because the image is padded to a
512-byte rung and eight bytes vanish into the padding. A size comparison alone
would have reported the two builds as the same. So the check reads the bytes.

**Both variants boot**: `build/small360.img` and the default `build/os8088-360.img`
each reach a settled desktop on a cycle-accurate 5150 in ~5 s at 60.0% lit — as
they must, the two being the same bytes today.

---

## 2. Where divergence is allowed to live

The whole design rests on one claim: **adding a feature to `kern_big` costs
`kern_small` nothing.** It is not self-enforcing. Four shapes break it, and
each is silent:

| leak | what it looks like | what stops it |
|---|---|---|
| a hook left outside its `%ifdef` | one `cmp byte [vid_ndisp], 1` / `jbe` on a path small never uses | `kernsplit` + `kernsize`'s baseline delta |
| a struct sized for the maximum | `vid_ctx: times 2 …` in both builds | review; the array's size is itself `%ifdef`'d |
| a routine made parameterised "while we are here" | absolute `[vid_stride]` becomes indirect, and **both** builds pay the indirection | this is the dangerous one — see below |
| a shared refactor whose only consumer is big | a helper factored out for big, linked into small | `kernsize --modules` attributes it |

**The third is the one that would actually have happened.** The dual-display
design (docs/plans/completed/DUAL-DISPLAY-PLAN.md §5) turns on the renderer's geometry words
being *absolute* operands — `[cs:vid_rowadd]`, `[cs:vid_wrapbit]` — read from
inner loops with no spare register. The obvious way to serve two displays is
to index them through a base register, and that would cost **every primitive
in `kern_small` too**, for a feature it does not have. It is also the reason
the plan already rejects it: `gfx_nextrow`'s contract is *DI and flags and
nothing else*. So `kern_big` **copies a context into the same absolute words**
rather than indirecting, and `kern_small`'s renderer is untouched by
construction.

That generalises into the rule this split runs on:

> **`kern_big` adds layers ABOVE `kern_small`'s code; it does not parameterise
> it.** A hook at a public entry costs the other build one `%ifdef`. A
> parameter threaded through a body costs it every call.

### 2.1 Where a `%ifdef KERN_BIG` may go

- **Around a whole `%include`** — the cleanest, for a module that is entirely
  big's. Note the section rule (SPEC.md §4): a module must switch back to
  `section .text` before it ends, and a `%ifdef` that spans a `section`
  directive without a matching one is a build failure under `-w+error` rather
  than a silent misplacement, which is the right way round.
- **Around a macro invocation at a public entry** — `GFXDISP` above `GFXCLIP`
  at the four rect entries. Zero bytes when undefined.
- **Around a `.bss` reservation** — free on disk either way (`-f bin`), but it
  moves `KERN_CODE_MAX` and the footprint, so it is not free of the guards.
- **Around a constant** — `KERN_BUDGET` is the worked example, and costs
  nothing at all.

**Where it must not go:** inside an inner loop, and around anything the
`%error` assertions measure without being told about it — the API table's
length being the one that matters (§3).

---

## 3. The ABI: one table, two implementations

This is the crux, and getting it wrong is the one mistake that would be
expensive to undo.

`kernel.asm` asserts the table's start **and its span**:

```
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%if OSAPI_TABLE_LEN != 111 * 8
%error "os8088 API jump table must be exactly 111 8-byte slots"
```

If `kern_big` appends slots, that assertion has to become variant-aware and
the two builds publish **different tables**. A package built against big and
run on small then far-calls into whatever follows the table — which on small
is the debug registry, not code. It assembles, it loads, it launches, and it
dies somewhere unrelated.

**So the tables stay the same length in both builds.** A `kern_big`-only slot
exists in `kern_small` as a cell pointing at a refusing stub, exactly as
retired slot `0x01E8` already does (SPEC.md §20.8 rule 4 — it answers CF=1 /
`FERR_NAME` and the SDK publishes no name for it). Cost to the small build:
**8 bytes per divergent slot, plus one shared refuser of about six.** For the
three or four cells dual display would want, call it 30 bytes.

Three things follow, and they are what make the rest of this cheap:

- **One `.o88` serves both kernels.** `make small` deliberately does *not*
  rebuild the apps disks — the ordinary `build/apps360.img` is small's apps
  disk too. If that ever stops being true, the split has grown an ABI, which is the
  single thing this design exists to avoid.
- **A package can ask.** A refusing slot is a runtime answer, so an app that
  wants a big-only capability tests it the way it already tests for a sound
  card: call and read CF. No new mechanism, and no build-time variant in the
  SDK.
- **`apps/os88api.inc` does not fork.** It publishes the numbers; whether a
  given kernel implements one is a runtime question.

**The debug registry needs nothing** (SPEC.md §57): it is found by tag and a
reader that cannot find its block says so and continues — which is precisely
the case "this instrument is running on the other variant".

---

## 4. What the images look like

`all` is **unchanged**: six images, small kernel, exactly as today.

`make small` adds `build/small360.img` and `build/small.img`. Deliberately no
720KB twin yet — the 720 and 360 rules differ only in `--size` and it is two lines
whenever somebody wants it, and adding artifacts nobody has asked to boot is
how a build target rots.

**The apps disks are shared and must stay shared** (§3).

**`SYSTEM.CFG` crosses between them and that is fine today** (SPEC.md §51.5):
a missing or malformed key means the defaults, never an error, and the keys
the two builds share mean the same thing. It stops being fine the moment
`kern_big` adds a key `kern_small` would round-trip *and act on* — the hard
disk's opaque blob is the precedent for how to do that safely (SPEC.md §51.9).

---

## 5. Tooling: two products, two baselines

`kernsize.py` compares against a baseline in docs/KERNEL-MEMORY.md, and it
already refused to `--bless` a knob build because *"the baseline is the SHIPPED
kernel"*. That refusal was right while there was one shipped kernel and is
wrong now, because **both variants are shipped products**. So the script
learned the distinction the split introduces:

- **A knob** — `VIDEO=cga`, `DISKCNT=1`, `REDRAWFULL=1` — produces a kernel
  nobody ships. Blessing one would write a baseline describing a binary that is
  on no disk. Still refused, and the refusal now *names the knob* responsible.
- **A variant** — `KERN_BIG` / `KERN_SMALL` — produces a kernel that ships.
  Each has a baseline of its own, and each is blessable.

The baseline block is a map keyed by variant. Three things about it:

- **`--bless` MERGES.** Blessing big must not delete small's figures; the two
  are blessed by two different commands minutes apart. Verified: bless big,
  bless small, and the block holds both.
- **The pre-split flat block is read as `big`**, which is what it described —
  big being the default and the split having removed nothing — so a tree that
  has not been blessed since keeps reporting instead of going quiet.
- **A variant with no baseline reports absolute figures and says so**, rather
  than inventing deltas against the other product's numbers.

**Every line names its variant** (`kernsize[big]:` / `kernsize[small]:`). That
is not decoration: the two have separate baselines *and separate budgets*, and
an unlabelled run of figures is a run somebody will compare against the other
build's. That is the specific bug this prevents — with one flat baseline,
`make KERN_SMALL=1` reported small's sections against big's figures and every
line was a delta of the difference between the two products: noise that reads
exactly like a regression, in the build that is supposed to be defended byte by
byte.

`os88ovlchk.py` runs on every kernel build including the sub-build, so the
overlay/cold boundary is checked on both. `checkdocs`, `checkreadme` and the
`%error` guards inside `kernel.asm` are all per-build and need nothing.

**Both are blessed now, and the first thing the pair reported is the thing one
baseline could not.** The dual-display round merged with a round of
sound/memory work that landed **+145 bytes of `.text` in BOTH** — shared code,
so neither variant escapes it — and the two answered differently: big had 298
bytes of image-rung slack and absorbed it, while **small crossed a rung**
(112 → 113 steps of 512) and now stands at **512 spare, one step**, against
big's 2,560. That is the split working as designed rather than a problem with
it: *small is the tighter product and shared growth is what squeezes it*, and
`kernsplit` at the same moment went from "big costs +512 over small" to **"same
size, DIFFERENT BYTES"** — both at 162 sectors, big's `%ifdef KERN_BIG` code
now fitting inside the padding small's own growth opened up. Neither figure
would have been visible with one flat baseline.

**Neither baseline was blessed in the round that built this**, deliberately:
big's was stale by
the toast round's +885 bytes (it predates that merge, which should have blessed
and did not), and `--bless` regenerates the module and theme tables as well — a
large mechanical diff that would swamp the review of the split itself. `make`
reports the staleness on every build, so it is visible rather than lost.
Blessing both is one command each whenever the owner wants the document
current.

## 6. Open decisions, for whoever owns them

1. **Which variant eventually ships by default.** Today small *is* today's
   kernel, so the default costs nothing and nothing needs deciding. Once
   things are removed from small, the shipped 640KB image probably wants to be
   big — at which point `all` builds both and §5's baseline gap becomes real.
2. **Whether `KERN_BUDGET` moves for big.** Not moved here. The mechanism
   makes it *possible* to move without touching the small machine, which is
   what the fourteenth move's comment said it was waiting for.
3. **A 720KB big image** (§4).
4. **What comes out of small, and when.** ~~Nothing yet, on purpose.~~
   **The first removal has landed: the extended-memory store (SPEC.md
   §41.11)**, asked for by the owner. docs/KERNEL-MEMORY.md also nominates
   SPEC.md §9.6's keyboard mouse — 520 bytes and two steps — recorded there at
   the owner's request precisely so this decision could be found rather than
   rediscovered. **It is a recommendation, not a plan**, and taking it is the
   owner's call.

   What the first removal established, for the ones after it:

   - **The seam is drawn by what code is FOR, not by which file it is in.**
     §41 is two modules and the split runs *through* one of them: the CPU
     tier stays in both builds (slot 0x0188 is a published ABI four packages
     read), and the A20 and HMA routines go, because they exist only to reach
     the store. The test that made that decision cheap was a grep —
     `[cpu_feat]` and the `CPU_F_*` bits have no readers outside the two
     modules, so nothing above them can observe the difference.
   - **ABI parity is not "refuse"; it is "answer what the floor machine
     answers".** §3's refusing stub was the right mechanism and the wrong
     default value. The target machine is an 8088, so tier 0's answers are
     already on every shipped package's tested path — which turns the stubs
     from new behaviour into behaviour that has been in the field for as long
     as the feature has. Verified by calling all five slots on both kernels
     on the same 8088: identical, register for register.
   - **A removal is worth measuring on the BOOT PATH and not only in bytes.**
     Two probes came off `kern_small`'s boot — `int 15h` AH=88h and the A20
     gate — and that is the only part of the feature that ever cost the
     machine time rather than image.
   - **The gate held exactly as designed**: `kern_big` came out
     **byte-identical**, md5 and `cmp` both, so the removal is provably free
     to the shipped product.

5. **What comes out of the APPS, and how.** ~~Nothing; the split is a kernel
   question.~~ **Answered, and it turned out not to be a kernel question at
   all**: SPEC.md §27.16's `APP_SMALL`, with Note Pad as the first consumer.

   The shape is the thing to carry forward. A package is **built twice off
   one source** and the small arm goes on a floppy of its own
   (`make smallapps` → `build/smallapps*.img`), so **nothing about the ABI
   moves**: a small-built `NOTEPAD.O88` calls the same table at the same
   offsets and runs on `kern_big` unchanged. What pairs it with `kern_small`
   is *which disk it is written to*, which is why §4's "one package, both
   kernels" survives intact — this is one package built twice, not two
   packages, and the day it becomes two is the day this acquired the ABI the
   whole design avoids.

   Three things it established for whoever gates the next package:

   - **§2's leak table applies unchanged, one level down.** The dangerous
     shape is still the third row — a routine made parameterised "while we
     are here" — and the gate is still byte-identity of the arm that ships.
     `tests/unit/t_appsmall.py` is that gate, and it holds: the full Note Pad
     is byte-for-byte what it was.
   - **The seam is drawn by what code is FOR, again.** The find panel, its
     regex engine and the About card came out whole because nothing outside
     them calls in; undo did *not*, because `np_uclose` is called by every
     path that is not typing — so undo's nine entry points became **one shared
     `ret`** rather than ~15 `%ifdef`s through the editing core. Gating a
     feature that threads through the core is a stub, not a bracket.
   - **The largest saving was not in the image.** Undo's arena is a heap claim
     that grows to 16KB (SPEC.md §27.9) and is simply never taken, against
     ~21.5KB of heap on the floor machine — larger than the 6,859 bytes of
     image and bss the report prints. `tools/os88pkgsize.py` says so on every
     build, and says explicitly that its figure is a floor.

---

## 7. Staging

| # | step | gate |
|---|---|---|
| 0 | ~~knob, `make small`, `make kernsplit`, the reporter, two `kernsize` baselines~~ **DONE** | the default build byte-identical to the pre-split kernel; both boot |
| 1 | ABI parity harness: the refusing stub and one `kern_big`-only slot behind it | one `.o88` runs on both; the slot answers CF=1 on small |
| 2 | Dual display's kernel-side work behind `%ifdef KERN_BIG` | `kernsplit` reports small unchanged at every commit |
| 3 | Second `kernsize` baseline, if and when both ship | — |
| 4 | Removals from `kern_small`, one decision at a time — **the extended-memory store is the first (SPEC.md §41.11)**; the rest still one decision at a time | big byte-identical; small −1,410 bytes and 2 rungs; the four slots answer tier 0's answers on both kernels |

**The gate that matters is step 2's, and it is one line**: `make kernsplit`
after every commit that touches the kernel. `kern_small`'s size moving in a
commit whose subject is about `kern_big` is the whole failure mode of this
design, and it is cheap to catch and invisible otherwise.
