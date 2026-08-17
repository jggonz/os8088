---
name: port-to-os8088
description: Port an existing program - written in C or in any other language - to os8088 as a C package (SPEC.md §70), the way apps/cword ported Microsoft Word 1.1a. Interactive intake of the reference source (scan nearby repos or clone a list), then a multi-agent scouting workflow that drafts the port plan, then one multi-agent implementation workflow per wave, asking the user only at real decisions, and a PR at the end. Runs on Opus 5 or Fable 5 only. Use when the user asks to port, bring over, reimplement or "do a CWORD-style port of" an application.
---

# Port a program to os8088, in C

This skill turns a reference codebase — the original program, in whatever
language — into a **native os8088 package written in C**, the way
`apps/cword/` is Microsoft Word 1.1a. "Port" here means what it meant there:
**the user interface is the original's, taken from its source and not from
memory; the behaviour is reimplemented in the strict C subset this toolchain
compiles; what cannot be carried is present and greyed with the fact that
greys it, never silently missing.** The code of the original almost never
compiles here and is not expected to. Its resources, tables, formats and
behaviour do.

Invoking this skill **authorises the multi-agent orchestration below** (the
`Workflow` tool — what the user calls "ultracode"). Every phase that reads a
lot or writes a lot fans out; every decision that is the user's comes back to
them through `AskUserQuestion`; everything else you decide and record.

Three files travel with this skill and are read by every agent it spawns:

| file | what |
|---|---|
| [`LESSONS.md`](LESSONS.md) | everything the CWORD port learned the hard way — the obstacles, in the order a port meets them, each with the fix. **Read it in full before step 1.** |
| [`workflows/scout.js`](workflows/scout.js) | the scouting workflow: reference source + this tree → a draft port plan and the user's questions |
| [`workflows/implement.js`](workflows/implement.js) | one implementation wave: implement → three review lenses → fix → independent verify on QEMU |

Also binding, and already in the tree: `docs/C-TOOLCHAIN.md` (what to type
and what each refusal means), `SPEC.md §70` (the contract), `apps/cc/os88.h`
(the API), `CLAUDE.md` (the hard rules and the performance table).

---

## 0. The model gate — do this first

This skill is supported on **Opus 5** and **Fable 5** only. Look at the
`# Environment` block of your own system prompt: it says which model is
running. If it is not `claude-opus-5` or `claude-fable-5`, say so in one line,
tell the user to switch with `/model` and re-run `/port-to-os8088`, and
**stop**. Do not run the workflows on another model: they were sized and
worded for these two, and a smaller model reading twenty thousand lines of
someone else's source and a 50,000-line SPEC does not scout, it guesses.

Never pass a `model` override to an agent inside the workflows — they inherit
the session model, which is the one the gate just checked.

## 1. Read before asking

1. `LESSONS.md` beside this file — all of it.
2. `docs/C-TOOLCHAIN.md` — the four rules, the ceiling, the overlay.
3. `apps/cc/os88.h`'s header comment, and skim the prototypes.
4. `SPEC.md §70.12` (the CWORD account) and `§70.14` (the overlay).
5. `apps/cword/cword.c`'s header comment — the redraw model and the cost table.

That is ~1,500 lines and it is the difference between a plan that fits and one
that discovers the 60KB ceiling in wave 4.

## 2. Intake — where is the reference source?

Ask, with `AskUserQuestion`, in one call — three questions:

1. **Where is the reference source?** (single select)
   - *Scan for repositories here* `(Recommended)` — you list every git
     checkout in the current directory and its parent (`ls -d ./*/ ../*/`,
     keep the ones with a `.git`), and the user picks from them.
   - *I will give you a list to clone* — the user supplies URLs (or paths)
     in "Other"; you `git clone --depth 1` each into the session scratchpad
     directory (never into this repo, never into `build/`).
2. **What is the program?** (single select)
   - *Infer it from the repository — its name, README and about text*
     `(Recommended)`
   - *I will type the name and version* — "Microsoft Word 1.1a",
     "Lotus 1-2-3 2.01"; the answer arrives as "Other".
3. **Anything about scope you already know?** (single select)
   - *No — port it whole and grey what cannot carry* `(Recommended)`
   - *Yes, I will describe it* — a subsystem to leave out, a file format to
     prefer, a feature that matters most; arrives as "Other".

If they chose *scan*, run the scan and ask a second `AskUserQuestion`
(multiSelect) listing what you found — path, top-level language guess from
file extensions, and one line from its README if it has one — and let them
pick one or more. If nothing plausible is found, fall through to *clone*.

