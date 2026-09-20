# HANDOFF: the icon store's shed repair, and the `dosglyph` red it carries

**Branch `elendilon-icostale`. NOT READY TO MERGE.** The feature is built,
measured and gated; it turns `soak -k dosglyph` red **5 runs out of 5**, and
the cause is not identified. This file is the elimination table, so whoever
picks it up starts from the evidence rather than from the beginning.

## What the feature is

SPEC.md 25.9.5. A DOS box claims the whole arena, which is a full compaction,
which correctly drops the purgeable machine-wide icon store — and nothing put
the pictures back, because **a repaint is not a mount**. Every Disk window on
screen went on drawing SPEC.md 25's generic icon until the user navigated
somewhere else. Reported from the field as *"the file manager redraws, but
does not reload its icon cache assoc.dat so things draw with no icons"*.

Two halves:

1. **`[ico_n]` survived the shed.** `mem_pg_forget` carries one naming word
   per owner, so `[ico_seg]` went to 0 and the count beside it did not.
   SPEC.md 54.7.4 put a second half on `asc_use`'s re-entry compare *for
   exactly this case* — `cmp byte [ico_n], 0` — and **the byte it reads was
   never cleared, so that guard could not fire.** The next mount skipped the
   one three-sector `ASSOC.DAT` re-read and paid a sector per package instead.
2. **The reference bytes.** `fmv_icostale` marks every Disk window with a
   cache `FSD_ICONS`; `fm_focus` spends it with `fmv_icorefs`, which stands on
   the folder quietly, absorbs `ASSOC.DAT` once and walks the two passes of a
   mount that read no sectors.

**MEASURED, `os8088_xt_hdd_sb`, `A:\APPS` (14 entries, 23 store rows):
the repair is 3 reads / 13 sectors against the re-list's 5 / 30, and both end
with the same 23 rows** — so it is cheaper without doing less.
`tests/icoshed.py` is the gate and is VERIFIED RED two ways.

## The `dosglyph` red, and everything it is NOT

`dosglyph` step 5 forces a store miss and expects the harvest to read
`DOS.O88`'s first sector and take the SHIPPED glyph. With this branch it takes
the REDUCTION instead. Step 4 (the hit) and step 6 (the hit on the harvested
row) both pass.

Rates, all from builds whose exit code was checked:

| tree | result |
|---|---|
| branch point (`0ae926d8`) | **0/5 fail** |
| this branch, whole | **5/5 fail** |
| this branch, `mem_pg_forget`'s `.ico` arm disabled (so `ico_demote`, `fmv_icostale` and `fmv_icorefs` are all unreachable) | fail |
| this branch, the mount's `call dsk_docpass` removed | fail |
| branch point **+ the two routine bodies only**, uncalled, mount untouched, no other file changed | **fail** |
| branch point + 8 inert bytes at the same spot | ok |
| branch point + 170 inert bytes at the same spot | ok |
| branch point + 340 inert bytes at the same spot | ok |
| branch point + 600 inert bytes before `ico_find` | ok |

**So DEAD CODE flips it, and the same number of inert bytes in the same place
does not.** That is the finding, and it is why this is a handoff rather than a
fix: the two new routines are never entered in the configuration that fails.

Ruled out, each by measurement and not by reading:

- **the cold rung / `KERN_SIZE` / the heap ladder.** The 600-byte padding
  build has `cold 41,442`, rung 41,472, `KERN_SIZE 110,592` — the *identical*
  rung state to this branch — and passes.
- **the repair replacing a re-list.** The store is never shed during
  `dosglyph`: probed at three points, `ico_seg` = 24E0 and `[ico_n]` = 11
  throughout, and `FS_DIRTY` = 0. `fmv_icorefs` never runs.
- **the document pass.** Removing the mount's call to it does not fix the red,
  and the branch point has it.
- **the entry count.** `open APPS -> 7 entries` then `5` appears on the branch
  point too, where step 5 passes.

## Where to look next

1. Whether `dosglyph` is position-dependent in some way the inert padding does
   not reproduce — a REFERENCE from a new address rather than bytes at one.
   The two routines call `dsk_get_dir_x`, `ico_ref_try`, `assoc_docicon_x`,
   `ico_key_doc` and `ico_put` from `.cold` addresses those symbols were not
   called from before.
2. `tools/os88bisect.py classify dosglyph` on a committed pair, which is the
   protocol this investigation did by hand.
3. Whether `dosglyph` itself is the fragile thing. It was already seen red
   once in an unrelated 73-row soak on `elendilon-next` and green 4/4 after.
