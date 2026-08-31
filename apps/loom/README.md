# Loom

Loom is the in-OS IDE for the Weave family: it edits a project's sources and
packs the `.WAB` bundle **on the machine**, byte-identical to
`tools/weavesim.py --pack` on the host. One window — a file switcher for the
project's four sources down the left, a plain monospaced source editor to its
right, a status row along the bottom — and `File > Pack Bundle` on `^P`. Pack,
click the open Weave window, `^R`: that is the whole edit–run loop, and it
costs zero new kernel bytes.

**It is not Weave.** `WEAVE.O88` is a separate package with a separate name,
target and disk (the WORD/CWORD precedent: two things may not answer to one
name). What the two share they share as **source** — `apps/weave/weave.h`'s
format constants, `apps/weave/wblob.inc`'s claim accessors,
`apps/weave/wfxc.c`'s formula compiler and `apps/weave/wnum.inc` are
`#include`d and `%include`d by both images and never copied (SPEC.md §20.5.1,
this platform's only code-sharing mechanism).

The binding contract for every byte, atom id and refusal sentence is
[docs/WEAVE-SPEC.md](../../docs/WEAVE-SPEC.md); `apps/loom/loom.h` is this
package's own header and explicitly **not** the contract.

## The files

| file | what |
|---|---|
| `loom.asm` | the shim: the name, the callbacks, the icon, the `.WML`/`.WJS` association, and the include order — which is load-bearing and says why at each line |
| `loom.h` | the workspace layout, the model, the compilers' entry points. **Not the contract** — WEAVE-SPEC is |
| `loom.c` | the state, the geometry, the window, the menus, the callbacks and SPEC.md §75.1's close guard; it `#include`s the parts below, because a C package is one compilation (SPEC.md §73.1) |
| `lmerr.c` | WEAVE-SPEC §10.5's pack-error voice, WEAVE-SPEC §3.1's Latin-1 fold and the scanners' character vocabulary |
| `lmatom.c` | WEAVE-SPEC §2.7's atom interner and the string builder every parser assembles through |
| `lmproj.c` | the project (WEAVE-SPEC §11.2): the four slots, the three claims, load and save, Pack, the sidebar, and SPEC.md §19.9's `SYSTEM/APPDATA/LOOM.CFG` |
| `lmed.c` | the editor — the line table, the caret, and **the damage model**, which is the point of the file and is written out at its head |
| `lmprev.c` | WEAVE-SPEC §1.7's Preview |
| `lmwml.c` | the WML compiler (WEAVE-SPEC §3) — **in the overlay** |
| `lmwjs.c` | the WJS compiler and code generator (WEAVE-SPEC §4) — in the overlay |
| `lmsheet.c` | the FX pre-compiler and the sprite reader (WEAVE-SPEC §5 and WEAVE-SPEC §3.6) — in the overlay |
| `lmwrite.c` | the resolve pass and the bundle writer (WEAVE-SPEC §2) — in the overlay. **This is where byte identity is either true or not** |
| `lmovl.c` | `LOOM.OVL`'s remaining tenants: About and New Project |
| `lmui.inc` | the alert, the scroll bar, the two claim movers, the line scanner and the glass shadow's comparator (assembly) |
| `icon.inc` | the 16×16 icon: a loom — beams, warp threads and a shuttle |
| `lmassoc.inc` | the association block: `.WML` and `.WJS` (WEAVE-SPEC §1.5 step 2) |
| `hosttest/` | `lmhost.c`: the compilers built with the HOST's `cc`, driven by `tests/unit/t_lmpack.py` and diffed against `weavesim --pack`. The dev loop, not the gate — `int` is 32 bits there and 16 bits here |

## Building and testing

```
make loom        # the package and its overlay (needs tools/setup-cc.sh first)
make loomdisk    # ...and the floppy, in all three geometries
make test TESTAPPS=build/loom.img          # boot it in B: on VGA
make test VIDEO=cga TESTAPPS=build/loom.img   # ...and on a 1bpp adapter
```

Then double-click **Disk B**, double-click **FORM.WML**, and you are in the
editor. `^P` packs `FORM.WAB` beside it; the demo bundles the host packer
produced are on the same disk, so the two can be compared without leaving the
machine.

The floppy carries `LOOM.O88` + `LOOM.OVL`, `WEAVE.O88` + `WEAVE.OVL` +
`WEAVE.WSM`, the three host-packed demo bundles **and the three demo project
sources** — because a pack gate has to have a project on the machine to open,
and because the runtime has to be on the same disk for `^R` to close the loop.
All three geometries fit; the 360KB one uses 187 of its 354 clusters.

`--folder SYSTEM/APPDATA` is in the disk recipe on purpose (SPEC.md §19.9):
`LOOM.CFG` holds the last project's folder and the last file slot, on the
volume the application was launched from and deliberately not the boot volume.
A disk without the folder is not an error — the preference is simply not kept.

## What is in the overlay, and what is not

SPEC.md §73.14 splits by **frequency**, not by size: a keystroke's path stays
resident and a menu command's can go out, because a menu command may refuse
and a keystroke may not. So the four compilers, the bundle writer, About, New
Project and the project open are all `ovl_*` and ship in `LOOM.OVL`; the
editor, the sidebar, the claims, **and Save** are resident. Save is the one
worth arguing: it is reachable from the menu, from `^S` and from the close
guard's alert, and a Save that could not happen because the module had gone
missing would lose the user's work at the exact moment the program had just
promised not to.

Every overlay tenant answers "it ran" separately from what it decided, because
a refused overlay returns 0 (`apps/cc/crt0.asm`). That is why `ovl_openproj`
answers 1 at every exit including its own refusals: a 0 can then only mean the
module is not there, and it gets a sentence of its own rather than silence.
