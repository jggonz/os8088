# Kernel hot-paths pass 1: the patches kept outside the knobs

`tools/README.md` beside this file indexes the agents' harnesses and
instruments, parked here too: the equivalence check for each knob, the
timers and tracers, and the size finders.

Three of the pass's byte-costing items could not become `KHP_*` knobs in
`kernel/kernel.asm`'s KHP block (docs/plans/khp1/LOG.md sections L.2 and
L.3), so they are kept here as patches. They are **references for a re-do,
not drop-ins**: each is against the tree it was written on, not today's.

| patch | what it is | against | why not a knob |
|---|---|---|---|
| `disk4-loader-cylinder-fill.patch` | disk-4 (+208): the loader's header peek arms the read-ahead's fill hint at the file's length, and the fill runs to the CYLINDER's end over several cache slots in ONE `int 13h`: one call fewer on every multi-track launch on 360KB and 1.44MB (measured, FDC calls) | `kernel-hotpaths` `1217b318` | it rewrites the same `dsk_rah_fill` and `disk_mount_x` code the shipped standalone disk-5 does (LOG L.1), so it cannot sit beside it as an arm. The owner's review: re-do it as an explicit prefetch call rather than an armed byte |
| `disk4-size17-multi-stamp.patch` | the size agent's -7 on disk-4's `.multi` stamp (string stores) | `kernel-hotpaths` `6538be65` | it edits disk-4's code |
| `growbox-blit1.patch` | the grow box composed into one `gfx_blit1` (+150 `.text`, +39 `.bss`; ~8 ms a grow paint on CGA, PREDICTED) - LOG O10 | the wm agent's branch | refused on value per byte by its author; never committed |

## disk-4's SPEC text, as it stood on `kernel-hotpaths`

`kernel-hotpaths-lean`'s SPEC.md 18.95.11 describes the listing mount alone
(its disk-5 needs no loader half). This was the section when both reads
filled further, kept word for word so a re-do of disk-4 starts from its
contract:

> #### 18.95.11 …and two reads that know what follows them fill further
>
> Kernel hot-paths pass 1 (docs/plans/khp1/disk.md). A fill reads forward from
> the sector that missed to the end of the TRACK, nine sectors at most, because
> a fill is a guess. Two reads in the system are not guessing, and for those
> the guess is replaced by what they know - through one byte, `[dsk_rah_hint]`,
> the sectors the caller will read from the one it asks for, armed across that
> one read and disarmed straight after it:
>
> - **The loader's header peek** (§21 step 2). The peek is one sector and the
>   whole file follows it, so with the hint set to the file's sectors the fill
>   runs to the bound `dsk_xfer`'s own reads use - the CYLINDER on a two-headed
>   floppy whose FDC was seen to cross a head (§18.91.1, `[dsk_cylrun]`), else
>   the track - and no further than the file. Longer than one slot, it is one
>   `int 13h` into the first *m* slots, each record then stamped with its own
>   nine, so every slot stays an ordinary chunk a later fill evicts alone.
>   Measured (FDC calls): Note Pad, TexPad and Paint each launch in one call
>   fewer on 360KB and on 1.44MB, Calc as before.
> - **A listing mount's LBA 0.** A full mount empties the cache at step 1, now
>   BEFORE the boot-sector read and not after the FAT window, and reads LBA 0
>   through it with the hint at **8** - the most every rule-11 geometry has on
>   its first track under the fallback 9/2. On 360KB, 720KB and 1.2MB those
>   eight are the boot sector, the FAT window and the root's first sectors, so
>   a navigation spends one call where it spent two or three. The rule the old
>   "never from the cache" stated is kept exactly: the boot sector is still the
>   first thing this mount read off the platter, and so is every byte served
>   after it. The chunk is re-tagged with the NEW disk's signature straight
>   after `dsk_bpb_sig`, before anything can look it up. A quiet mount reads
>   LBA 0 as it always did, around the cache.
>
> Not on kern_dos (no room in its image) and not on kern_mega (its store above
> 1MB lands through a nine-sector bounce): `DSK_RAHINT` is undefined there.
