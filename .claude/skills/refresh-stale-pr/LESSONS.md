# What refreshing a stale PR cost to learn

Read before step 3 of [`SKILL.md`](SKILL.md). Each item happened on a real
refresh in this repository, and each one was silent - git, the PR's own
tests and the build all reported nothing.

---

## 1. A clean merge is not a valid PR

**PR #200** (the USB/CF imager keeping `SYSTEM.CFG`) was 28 commits behind.
`main` had never touched `tools/os88imager.py`; the merge was clean and the
PR's 35 tests passed on it. But in those 28 commits #203 had taken FAT16
volumes to 2GB, and os8088's own installer writes partition type **06h** at
32MB and over (`drivers/hdd/partw.inc`), while the kernel mounts 04h, 06h and
0Eh (`drivers/hdd/hdcom.inc`, `hd_part_isfat`). The PR's reader hard-coded
04h, because the imager's *own* image is 04h - so every card the installer
had set up would have lost its settings with a "choose no" message.

What found it was reading `main`'s commit titles for the PR's subject area
and then the **kernel's acceptance rule** for the thing the PR reads, not the
diff of the PR's own files. The rule: a host tool should accept at least what
the kernel accepts, or it refuses disks real users have.

## 2. Fixtures encode the author's assumptions

The same PR's tests built their FAT16 volume with the PR's own assumptions
(type 04h, 16-bit sector count), so they could never have caught §1. The
end-to-end check that did prove the fix built the old card the way the
*installer* would - type 06h, 32-bit total sectors, past 32MB - and wrote it
into the real `build/os8088-usb.img` at both geometries, then ran
`os88disk.py --verify-hdd` on the result. Build at least one fixture from
what the *other* producer of the format writes.

## 3. A check must be able to fail

During #200's end-to-end run, a "source image untouched" check compared the
image's SHA-256 with itself and printed `source-untouched`. It proved
nothing and was nearly reported as evidence. Before quoting a check, ask what
input would make it print the other answer; if there is none, delete it.

Related: monkeypatching inside a loop (`f = lambda: real_f(...)` re-bound on
each pass) recursed on the second pass. Patch once, outside the loop, or use
`unittest.mock.patch` as a context manager.

## 4. Strictness belongs on the write side

#200 refused any card whose two FAT copies differed anywhere. The kernel
writes both from one buffer (`kernel/diskw.inc`, `dskw_flush`), so they
disagree only after an interrupted flush - exactly when a user re-images a
card. The fix kept the guarantee where it matters: reading the OLD card needs
only the settings chain to agree in both copies; restoring into the NEW image
still requires the whole FAT to agree. When a tool refuses, ask which side of
the operation the refusal actually protects.

## 5. The branch is already checked out somewhere

The PR's branch was checked out in `../os8088-imager-settings`, the worktree
it was written in, so `git worktree add /tmp/pr200 feat/...` refused. A local
branch off `origin/<headRef>` and `git push origin HEAD:<headRef>` updates
the PR without touching that worktree, which may hold uncommitted work.

## 6. Environment skips are not passes

`make test-full` in a fresh worktree skips the MartyPC and C-toolchain rows
(`SKIP bootsmoke (needs marty)`, `SKIP ctoolchain (needs cc)`): the worktree
has no `build/martypc` and no `build/cc`. That is fine for a host-only
change - say which rows skipped and why. For a change those rows exercise,
stage MartyPC into the worktree (copy main's `build/martypc`, then `make
marty`; a symlink fails rows) and run `tools/setup-cc.sh` before calling the
gate green.