Then confirm the set back in one line and record it: **write a memory** (a
`reference` type, like the existing `word-opus-source` memory) naming where the
reference source lives, because the repo will never say — SPEC.md names the
release, not a path — and the next session that touches the port will need
it.

Two things to say to the user here, once, plainly:

- The reference source stays outside this repo. **Nothing is vendored**
  (CONTRIBUTING.md §6): the port quotes strings, tables and formats and cites
  the file; it does not copy code. If the reference is under a licence that
  needs attribution, that attribution goes into every file header that
  carries derived material and into the About box — CWORD carries the
  Computer History Museum credit exactly so.
- The port will be **written in the C this toolchain accepts**, which is
  smaller than C: no `long`, `float`, `double`, no `&local`, no struct by
  value, no `printf`, no `malloc` as they know it, one translation unit,
  60KB. A program that needs 32-bit arithmetic on its hot path is a
  candidate for a *rewrite of its behaviour*, and the plan will say so.

## 3. Preflight — the machine and the branch

```
tools/setup-cc.sh && make cc-smoke        # the compiler, at the pinned commit; the smoke test builds
make covl                                 # the overlay gate builds (you will very likely need the overlay)
pkill -f qemu-system-i386 || true         # a stale QEMU answers on build/qmp.sock with an OLD image
git status --short                        # clean tree
git checkout -b port/<name> main          # branch; never work on main
```

If `setup-cc.sh` fails, that is a blocker for the *user* (network, Xcode
tools) — say what it printed and stop. Every later step needs the compiler.

## 4. Scout — the multi-agent survey

```
Workflow({ scriptPath: "<abs repo>/.claude/skills/port-to-os8088/workflows/scout.js",
           args: { repo: "<abs repo>", sources: ["<abs path>", ...],
                   app: "<program name and version>", name: "<proposed package name or ''>",
                   notes: "<the user's scope notes>" } })
```

It runs one scout per reference repo and three over this tree, one planner,
three adversarial reviewers and a reconciler, and returns `{ plan, sources,
tree, reviews }`. The plan is JSON in the shape `PLAN_SCHEMA` in `scout.js`:
name, authority table (surface → defining source file), scope
(ships / greyed-with-fact / absent), file split, byte budget with its basis,
API gaps, **waves**, verification, risks, and **questions**.

While it runs, do nothing that touches the tree. If it returns with a
source report missing (a null in `sources`), say which repo failed and re-run
scouting for that one alone before planning on three-quarters of the facts.

## 5. Decide — the user's questions, and yours

Take `plan.questions` to the user with `AskUserQuestion`, **at most four
questions per call**, each with the plan's options and its recommendation
first, labelled `(Recommended)`. Typical ones, and they are the only kind that
belong here:

- the package name, if the plan's is unsure or collides
- a scope cut that changes what ships (which of two file formats; whether a
  whole subsystem is greyed or built)
- whether to spend an API slot / add a thunk versus greying a feature
- which reference wins where two disagree

Everything else — the file split, the wave order, the buffer sizes, which
mode the program opens in — **you decide**, and you record the decision in the
plan. Do not ask a question whose answer is in `LESSONS.md` or the SPEC.

Then write the plan into the tree, in this order:

1. **`SPEC.md`** — a new top-level section at the end (the next free number),
   titled `<N>. <NAME> — <Product>, written in C`, in the shape of §70.12:
   what it is, where the UI comes from (the authority table), the two segments
   and the byte budget, what ships, what is greyed and the fact for each, the
   names (package, dir, images, vm) and the sentence that it shares nothing
   with any other package. SPEC is updated **before** the code, and this is
   the section every wave amends. `make` runs `tools/checkdocs.py`, so every
   `§` you cite must exist.
2. **`docs/<NAME>-PORT-PLAN.md`** — the plan itself, with the user's answers
   folded in and the questions section replaced by "Decisions". This is the
   file the implementation workflow reads.
3. Commit the two: `Plan the <NAME> port (SPEC.md §<N>)`.

## 6. Names, once

Before wave 1, check every name the plan uses collides with nothing:
`ls apps/ vm/ | grep -i <name>`, `grep -in <name> Makefile | head`. A package,
its directory, its disk images, its vm directory and its extension must not
answer to an existing program's name — `apps/word` and `apps/cword` are two
programs with one ambition and **share nothing**, by rule (SPEC.md §70.12).
Package names are ≤ 15 characters, upper case in `CC_PKG_NAME`.

## 7. Implement — one workflow per wave

