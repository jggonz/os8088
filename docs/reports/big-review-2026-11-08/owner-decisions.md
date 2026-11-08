# The owner's answers to the big review (2026-11-08)

The owner's decisions and reactions as they were given, in the owner's words
where it matters, while reading the review. The plan
(`docs/plans/BIG-REVIEW-PLAN.md`) and the syntheses are brought into line with
these; this file is the record of what was said.

## Decided

- **FILECP and FDLG as kern_big modules (WP2b, report decision 4): REFUSED** -
  as it has been "20+ times now (almost once per size pass)". kern_small can
  afford them as modules: a one-disk distribution with no hard disk, drivers
  or DOS, where being able to run a program at all beats managing and opening
  files faster. kern_big cannot: the user has swapped in a DOS, office or
  utility disk and cannot be made to put in the system disk and wait every
  time they open or copy a file - "open and copy file are not 'system
  management' actions". A boot-time cache is not a saving either. Recorded in
  CLAUDE.md, AGENTS.md and SPEC.md 38 so it is not proposed again.
- **A 14th boot-blob sector on kern_big (decision 2): NO.** It delays the
  progress bar by 0.4-0.8 s (one or two more int 13h). Instead, design a
  third boot-time section at the tail of `kernel.sys`, read while the splash
  is already up and dropped before the `.vga` drop and heap compaction, so
  `.boot2` can go back to "draw the splash and load kernel.sys":
  `docs/plans/BOOT3-PLAN.md`.
- **Packages are trusted (decision 3).** Real mode cannot defend against a
  hostile package (and on kern_mega an FSX real-mode bracket hands it real
  mode anyway), so the only thing worth defending against is our own
  mistakes, and most of the time those are better found and guarded at
  BUILD time than with resident bytes. Resident guards pay where the failure
  is catastrophic and user-visible and the printout gives the owner useful
  information (the stack-overflow panel). The six package-argument fences
  (BR-8's doors, BR-22, BR-29, BR-30, BR-37, BR-173) are not taken as retail
  code. Recorded in CLAUDE.md and AGENTS.md.
- **The fault panel (decision 1).** On kern_mega, where the kernel can afford
  not to crash, closing the faulting package beats killing the OS. In real
  mode a divide fault deserves at least a stack-overflow-style message where
  it cannot be recovered from, since a picture of it lets the owner trace
  who divided - but it has not been met in production, so the panel's
  resident bytes (the ~174-byte package answer included) wait behind a build
  KNOB until a field report says it is being hit. "That is a balancing act."
  The same weighing applies to every diagnostic panel for a failure nobody
  has met. Recorded in CLAUDE.md and AGENTS.md.
- **`.bss` zeroing.** "iirc we didn't used to, and now do" - confirmed from
  the code: zero on every shipped path since PR #61
  (`areas/gap-bss-zeroing.md`); the comments are corrected.
- **BR-6 (plain MDA driven with Hercules timings).** This can damage the
  monitor. os8088 does not run on an MDA: print an MDA message and halt, or
  switch to the usable CGA when one is present.
- **The ABI/API version (A4, abi-6, gfr-5).** Liked in general, but packages
  and drivers already carry versions checked before anything runs, so a
  run-time API level would only catch a mistake in that versioning - and the
  same mistake could break the new number. It must prove its worth before it
  spends bytes.
- **KHP (decision 9, WP11).** Waiting to be revived; each row is reviewed
  individually on whether it earns its space.

## Reactions that adjust the review

- "Most of the agents seemed to like our current structure, overall."
- **The boot canary (BR-1)** is pretty critical. One known machine, a Pravetz
  8088, cannot use cylinder floppy reads; it is reported as booting fine but
  more slowly today - so this may have broken recently. Under investigation:
  `followup-canary.md`.
- **Work running on whatever context was there (theme 1).** It has caused one
  or two problems, not shutdown-related: a sound interrupt arriving can drive
  a package already close to its stack's edge over it. The mouse and idle
  already have stacks of their own; more work there is not needed
  immediately.
- **Purgeable claims for packages.** Needs a list of real use cases to prove
  it is worth doing: `followup-purgeable.md`.
- **".LNK desktop shortcuts"** already exist - anything runnable can be
  dragged onto the desktop, `.LNK`s included. The review is swept for other
  recommendations of things the tree already has:
  `followup-already-exists.md`.
- **rt-6, kern_small's module cache (decision 5):** interesting - "keep FDLG
  as purgeable" might be worth ~100 bytes.
- **Decisions 6, 7 and 8** asked to be explained: `followup-decisions-6-8.md`.

## Second round (the same day)

- **Item 6, the keyboard mouse (rt-7): TAKE IT, as "instead of", not
  "always, then dropped".** Verified first: the keyboard mouse IS resident on
  every kernel - its code is in `.text` (`kbm_tab` .. `kbm_ev`,
  `build/kernel.map` offsets 29E9h..2D77h on kern_big), and the size passes
  that touched it (16, 17, 18) trimmed bytes, they never made it droppable.
  The owner: one cluster on disk for a decent amount of resident RAM, for
  something only active during boot, is a good trade - but every boot must
  not get longer, so `KBMOUSE.DRV` is loaded ONLY when no mouse is found.
  A machine with a mouse never reads it.
- **Item 7, `font_run_x`'s VGA prologue into `.vga` (text-7): TAKE IT.**
  Worthwhile at 0.048% of a full Note Pad repaint (0.78% in the absolute
  worst case).
- **Item 8, the mono residency figure (rt-5): print it.** A tracking/doc
  improvement - mono gains are pure gains for mono, so `kernsize` should
  show them.
- **`.LNK` and the desktop as a folder.** The owner questioned the entry in
  the report's "would NOT copy" list because shortcuts and `.LNK`s already
  exist (SPEC.md 26.8, 26.10). The refusal itself stands: making the desktop
  a folder was already looked into, and it is not a bad idea at its core,
  only too expensive in disk reads and resident bytes to manage for a
  machine without a 95-era system's resources. Reword the entry to say
  exactly that.
- **The runtime API level (A4, abi-6, gfr-5): TAKE IT IF IT IS CHEAP.** The
  loaders gate on `<=` PKG_FMT rather than `==`, so a package newer than the
  kernel can still call a cell the kernel lacks. An `==` check on an API
  level keeps the `<=` PKG_FMT gate (rather than a list of supported formats
  per kernel) while refusing to crash on an old apps disk in a new kernel,
  or the reverse.
- **`.boot3`:**
  - make it GENERIC across the kernels - it buys them all space and gives
    them the same boot ladder, so fewer mistakes reach another kernel -
    unless kern_small truly does not need it;
  - the name: whatever confuses a new session reading the boot least;
  - wave 4: YES - nothing should stay bigger than it needs to be, and the
    quicker splash is a benefit;
  - a kern_big 128KB boot row: NO - it works and should keep working, but
    nobody runs it that way; there was a row and it was taken out;
  - `blobruns` into the fast tier if it is really quick (or full);
  - the earlier bar without wave 4 (show the bar after the plain head, then
    read on): an option only if it is less complex than wave 4.
