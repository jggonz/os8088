# Weave & Loom

Weave runs web-style applications on the 8088 by inverting the browser: the
components are native, and the app's markup (WML), script (WJS) and formulas
(FX) are compiled at pack time into one `.WAB` bundle the runtime interprets
as a display list plus event-handler bytecode. Loom is the in-OS IDE that
edits the sources and packs the bundle on the machine, byte-identical to the
host packer. The binding contract for every byte, opcode and refusal is
[docs/WEAVE-SPEC.md](../../docs/WEAVE-SPEC.md); `tools/weavesim.py` is the
host reference implementation, and `demos/` holds the three committed demo
projects (FORM, SHEET, PONG) it packs.

## The files

| file | what |
|---|---|
| `weave.asm` | the shim: the name, the callbacks, the icon, the `.WAB` association, and the include order — which is load-bearing and says why at each line |
| `weave.h` | the format's constants and every core's prototypes. **Not the contract** — WEAVE-SPEC is |
| `weave.c` | the state, the window, the menus, the callbacks; it `#include`s the parts below, because a C package is one compilation (SPEC.md §73.1) |
| `wval.c` | the hostile-bundle reader (WEAVE-SPEC §2, WEAVE-SPEC §10.4) — **in the overlay** (WEAVE-SPEC §1.2) |
| `wflow.c` | the flow walk (WEAVE-SPEC §7) |
| `wpaint.c` | the component painter, the hit test and the per-component runtime state (WEAVE-SPEC §6) |
| `wact.c` | the press, the release, the field pool, focus and `^R` (WEAVE-SPEC §6.5 to WEAVE-SPEC §6.8, and WEAVE-SPEC §1.7) |
| `wevent.c` | the ring's doorway, the slices, the errors, the timer (WEAVE-SPEC §4.9, WEAVE-SPEC §4.10 and WEAVE-SPEC §4.11) |
| `wnative.c` | WEAVE-SPEC §6's get/set/method surface and WEAVE-SPEC §8.1's six impure builtins |
| `wstate.c` | WEAVE-SPEC §8.3's `.SAV`, the only file surface |
| `wovl.c` | `WEAVE.OVL`'s own tenants: About and Bundle Info |
| `wblob.inc` | the bundle claim's accessors (assembly) |
| `wdraw.inc` | the paint and hit-test cores (assembly, WEAVE-SPEC §1.2's seam — LOOM's Preview paints with these) |
| `wui.inc` | the shared alert, the arm word, the `os88line` wrappers, and the bridge the bytecode core leaves through |
| `wvm.inc` | **the WJS VM** (WEAVE-SPEC §4). Named by nothing outside itself but `wvm_native`, which is what lets `hosttest/weavevm.asm` run it in a boot sector |
| `hosttest/` | that gate: `weavevm.asm` + `weavevm.sh`, the rcz80test / c64memtest shape |

## Building and testing

```
make weave        # the package and its overlay
make weavedisk    # ...and the floppy, in all three geometries
make weavevm      # the VM against the model, in raw QEMU (seconds, no OS)
python3 tools/os88test.py soak -k 'weave*'      # the family's one command
```

`make weavevm` is the gate to run first after touching `wvm.inc` or
`tools/weavesim.py`: it generates the corpus from the model, assembles the
SHIPPING core into a boot sector and diffs every case's WEAVE-SPEC §8.3 end
state, at two slice budgets, with negative controls it must fail.