For each wave `n` in the plan, in order:

```
Workflow({ scriptPath: "<abs repo>/.claude/skills/port-to-os8088/workflows/implement.js",
           args: { repo: "<abs repo>", plan: "<abs repo>/docs/<NAME>-PORT-PLAN.md",
                   wave: n, name: "<NAME>", dir: "apps/<dir>",
                   sources: ["<abs path>", ...],
                   decisions: "<every user answer so far, verbatim>", rounds: 2 } })
```

One implementer builds the wave through the gate, runs the host harness,
boots it and shoots it; three read-only reviewers (the four rules + budget,
the redraw budget, fidelity to the source) find what is wrong; a fixer applies
it; an independent verifier rebuilds, reboots and checks the wave's
`done_when` with its own eyes. It returns `{ status, report, questions, size,
shots, reviews }`.

Then, **you**:

- `status: done` — read `report` and `size`, **look at two of the `shots`
  yourself** (Read the PNGs), run `make` once (the doc gate) and commit the
  wave: subject `<NAME>: <what the wave added> (§<N>)`, body with the size
  line and how it was verified. Amend the SPEC section if the wave changed a
  fact in it. Go to the next wave.
- `status: blocked` — take `questions` to the user (`AskUserQuestion`, ≤ 4 a
  call, recommendation first), append the answers to `decisions`, and
  **re-run the same wave** with the new `decisions`.
- `status: failed` — read the workflow's `journal.jsonl` (the path is in the
  tool result), find which agent failed and why, and either re-run the wave
  or fix the specific thing by hand and re-run. Do not skip a wave; a later
  wave builds on it.

Watch the size line across waves. The moment resident image + bss passes
**55,000 of 61,440**, the next wave's first job is the overlay split, not a
feature — `LESSONS.md` "The overlay" says what moves and what may not.

Between waves the tree always builds and boots. If it does not, that is the
thing to fix before anything else.

## 8. Finish

When the last wave is done:

1. **The disk and the machine.** The plan's disk target builds all three
   geometries and `--verify`s each (copy the `cworddisk` rules). Ask the user
   whether the port gets its own 86Box machine (`vm/<machine>/86box.cfg`,
   copied from `vm/386-c-word/86box.cfg` with the B: image and the uuid
   changed and **nothing else** — see LESSONS.md on 86Box rewriting configs)
   and whether it goes onto `make allapps` (SPEC.md §19.9: a package with an
   overlay gets a **folder of its own** on that disk, never `APPS/`).
2. **The documents.** The SPEC section is final and its numbers are the
   shipping ones (`os88pkg` line, overlay size, frame max, the harness's cost
   table). `docs/<NAME>-PORT-PLAN.md` gets a closing "What shipped" section.
   `README.md` gets a paragraph beside the CWORD one under *A package can also
   be written in C*. `CLAUDE.md`'s command table gets the new targets if they
   are on demand like `cword`'s.
3. **The gates.** `make clean && make` (nothing in `all` may need the
   compiler — your targets are on demand); `make <name>disk`; the host
   harness; boot the 360KB image once (`make test TESTAPPS=build/<name>360.img`);
   `VIDEO=cga` once and look at a dialog and a greyed item on it.
4. **The PR.** Push the branch and open the PR against `main` with the body
   CONTRIBUTING.md §7 asks for: what changed and why, which SPEC sections
   moved, **how you verified it** with the exact commands and cropped
   screendumps, and whether the 360KB build was booted. Attach the cost
   table. Add `Co-Authored-By: Claude <model> <noreply@anthropic.com>`.
5. Tell the user, in a paragraph they can paste into a release note, what the
   port does, what it greys, and what it costs — numbers, not adjectives
   (the `release-os8088` skill's "Writing the copy" section is the standard).

## When to stop and ask, and when not to

Ask (`AskUserQuestion`, with a recommendation) when:

- a scope cut changes what ships, or a feature would need a new kernel
  facility rather than a thunk;
- two references disagree about the UI;
- a name collides;
- a wave comes back `blocked`;
- the plan wants to raise `KERN_BUDGET`, `KERN_CODE_MAX` or `APP_MAX_SIZE` —
  the answer is no unless the user says otherwise, and CLAUDE.md says why;
- the reference's licence needs something you cannot give.

Do not ask about: file layout, buffer sizes, wave order, which precedent to
copy, whether to build the harness (always), whether to use the overlay (when
the size line says so), whether to grey rather than fake (always grey a fact).
Those are in `LESSONS.md` and were settled once.
