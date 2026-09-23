# SCRIBE's overlay stops stamping segments — and then stops being a sidecar

**Status: NOT STARTED. This is the planning the owner asked for before the
work.** It is the last `region` exemption in `tests/movable.txt` that has any
work behind it (SPEC.md §66.6.1.1) — `apps/skies/csload.asm`, the other one,
has nothing to declare and never will.

The one-line version: **SCRIBE is the only package in the tree that writes its
own segment into memory the kernel does not know about**, so a bare
`OS88_REGION_MOVABLE` there is silent corruption rather than a missed
optimisation. Everything below is about which of three shapes retires that,
and they are separable.

---

## 1. What is actually wrong, exactly

`SCRIBE.OVL` is a 10,121-byte module read into a 12KB claim
(`SC_OVKB equ 12`) the first time a file-format or picture feature is asked
for. It is SPEC.md §2.8's shape rather than §52.11's, and the sentence that
makes it fast is the one that makes it pin us:

> the module keeps **DS = THE PACKAGE'S SEGMENT** and reaches the document,
> the claims and every `sc_*` variable through it exactly as resident code
> does

So the module has to be told our segment, and `sc_ovload` tells it **twice**:

| what | where | filled by |
|---|---|---|
| `sc_pkgseg` | a word **inside the module** | `mov ax, cs / mov [es:sc_pkgseg], ax` |
| six `sc_v_*` vectors | `sc_v_first`..`sc_v_end`, **in our image** | `sc_ovbind`, `mov [bx+2], ax` per row |

The vectors are `dw offset, segment` pairs the module far-calls back through
(`sc_v_saymsg`, `_resize`, `_pictfree`, `_papfind`, `_ldscan`, `_picrec`).
**Both are stamped once, at load, and are meant to last the session.** Move the
region and both name a base that is no longer ours: the module sets DS from a
stale `sc_pkgseg`, and its six ways home far-call into whatever the compactor
packed over us.

### 1.1 What is NOT wrong, and it is worth stating

**The module RUNNING is safe already.** It holds our segment in DS for the
length of one entry, and a region only moves when it is *frameless* (§66.6.1):
the module is only ever entered from resident code, which is only ever entered
from a callback, so `[wm_pkgs]` names our segment throughout and
`mem_frameless` refuses. There is no race to close here — only two words that
outlive the entry that wrote them.

That is the whole defect. It is not a design flaw in the module's shape; it is
that a value with a **session** lifetime was written where an **entry**
lifetime was enough.

---

## 2. Three shapes, and they are separable

### 2.1 Option A — `sc_reloc`, the stopgap (~15 bytes, half an hour)

Declare `OS88_REGION_MOVABLE sc_reloc` and re-derive both stamps on a move.

**It is nearly free because `sc_ovbind` already does the hard half and needs no
change at all.** Inside a relocation proc `CS` is the region's **NEW** base —
`mem_reloc_call` dispatches a region's holder at `[MC_SEG]`, which
`mem_cp_run` has already updated — so `sc_ovbind`'s `mov ax, cs` is right by
construction. `apps/cc/crt0.asm` is the precedent and says so in as many
words: `cc_regreloc equ cc_ovbind`, one line, because the bind proc *is* the
relocation proc.

    sc_reloc:                       ; BX = old base, DX = where we are now
        push ax
        push es
        mov ax, [<the module's claim seg>]
        or ax, ax
        jz .done                    ; never loaded: nothing is stamped
        mov es, ax
        mov [es:sc_pkgseg], dx
        call sc_ovbind              ; CS is already the new base
    .done:
        pop es
        pop ax
        ret

**What it does not do** is stop the stamping, which is the thing the owner
asked for. It leaves a package holding two session-lifetime copies of its own
base and a proc that must be remembered whenever a seventh vector is added.

### 2.2 Option B — bind at ENTRY instead of at load (the owner's fix)

**Move both stamps from `sc_ovload` to `sc_ovcall`.** Then nothing stamped
outlives one entry, there is no proc to write, no proc to keep up to date, and
the region is movable by the plain macro like every other package.

`sc_ovcall` is already the right place and already says so — *"the ONE place
every future verb passes through"*. It is the single funnel: 7 call sites,
8 verbs.

**The cost is six stores and one, on a path that is already disk-bound.**
Every verb is file-format or picture work — `SCM_IMGLOAD`, `SCM_DOCIMG`,
`SCM_DOCPARSE`, `SCM_RTFIMG`, `SCM_RTFPARSE`, `SCM_ISRTF`, `SCM_LDPOST` — so
an entry is a document open, never a keystroke. Against `int 13h` at ~400 ms a
call (PERFORMANCE.md), seven stores is not measurable and does not need to be
measured.

**This is what retires the exemption**, and it is the smaller half of the work.

### 2.3 Option C — make the module a PART (the packaging fix)

`SCRIBE.OVL` is a sidecar file. SPEC.md §20.12 is the standard that exists to
end sidecars, and the tree has already run this exact play once: `C64.ROM` was
a sidecar until wave 6 of docs/plans/completed/O88-MULTISEG-PLAN.md, and the
motivation there is the same sentence — *a copy that took the program and left
the sidecar behind was a machine that could not start*.

**C is INDEPENDENT of B and does not retire the exemption on its own**: a part
is a claim with a segment of its own, so a module living in one still has to be
told where its package is. Do B first; C is a packaging improvement that can
follow whenever it is wanted.

**It is not urgent on disk arithmetic.** SCRIBE is on **no shipped floppy**
today — `make scribedisk` is its own on-demand disk, WIREFRAME's shape — so
the two-file hazard costs nothing until it ships.

---

## 3. Recommendation

**B, then C, and skip A.**

A is worth taking only if the region needs to move before B is written, and
nothing is waiting on it: SCRIBE ships on no disk, so its region is a wall in
nobody's arena today. Taking A would mean writing a proc, a comment about why
the proc exists, and a `tests/movable.txt` edit, and then deleting all three
when B lands.

**B is a few lines and one measurement nobody needs to take.** After it, the
package takes the plain `OS88_REGION_MOVABLE`, both `scribe` lines come out of
the registry, and the tree is at 43 of 44 with `csload` — which has nothing to
declare — the only row left.

---

## 4. What each option has to prove

| | the assertion | how |
|---|---|---|
| A | the proc is CALLED and both stamps follow | `tests/rehomemove.py`'s shape: read the module's `sc_pkgseg` and one `sc_v_*` back and check they name the NEW base. **Read it back through the fixed vector** — a compaction does not scrub what it copied from, so a word the proc never fixed still reads correctly off the old copy |
| B | a module entry after a move still works | open a `.DOC`, force a compaction, open another. The failure is a far call into freed memory, so it is loud |
| C | one file, and the launch path is unchanged | `t_image`/`t_diskverify` walk the disk; `make scribedisk` is the geometry check |

**And whichever lands, the ratchet is the gate that says it happened**:
`tests/unit/t_movable.py` fails if a `tests/movable.txt` line names a package
that now declares, so the exemption cannot rot into a register of things that
used to be true.

---

## 5. The two facts worth not re-deriving

1. **`sc_ovbind` needs no change to be a relocation proc.** `CS` inside one is
   already the new base. That is what makes A two lines and B cheap, and
   `cc_regreloc equ cc_ovbind` is the worked example one package along.
2. **The module is safe while it RUNS.** Every entry is inside a callback, so
   `[wm_pkgs]` pins us for its whole duration. Anyone re-opening this will
   look for a race there and there is not one — the defect is entirely that
   two words outlive the entry that wrote them.
