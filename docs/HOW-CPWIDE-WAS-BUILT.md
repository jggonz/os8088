# How the wide Control Panel was built

A diary, kept as the work happened, for later reuse. Observations are
marked as such; intentions are marked as plans. The contract is SPEC.md 135
and the story list is [docs/plans/CPWIDE-PLAN.md](plans/CPWIDE-PLAN.md).

## 2026-11-21 - the brief, and why a second face

The owner asked for a Control Panel redesign for VGA and VESA, loaded
modularly "without bloat", with a much better Drivers view (the driver
library will grow), a more powerful video mode changer and a richer theme
changer.

What we found first (observed, at main 528648017):

- The panel is 320x151 because CGA is 200 rows; SPEC.md 31.1 pins 151 as
  the minimum that holds nine list rows. Its page pane is 221x132.
- `CTRL.DRV`, the on-demand module that IS the panel, was 16,200 bytes on
  `kern_big` and 16,431 on `kern_mega`, against a 17,408-byte module cap. A
  wide face could not live inside it.
- The pages' click procs are hit test, state change and redraw in one body,
  and on `kern_big` the same body is re-run as the probe that finds the
  control under the pointer. Rewriting that logic for a second face would
  have been the bloat the owner ruled out.

So the design (SPEC.md 135) is a SECOND module loaded beside the first only
on a colour display of 640x480 or more: `CTRL.DRV` keeps every state change
and every compact page; `CPWIDE.DRV` draws the wide chrome and three pages
natively, hosts the rest at an origin inside its larger page area (the
compact page contract already takes its origin in registers), and reaches
the logic through one gate entry. Any refusal opens the compact face.

## The mockups and icons

Codex CLI's image generation drew three full-screen mockups at real
resolution and two icon sheets in the 16-colour palette, from the prompts
kept in `docs/cpwide/MOCKUP-PROMPT.md` and `kernel/cpwart/ICON-PROMPT.md`.
Each run took about five minutes. The mockups came back pixel-exact, in the
8x8 font and the EGA palette, which made them directly buildable rather than
mood boards.

![Drivers](cpwide/cp-drivers-vga-640x480.png)
![Display](cpwide/cp-display-vesa-1024x768.png)
![Theme](cpwide/cp-theme-vga-640x480.png)

## Wave 1 - foundation, three agents in parallel (2026-11-21)

Three agents worked at once, each in its own worktree, while the lead
integrated each branch as it finished:

- **art** (Sonnet) wrote `tools/cpwdat.py`, which turns the two icon sheets
  and `drivers/catalog.toml` into `CPWIDE.DAT` (14,642 bytes, 5,290 packed),
  and the `t_cpwdat` gate. The catalog is what makes the Drivers page scale:
  a new driver gets its icon, category and description from one TOML entry,
  and a driver with no entry still shows. The gate first failed because the
  agent had marked the data file hidden like a boot file; the image gate
  wants it ordinary.
- **vxinfo** (Sonnet) added `VXV_INFO` to `VESA.DRV`: the card's VBE version,
  memory and every offered mode in registers (`drv_call` leaves the driver no
  buffer to write into). It kept `VX_ABI_VER` at 6, because the verb is
  additive the way `VXV_PM` was; stepping it would have refused older
  drivers for nothing. It checked the verb against QEMU's real VBE table
  through the gdb stub: VBE 3.0, 16 MB, ten modes.
- **shell** (Opus) built the module `CPWIDE.DRV`, the open-time choice of
  face, the wide window, hosting of every compact page at an origin, the
  22-opcode gate into `CTRL.DRV`, and the widget kit. Its first cut crossed
  `kern_emu`'s memory rung by 17 bytes, which had 28 bytes of slack. Moving
  the app launch into the gate made the resident open path 2 bytes SHORTER,
  so no rung moved on any kernel. `kern_small` stayed byte-identical.

Observed on the glass after wave 1: the wide face opened on VGA and VESA,
every compact page worked hosted inside it, and CGA still got the compact
panel unchanged. Deleting `CPWIDE.DRV` from a VGA disk gave the compact
panel with no message, which is the designed refusal.

## Wave 2 - the three pages (2026-11-21/22)

- **drv** (Sonnet), the Drivers page: category tabs, a table of 10 rows at
  616x408 and 16 at 760x520, coloured status badges, a Start-at-boot column
  that toggles only the want bit, and a details box with Load, Unload,
  Settings... and Ports.... It added 1,621 bytes and needed no new gate op.
- **thm** (Sonnet), the Theme page: four preview cards drawn in each theme's
  own pens, and the eight Color presets applied with one click.
  `CPWIDE.DRV` writes the preset row and one gate op applies it, so
  THMPAL.DRV's ABI did not change. Its look found a clobbered register that
  stopped Customize... firing.
- **vid** (Opus), the Display page: a mode table read through `VXV_INFO`
  with depth filters, a monitor preview, Apply, a 15-second Test with
  Keep/Revert/Esc and a revert on timeout, the startup-mode box, and an
  information box. The countdown needed a tick on a page other than
  Date/Time, which cost 3 resident bytes (`cp_tickp`) with no rung crossed.
  Building the table found a real `VESA.DRV` bug: a class picked the first
  mode at a resolution, so the compact page's cycle stuck on the 16-colour
  mode. It now picks the deepest. The page came in at 4,707 bytes against a
  3,300 allocation. The lead accepted the overrun because the other two
  pages came in near 1,600 each.

**The 360KB disk.** With all three pages, the 360KB system disk needed 708
clusters of its 706. The owner decided (2026-11-22) that the 360KB system
disk keeps the compact panel, because a 360KB machine is an XT. The wide
face's files ride every other system disk.

**Merging.** The three pages each added a line to the same three dispatch
points (paint, release, key) and padded the gate's opcode table for the
others' reserved numbers. The lead resolved those merges by hand. Reserving
opcode numbers per agent in the brief meant no two agents took the same one.

| | before | after all waves |
|---|---|---|
| `kern_big` resident | 248 left in rung | 262 left (no rung crossed) |
| `CTRL.DRV` big / mega | 16,200 / 16,431 | 16,705 / 16,944 |
| `CPWIDE.DRV` (image + bss) | - | 13,573 (cap 17,408) |
| `CPWIDE.DAT` | - | 14,642 (5,290 on disk) |
