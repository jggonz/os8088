---
name: release-os8088
description: Build os8088 and publish the floppy images to the os8088.com website repo as a pull request, plus a GitHub release on the OS repo. Use when the user asks to cut a release, publish a new build, ship the latest images to the website, or update os8088.com with a new version.
---

# Release os8088

Before starting, read the [shared runtime guide](../../../docs/AGENT-SKILLS.md)
for Claude Code and Codex tool mappings, workflow execution and session state.

Builds the floppy images, publishes them into the website repository next
door along with the notes for its releases page, opens a pull request there,
and cuts a GitHub release on this repo.

**The GitHub release carries the release ZIPS and nothing else** -- four of
them, all written by `mkzip.py` in step 3a:

| zip | holds |
|---|---|
| `os8088-<version>.zip` | every floppy, in a folder per disk size (`1440K/`, `1200K/`, `720K/`, `360K/`, with `128K-RAM/` inside the last two) and `PROGRAMS/<APP>/` for the one-program disks, every image and folder named 8.3; a `README.md` that explains every file, grouped by what the reader is trying to do; a `SHA256SUMS` |
| `os8088-<version>-live.zip` | the live USB image and CD as `LIVEUSB.IMG` and `OS8088.ISO`, the Mac burner as `burn-usb-macos.py`, its own README and SHA256SUMS |
| `os8088-<version>-videos.zip` | the three 86Box machines with the Video Player demos (`make viddemozip`) |
| `os8088-<version>-tools.zip` | the PicoMEM firmware, the kernel ROMs cut against this build, the encoder, the Game Boy precompiler, the file splitter, the Browser's proxy and the DOS tools (`make toolszip`) |

The main zip is the one every release has; the other three are written when
their on-demand targets were built, and named as skipped when not. Eleven
loose `.img` files used to hang off the release, and a reader had to know
which pair they wanted before they could download anything. The website is
unchanged -- its download page still serves the images individually, under
their build names, out of `public/disk/`, because the browser demo streams
them and a table of per-image checksums is the point of that page.

The release notes get written once and used twice: `data/releases.json` in the
website repo (step 4a) and the GitHub release (step 6) say the same thing, and
the site's /releases/ page is that file rendered.

When the release adds a whole program -- or there is a video about it -- it
also gets a **Spotlight page** (step 4b), which is a page of its own under
`/spotlight/` with its own screenshots. A line on the releases page does not
carry a new program.

**Every release also brings the site's Docs section up to the build** (step
4d): `/docs/`'s disk guide, memory model and source-checked articles quote one
source commit, and a release that ships new disks under last release's Docs is
a guide describing software nobody can download any more.

**Read "Writing the copy" below before writing a word of any of them.**

## Writing the copy

The reader is someone who found the project and is curious. They may write
assembly, or they may just like old computers. Write so both finish the
sentence. Assume interest, never knowledge.

**Say it in this order:** what changed, what it does now, and what that means
for someone using it. Nothing else is required.

Rules, in order of how often they get broken:

1. **Short sentences, one idea each.** If a sentence needs a comma-spliced
   aside or a dash to hold together, it is two sentences.
2. **Explain the term the first time you use it**, in the same sentence and in
   a few words: "Hercules, a monochrome graphics card from 1982", "the FAT12
   filesystem DOS floppies use". Do this once per release, not once per
   highlight.
3. **No internal shorthand.** A `§` number, a source label, an `.inc` file, a
   register name or a symbol like `gfx_fill` means nothing outside this repo.
   If a spec section is the authority, name it in a short closing sentence --
   never use it as the explanation.
4. **Numbers instead of adjectives.** "Redraw dropped from 158 character cells
   a frame to 14" beats "much faster". If there is no measurement, say what
   changed and leave the speed claim out.
5. **No marketing.** Cut "powerful", "seamless", "blazing", "beautiful",
   "exciting", "we're thrilled", "the best yet", "finally". No superlatives, no
   exclamation marks, no first-person plural selling the work. State the fact
   and stop.
6. **No fluff.** Every sentence adds something a reader did not already have.
   Do not restate the title in the body. Do not open with throat-clearing
   ("As part of our ongoing work..."). Do not pad a small change into a
   paragraph -- a one-sentence highlight is a fine highlight.
7. **Leave out the war story** unless it changes what someone does. The
   debugging that got you there is interesting to you and to nobody reading a
   download page.
8. **Plain words.** "Faster" not "performant". "Uses less memory" not
   "optimises the footprint". "You can now" not "enables the ability to".

The check before you commit: read each sentence and ask whether someone who
has never opened this repository understands it. If they would have to, rewrite
it or cut it.

An example of the difference, on a real change:

> **Too dense:** The player now gates widget drawing on a word of dirty bits,
> applying the period's update-region idea at the widget -- 28,365 glyph cells
> down to 2,468 over the same ten seconds of playback, per the spec section
> that owns it.

> **Write this instead:** The player used to redraw its whole face every time
> the screen updated, which was more work than the machine could finish between
> frames, so playback stuttered. It now redraws only the parts that changed.
> Over the same ten seconds of music that is 2,468 character cells drawn
> instead of 28,365.

Lengths that fit the page and stay readable:

| field | length |
|---|---|
| `summary` | 2-4 sentences. What this release is, leading with the one thing that matters most. |
| `highlights[].body` | 2-5 sentences. One change each. |
| `notes` | 1-3 sentences, written as instructions to the reader. |

## Locating the two repositories

Never hardcode an absolute path. Resolve both from the current checkout, and
use these variables in every command below:

```bash
OS_REPO="$(git rev-parse --show-toplevel)"
WEB_REPO="${WEB_REPO:-$OS_REPO/../os8088-web}"
```

| repo | what it is | role |
|---|---|---|
| `$OS_REPO` | this checkout | builds the images, gets the git tag and GitHub release |
| `$WEB_REPO` | a sibling checkout of the website | receives the images, the manifest and the release notes, gets the pull request |

If `$WEB_REPO` does not exist, the website half is simply not available in
this working copy. Say so plainly, **do the build and the GitHub release
anyway** (steps 1-3 and 6), and skip steps 4 and 5. Do not go looking around
the filesystem for it, and do not clone it uninvited -- offer, and let the
user decide. If the user keeps their website checkout somewhere else, they can
point at it with `WEB_REPO=/path/to/os8088-web`.

## Arguments

- `$1` (optional) -- the release version, e.g. `v1.0.20260727`. If the user did
  not give one, derive it: take the version the OS reports in its own About box
  (`grep -n 'os8088' kernel/apps.inc` -- currently `1.0`) and append today's
  date, giving `v<osversion>.<YYYYMMDD>`. Tell the user the version you picked.
- `--shots` -- also recapture every screenshot from the new build. Do this
  whenever the change touches anything visible. It boots ~15 QEMU instances and
  takes a few minutes.
- `--no-pr` -- do everything except opening the pull request and the release.

## Steps

### 1. Preflight

Run these and stop if any fails, reporting exactly what is wrong:

```bash
cd "$OS_REPO"
git status --porcelain          # uncommitted OS changes?
git rev-parse --abbrev-ref HEAD
command -v nasm qemu-system-i386 python3
gh auth status
ls "$WEB_REPO/tools/release.py"
```

If the OS working tree is dirty, **stop and ask** whether to release anyway --
the manifest records the commit hash, and releasing uncommitted work makes that
hash a lie. If the user says go ahead, note it in the PR body.

If the website repo has uncommitted changes, stop and ask; the release commits
to a fresh branch off `main` and would otherwise sweep unrelated work into it.

### 1a. Stamp the release (SPEC.md 117.10.1)

INSTALL's network check compares releases, not builds: the release is the
date in this version, `YYYYMMDD`, and it lives on the first line of
`apps/install/OS88VER.TXT`. Write it there, and replace the lines under it
with **exactly four lines of what is new, 40 characters each** - INSTALL
shows them on its first page whenever the network is the pick. Recommend one
of two shapes to the user and let them choose:

- **four points**, one line each, every line starting `* `;
- **three points**, one of which takes two lines - its second line indented
  two spaces, so it reads as the same point:

```
20261024
* Install and upgrade over the network
* RealDMG recompiler on 8088 class
* File Progress, toast, clock and
  volume icon system consolidation
```

Count to 40 including the `* `; a longer line is cut at 40 on the glass, and
a fifth line is never shown (`make nettree` warns on both). Ask the user for
the points if the changes since the last release do not make them obvious.

**Then land it on `origin/main` through a short pull request, before step 2
builds anything.** Never commit it straight onto `main`: the build number is
the commit count (SPEC.md 14.2), so a local commit gives the release kernel
the number the next merge to `main` also gets, `origin/main` keeps the old
release for every later build of INSTALL, and the tag names a commit `main`
does not contain. Never tag the branch commit either - the squash merge
re-hashes it. Merged first, the built tree, the build number and the commit
step 6 tags are all `origin/main`'s tip:

```bash
cd "$OS_REPO"
git switch main && git pull --ff-only
git switch -c "release/$VERSION-version"
$EDITOR apps/install/OS88VER.TXT        # line 1 = the date, then what is new
git commit -m "Release YYYYMMDD: version file" apps/install/OS88VER.TXT
git push -u origin "release/$VERSION-version"
gh pr create --title "Release YYYYMMDD: version file" \
  --body "The release stamp INSTALL checks (SPEC.md 117.10.1)."
gh pr merge --squash --admin --delete-branch
git switch main && git pull --ff-only
head -n 1 apps/install/OS88VER.TXT      # must print YYYYMMDD
```

`--admin` because the ruleset wants a review the sole maintainer cannot give.
With `--no-pr`, stop after the commit on the branch, build from it, and say
in the report that `main` does not carry the release yet. Step 3a refuses to
pack when line 1 is not this version's date.

### 2. Build the images

```bash
cd "$OS_REPO"
make clean && make && make emu
ls -l build/os8088.img build/os8088-120.img build/os8088-720.img \
      build/os8088-360.img \
      build/apps.img build/apps120.img build/apps720.img build/apps360.img \
      build/media360.img \
      build/office360.img build/network360.img build/games360.img \
      build/emu360.img build/util360.img build/more720.img \
      build/small360.img build/small720.img \
      build/emu.img
```

All eighteen must exist -- step 3a refuses to pack without them.

**And the published tree** INSTALL upgrades from (SPEC.md 117.10.2):

```bash
make mega              # kern_mega's system and apps disks, which the mega tree is cut from
make nettree           # build/nettree/{big,mega,emu}/: system + apps, SYSTEM/OS88VER.TXT, OS88.IDX
head -n 1 build/nettree/{big,mega,emu}/SYSTEM/OS88VER.TXT   # each YYYYMMDD
```

`make nettree` builds all three trees, so it runs after `make emu` (above)
and `make mega`: the emu tree is cut from `build/emu.img`, the mega tree from
`build/mega.img` and `build/megaapps.img`.

There are THREE trees, one per kernel, because kern_big, kern_mega and
kern_emu install different kernels: `build/nettree/big/`,
`build/nettree/mega/` and `build/nettree/emu/` (emu.img with the shipped
apps.img). kern_big's INSTALL asks `http://os8088.com/net/big/`, kern_mega's
`http://os8088.com/net/mega/` and kern_emu's `http://os8088.com/net/emu/`.
They are published to the R2 bucket `os8088-net` (SPEC.md 117.10.2's store)
by step 6a, after the GitHub release exists, and never committed to the
website repo. The last release's prefix stays until the maintainer deletes
it. Plain `http://` must serve it: the HTTP drive does not do TLS. **Change
nothing in it by hand once it is built**: `OS88.IDX` carries every file's size and the
drive believes it, so an edited OS88VER.TXT is served cut to its old length.
Edit `apps/install/OS88VER.TXT` and run `make nettree` again, or re-index the
folder in place with `python3 tools/os88httpdrv.py export DIR DIR`. (Four
geometries of each pair since SPEC.md 19's 1.2MB disk, plus the 360KB-only media
disk and SPEC.md 24.6's three 360KB-only category disks -- at that size the
spreadsheet and the chart viewer ship on the office disk and on NO other.)

The build enforces its own invariants, so a build failure here is a real
problem, not something to work around. **Then the kernel budgets, which the
build does NOT enforce exactly**:

```bash
make budget-check
```

Branch and integration work may run a kernel up to 2KB over its budget
(`KERN_GRACE`, docs/KERNEL-MEMORY.md) and `make` lets it; **a release may not
run one byte over**. `budget-check` measures kern_big, kern_small and kern_emu
against 100,864 / 64,000 / 100,864 and fails naming the one that is over. If it
fails, STOP: that is a size pass or a conversation with the owner about the
budgets, never a raise to get the release out. Report each kernel's
`KERN_SIZE` and spare from its output.

**`make emu` is not optional, though `make` does not run it.** It builds
`build/emu.img`, the **emulator system disk** (SPEC.md 9.11.7): the `kern_emu`
kernel -- `kern_big` plus the VMware absolute pointer, `VMMOUSE.DRV` -- with a
`SYSTEM.CFG` that already switches the driver on. In QEMU, VMware, VirtualBox
and the v86 browser emulator the pointer then follows the host mouse with no
grab. It needs nothing a bare `make` does not, which is why `mkzip.py` treats
it as required rather than on-demand. Three things about it that are decided
and not to be "fixed" at release time:

- **It pairs with the SHIPPED `apps.img`.** `kern_emu` holds the same API table
  at the same offsets, so there is no emu software disk and must not be.
- **It is 1.44MB only.** The driver is 386 code, and no machine that needs a
  360KB floppy can run it. The Makefile says a 720KB one is `--size 720` if it
  is ever wanted; that is a Makefile change, not a release step.
- **Its kernel lives in `build/emuk/`**, never in `build/`, so nothing in the
  rest of this procedure boots it by accident.

**Then the on-demand targets**, which `make` does not build and the release
zips carry when they exist -- the one-program disks and the everything sets in
the main zip, the live media, the video machines and the tools in zips of
their own:

```bash
tools/setup-cc.sh                     # SmallerC; needed by cword and allapps
make allapps                          # apps-all-N.img -- every program, a set
                                      # of disks: 1440K/ALLn-144.IMG and
                                      # 1200K/ALLn-120.IMG in the zip
make worddisk cworddisk               # word*.img, cword*.img
make c64disk                          # c64*.img
make weavedisk                        # weave*.img -- the Weave family's disk,
                                      # runtime, editor and sources
make runcpm-src && make runcpmdisk    # runcpm*.img
make scribedisk paccmandisk           # scribe*.img, paccman*.img
make 1942disk midirackdisk            # 1942*.img, midirack*.img
make pixeldisk                        # pixel.img -- PiXEL with its whole
                                      # gallery, which no software disk has
                                      # room for; the 1.44MB disk alone is
                                      # released, as PROGRAMS/PIXEL/PIXEL144.IMG
make xtmp3disk                        # xtmp3*.img -- XTMP3 and its three songs
make apple2rom && make apple2disk     # apple2*.img -- apple2rom fetches the
                                      # ROM once; `make clean` spares it
make live                             # os8088-usb.img + os8088.iso -- the live
                                      # USB image and the live CD (SPEC.md 80),
                                      # the live zip. Needs the fetch on the
                                      # runcpm line and the C toolchain, like
                                      # allapps
make viddemo viddemozip               # the three 86Box machines with the Video
                                      # Player's demo hard disks -- the videos
                                      # zip. The website's Docs disk guide
                                      # lists the VHDs too, and step 4d reads
                                      # every image that guide lists
make toolszip                         # the firmware and utilities -- the tools
                                      # zip: it cuts the kernel ROMs (`make
                                      # rom`, `socketrom`) against THIS tree
                                      # and packs the encoder, the Game Boy
                                      # precompiler and the splitter bundles
make usb-emu                          # os8088-emu-usb.img -- THE WEBSITE'S DEMO
                                      # DISK (SPEC.md 80.7), the same volume on
                                      # kern_emu with VMMOUSE.DRV wanted. In no
                                      # zip; the web repo's release.py
                                      # publishes it, and skipping it leaves
                                      # the demo on last release's build
```

**Build the live media, the video machines and the tools AFTER the last
`make`.** Each carries this build's kernel, or ROMs cut against it that
refuse every other, so `mkzip.py` refuses one older than `build/kernel.sys`
rather than releasing a companion from another tree -- and every commit moves
the kernel, its build number being the commit count (SPEC.md 14.2).

**Three one-program disks are built and NOT released**: `realdmgdisk`,
`redlinedisk` and `loomdisk`, and of `pixeldisk` only the 1.44MB disk is.
Their programs ride the main disks -- RealDMG on the 360KB emulators disk and
the second 720KB software disk, REDLINE on the system disks and the
utilities disk, PiXEL on the software disks with its gallery's five-picture
selection on the second 720KB disk and the everything sets (the whole
gallery on the live media and the VGA and CGA video disks) -- and LOOM is on
the Weave disk; Word's 360KB disk is left out the same way. Build them only
when step 4d's Docs disk guide still lists them, since it refuses an
inventory with a listed image missing.

Offer these, do not assume them. If `tools/setup-cc.sh` cannot run -- no
network, no host toolchain -- **release the eighteen and say which on-demand targets
were skipped**, and know that step 4d then cannot refresh the Docs disk guide,
which reads every image it lists or none; they are a convenience, and a release that waits on one is a
release that does not happen. `mkzip.py` prints the ones it did not find, so
that list is generated rather than remembered. Boot any that were built in step
3 like any other image (they go in B:, `make test TESTAPPS=build/word.img`).

**The live media carries the story files, and that is decided.** Since #188,
`make live` puts every story in `tools/getstories.py`'s MANIFEST on the live
USB image and the live CD, so the live zip carries them inside those two files. The
user decided at v1.0.20260916 that this ships: the MANIFEST is curated to files
that are free to redistribute -- three Infocom and Activision giveaways (the
two Samplers, Mini-Zork, ZTUU) and authors' own freeware -- and none of the
Infocom games that were sold. Do not stop to ask about it again.

What still stays out is the story disk as a zip entry. `zork*.img` is not in
`mkzip.py`'s `ITEMS`, and `STORIES=` can add files a user owns but may not
redistribute, so **never run a release build with `STORIES=` set**. If a new
entry is proposed for the MANIFEST, it has to be free to redistribute, because
it ships in the next release.

### 3. Smoke-test the build before publishing

Never publish an image that has not been booted.

```bash
cd "$OS_REPO"
rm -f build/qmp.sock build/qemu.pid
make test
sleep 8
python3 tools/qmp.py build/qmp.sock 'screendump build/smoke.ppm'
python3 tools/qmp.py build/qmp.sock 'quit'
magick build/smoke.ppm build/smoke.png    # or: convert, on older ImageMagick
```

**Look at `build/smoke.png` with the Read tool.** You are checking for: the
menu bar across the top with the chip glyph, then Locator, File and Builtins;
a Disk A and a Disk B icon down the right-hand side; the mouse pointer.
If the screen is blank or garbled, stop -- do not publish. Delete the two
scratch files afterwards; `build/` is gitignored, but leave it tidy.

**Then boot the emulator disk** -- `emu.img` with `apps.img`, the README's own
QEMU line with no mouse on it:

```bash
python3 .claude/skills/release-os8088/emusmoke.py --shot build/smoke-emu.png
```

It must print `PASS`. A screenshot alone cannot pass this disk: an emulator disk
whose driver never attached boots to the same desktop, silently on the serial
mouse, which is the one failure that matters here. So the script reads the
answer out of the guest -- the driver attached, the backdoor won the mouse, and
three absolute positions sent through QMP land where they were sent -- and kills
only the QEMU it started. **Then look at `build/smoke-emu.png`** the same way as
`smoke.png`: the same desktop, with the pointer in the middle of the screen,
where the script left it. Delete it afterwards.

**If `make live` ran, boot both live images the same way** -- the rule is
per image, and a zip does not exempt the two biggest files in it:

```bash
qemu-system-i386 -display none -qmp unix:build/qmp.sock,server,nowait \
  -drive file=build/os8088-usb.img,format=raw -boot c \
  -chardev msmouse,id=m0 -serial chardev:m0 &
sleep 10
python3 tools/qmp.py build/qmp.sock 'screendump build/smoke-usb.ppm'
python3 tools/qmp.py build/qmp.sock 'quit'
# ...then the same five lines with `-cdrom build/os8088.iso -boot d`
```

**Look at both screenshots.** The desktop must show a Disk A icon AND a
Disk C icon down the right-hand side -- C: is the live partition the kernel
adopted, and its absence means the image booted the kernel and lost the
volume, which is exactly the failure a screenshot catches and a checksum
cannot.

### 3a. Pack the release zips

What the GitHub release gets. Build them only after step 3 has booted the
images -- the zips are what people download, and packing an unbooted image is
the same mistake with an extra layer of wrapping on it.

```bash
cd "$OS_REPO"
python3 .claude/skills/release-os8088/mkzip.py --version "$VERSION"
```

It writes `build/os8088-<version>.zip` and, for each companion that was built,
`build/os8088-<version>-live.zip`, `-videos.zip` and `-tools.zip`, printing
each one's entry count, size and sha256, then the on-demand files it did not
find. **Quote every zip's sha256 in the release notes** -- it is the only
checksum a reader of the GitHub release gets, since there is no longer a
per-file table there.

The script does seven things worth knowing about:

- **Its file list is an allowlist, not a glob.** `build/` also holds the story
  disks and every test gate's scratch image, and a glob ships those the first
  time somebody runs an unrelated target before cutting a release. If a new
  disk should be in releases, add it to `ITEMS` in `mkzip.py`, in the group a
  reader would look for it under -- the README's description of it is written
  there too -- and give it a place: `DEST` names each main disk's path, and a
  one-program disk takes its folder and short name from `PROGRAMS`. A disk
  that fits no group gets a new entry in `GROUPS`, with the instructions for
  using it. `ITEMS` keeps BUILD names because `tools/os88guide.py` and
  `docsync.py` match on them.
- **Every image and folder in the main zip is 8.3** -- `WORD-144.IMG`,
  `360K/128K-RAM/OS88-360.IMG` -- so the unpacked tree can be copied onto an
  os8088 volume and every disk mounted there. A name that is not, or two
  files on one name, stops it. An everything set that grows to ten disks
  would make `ALL10-144.IMG` and stop it too; that is a naming decision for
  the owner, not a fix to make at release time.
- **It refuses to pack a partial release.** A missing image that `make` or
  `make emu` builds stops it; a missing on-demand file is reported and
  skipped, and its zip is not written.
- **It refuses a stale companion.** The live images, the videos zip and the
  tools zip must be newer than `build/kernel.sys` (step 2 says why); one that
  is not names the target to run again.
- **It refuses a release stamped with another date.** Line 1 of
  `apps/install/OS88VER.TXT`, and of each `build/nettree/*/SYSTEM/OS88VER.TXT`
  that is built, must be the `YYYYMMDD` in `$VERSION`; one that is not means
  step 1a was skipped or the tree was cut before it.
- **It deletes this version's companion zips that it did not write**, and
  prints each one: an earlier run's `-live`, `-videos` or `-tools` zip whose
  source has since gone would otherwise ride out on step 6's glob.
- **The zips are byte-for-byte reproducible.** Timestamps come from the date
  in the version string and the images are already deterministic, so the same
  version from the same commit packs to the same bytes. The videos and tools
  zips are `make`'s own, repacked under `os8088-<version>-videos/` and
  `-tools/` with those timestamps. Do not add anything that varies per run.

**Then boot the video machines out of the zip**, since a published image is a
booted one and these three are three hard disks:

```bash
python3 .claude/skills/release-os8088/vidsmoke.py --version "$VERSION"
```

It unpacks the videos zip into a scratch directory and starts each machine
folder in 86Box exactly as it is -- the config, the CMOS it carries, its hard
disk -- so a disk name that does not resolve or a CMOS that stops in BIOS
setup fails here and not on a reader's machine. It must print `PASS`, and it
needs 86Box built (`python3 tools/os88box.py build`, docs/86BOX.md). **Look at
each screenshot in `build/smoke-vid/`**: the desktop, with a Disk C icon.
Delete them afterwards.

Then look inside each zip, the same way step 3 makes you look at the
screenshot:

```bash
cd "$(mktemp -d)" && unzip -q "$OS_REPO/build/os8088-$VERSION.zip"
cd "os8088-$VERSION" && shasum -a 256 -c SHA256SUMS && cat README.md
find . -type f | sort
```

and the same for `-live` (which has a SHA256SUMS too) and, by listing,
`-videos` and `-tools`, whose own READMEs are written by the tools that
build them.

**Read each README with the Read tool.** It is written for someone who has
never seen this project, so "Writing the copy" governs it exactly as it governs
the release notes. The main one is `README.md`: Markdown that reads the same
in Notepad as on GitHub, generated from two tables in `mkzip.py`:

- **`GROUPS`** -- the situations a reader is in, in the order the README takes
  them: *Start here* (the system and software pair per disk size, as a table),
  *More software disks for a 360KB machine*, *A second software disk for a
  720KB machine*, *A machine with as little as 128KB of memory*, *In an
  emulator or virtual machine* (the emulator disk), *Every program, on a set
  of disks*, *One program per disk*, and *About this zip* (checksums and the
  licence files). Each carries the instructions for using its own files --
  the QEMU line -- so they sit beside the files they are about. *No floppy
  drive* is the live zip's README, with the `dd` line and the burner.
- **`ITEMS`** -- the things a reader chooses between, each in one group, with
  the images it comes as and a line saying what it is for. This is also the
  allowlist of what can go in the zips.

Only what was packed is described, a group with nothing packed is left out
whole, and the main README ends by naming the companion zips that were
written. **Check that every file in the unpacked directory has its own line**
in the README, that the version and commit are this release's, and that no
group explains a file that is not there.

The QEMU command lines in it are the real ones -- if `make run`'s invocation
ever changes, `GROUPS` in `mkzip.py` has to change with it, and the way to know
is to run the commands out of the unpacked directory and see the desktop come
up. The emulator group's line is the one `emusmoke.py` boots (on `emu.img`,
which the zip calls `1440K/VMSYS144.IMG`), and it has **no mouse line on
purpose**: QEMU answers the absolute pointer by default, and the serial mouse
beside it would be a second pointing device with the buttons split across the
two.

### 4. Publish into the website repo

```bash
cd "$WEB_REPO"
git checkout main && git pull --ff-only
git checkout -b "release/$VERSION"
python3 tools/release.py --version "$VERSION" --os-repo "$OS_REPO" [--shots]
```

Pass `--os-repo` explicitly rather than relying on its default, which assumes
the OS checkout is the sibling directory `../jop`.

`release.py` copies the four images into `public/disk/`, regenerates the
gzipped copies the browser demo streams, writes `public/releases.json`, adds
this release to `data/releases.json`, and rebuilds the site. The download
page's table of sizes and checksums is generated from that manifest at build
time, so it cannot drift.

#### 4a. Write the release notes into `data/releases.json`

**This is yours to write -- the script cannot.** `release.py` fills in only
what it reads off the build: version, date, commit, kernel size, file list. It
leaves `summary`, `highlights` and `notes` empty and prints a reminder saying
so. The releases page (`$WEB_REPO/site/releases.html`, published at
os8088.com/releases/) renders them, so an unfilled entry ships as a version
number with no story attached.

Write the same words you are about to put in the GitHub release notes in step
6 -- write them once, here, and reuse them there. **Follow "Writing the copy"
above for all three fields:**

- `summary` -- what this release is, in 2-4 plain sentences, most important
  thing first.
- `highlights` -- one entry per change worth reading about: `title`, the
  optional PR number as `issue`, and a `body` that explains it rather than
  restating the title. Titles are plain too: name the change, do not sell it.
  HTML is allowed in `body`; keep it ASCII, and use `--` the way the rest of
  the site does.
- `notes` -- anything that changes how the system is *used* and would
  otherwise surprise someone (a menu item that moved, a default that flipped).
  Write it as an instruction to the reader. Rendered as a call-out.

**Check whether the images actually changed** -- `git diff --stat HEAD --
public/disk/` in the website repo, after `release.py` has run. The build is
deterministic, so a release whose work was all in a package or on the story
disk produces four images byte for byte identical to the previous release.
That is a fine release to cut, and it is a lie by omission not to say so: the
headline feature is not in the download, and someone will boot the image
looking for it. Put it in the summary and again in `notes`.

The optional `ramBytes` / `ramCap` / `sourceLines` / `modules` fields render
the size figures on the page. `ramCap` is `KERN_CODE_MAX` -- **65536**, and
read it out of `kernel/kernel.asm` rather than trusting this line, because it
has moved once already. The other three are not printed by the build, so
measure them -- from `$OS_REPO`:

```bash
# ramBytes: image + .bss, the number the build-time assertion guards.
# The kernel refuses to assemble over the cap, so bypass the %error to read it.
sed 's/%error "kernel too big.*/%warning bypassed/' kernel/kernel.asm > /tmp/ksz.asm
printf '%%assign KT KTEXT_SIZE\n%%assign KB KBSS_SIZE\n%%warning KTEXT=KT KBSS=KB\n' >> /tmp/ksz.asm
nasm -f bin -I kernel/ -o /dev/null /tmp/ksz.asm     # warning prints both; ramBytes = KT + KB

# sourceLines and modules: the boot sector, kernel.asm and everything it
# actually includes, the SDK header, and every package's .asm AND the .inc
# files that .asm includes. The dead kernel .inc files are not included by
# anything and do not count; neither does apps/frotz/zharness.inc, which is
# development-only and never in a shipped build.
#
# ANCHOR THE GREP. `grep '%include'` also matches the word in a comment, and
# both figures were wrong for it: a stray match became a garbage filename wc
# silently skipped, and `grep -c` counted it as a 35th kernel module when
# there are 34.
python3 - <<'EOF'
import glob, os, re
def n(p):
    return open(p, 'rb').read().count(b'\n')
def incs(path, dirs):
    out = []
    for f in re.findall(r'^\s*%include\s+"([^"]+)"', open(path).read(), re.M):
        for d in dirs:                  # an .asm names its include EITHER
            q = os.path.normpath(os.path.join(d, f))   # beside itself or
            if os.path.exists(q):       # apps/-relative, because that is the
                out.append(q)           # -I nasm is given. Resolve BOTH: the
                break                   # apps/-relative form is how cword and
    return out                          # runcpm name theirs, and only trying
                                        # the sibling form silently drops them
kern = incs('kernel/kernel.asm', ['kernel'])
apps = sorted(glob.glob('apps/*/*.asm'))
pkg = []
for a in apps:
    for q in incs(a, [os.path.dirname(a), 'apps']):
        if q not in pkg and 'zharness' not in q and 'crt0' not in q:
            pkg.append(q)               # crt0.asm is the C runtime, counted
                                        # once with the SDK rather than per
                                        # C package that includes it
files = ['boot/boot.asm', 'kernel/kernel.asm'] + kern + ['apps/os88api.inc'] + apps + pkg
files = list(dict.fromkeys(files))
print('sourceLines', sum(n(p) for p in files if os.path.exists(p)))
print('modules    ', len(kern))
# The C is NOT in sourceLines -- the page labels that figure "lines of
# assembly". Count it separately and quote it in the entry when it moves.
csrc = [p for p in sorted(glob.glob('apps/*/*.c') + glob.glob('apps/*/*.h'))
        if 'hosttest' not in p]
print('C lines    ', sum(n(p) for p in csrc))
EOF
```

**This recipe changed AGAIN at v1.0.20260818 and the series steps there too.**
Package includes are named `apps/`-relative as often as they are named beside
the `.asm` -- `%include "cword/cwmove.inc"` -- and resolving only the sibling
form dropped them, cword's and runcpm's bulk among them; the same pass also
counted `apps/os88api.inc` and `apps/cc/os88thunk.asm` twice, once as a glob
hit and once as an include, hence the `dict.fromkeys`. Net 9,324 lines out on
that tree: the old recipe reported 222,280 lines, this one 231,604, and the
previous release recounts to 228,855 -- **which is the number that entry's
note quotes, so the +2,749 that is real growth is not read as +9,765.**
Entries before v1.0.20260818 still hold their own recipe's numbers.

**And it changed at v1.0.20260810 before that.** It used
to glob `apps/*/*.asm` only, which missed the `.inc` files four packages keep
their bulk in -- ArtfulType, ModPlug, Tracker and Frotz, 37,309 lines between
them at that release. The old recipe reported 115,528 lines and 35 modules for
that tree; this one reports 152,837 and 34, and `data/releases.json` was
restated to the new figures rather than left carrying a known undercount.
Entries before it still hold old-recipe numbers and are not being recounted --
so **the step is between v1.0.20260809 and v1.0.20260810, and the entry says
so.** When a figure jumps because the counting changed, say so where the
figure is, or the jump reads as growth that did not happen.

If you update these, the same figures may be hardcoded in the website's prose --
`site/index.html` and `site/docs.html` have quoted the kernel size, the RAM
footprint, the headroom or the line count, and pages come and go (the FAQ, How
It Works and Download pages are gone; `/docs/` replaced the FAQ). Grep the old
numbers across `$WEB_REPO/site/` and fix them in the same PR; nothing validates
them.

Then rebuild and verify:

```bash
python3 tools/build.py        # must report 0 problems
python3 tools/linkcheck.py    # must report 0 dead
git status --porcelain
```

**Look at the releases page before you commit it.** Serve `public/` and read
it, the same way step 3 makes you look at the smoke screenshot -- a release
whose entry renders as an empty window is worse than no page at all:

```bash
(cd "$WEB_REPO/public" && python3 -m http.server 8099 &) && sleep 2
# then open http://localhost:8099/releases/ and check this release's window
# has its summary, its highlights, its figures and its four files
```

#### 4b. The Spotlight page, when the release earns one

`/spotlight/` is the hub for one-page write-ups: a new program, a feature too
big for a highlight, or anything with a video about it. `site/spotlight.html`
is the index and `site/spotlight/<name>.html` is the page. The nav already
carries Spotlight (File menu in `tools/build.py`, and the footer dock in
`site/_layout.html`), so a new page needs no wiring beyond its own entry on the
index.

**Decide first, and it is usually no.** A bug fix, a speed-up or a new menu
item is a highlight on the releases page and nothing more. Write a Spotlight
page when a reader would want a page: a program that did not exist before, or a
video that needs somewhere to live. If in doubt, ask the user rather than
producing a page nobody asked for.

**1. Capture the screenshots.** They come out of the emulator, never a mockup.
Scenes live in the website repo beside the others:

```bash
cd "$WEB_REPO"
# scenes.frotz.json is the worked example: five scenes, one per story.
python3 tools/capture.py --scenes tools/scenes.<name>.json \
        --out public/img/<name> --repo "$OS_REPO" --jobs 3
```

A scene may set `"diskB": "<image>.img"` to put a different floppy in drive B:
than the software disk -- that is how Frotz's scenes reach the story disk
`make zdisk` builds. Whatever the page shows must be built first; `make` alone
does not build an on-demand disk.

**Look at every captured PNG with the Read tool.** A lost click gives a
plausible-looking screenshot of the wrong thing, and file size will not tell
you: two of Frotz's five first came out sitting on an unanswered "Do you need
instructions?" prompt with an otherwise empty window.

**2. Write the page.** Copy `site/spotlight/frotz.html` and change it. Set
`body_class: spot` in the metadata block -- that is what turns on the article
layout, and without it the page is the ordinary stack of one-screendump-wide
windows, which is what these pages exist to stop being.

The layout has **two widths and nothing between**: 800px for anything with
sentences in it, the full two columns for anything to look at. Alternating them
is the structure. The pieces, all in the stylesheet's `spotlight` section:

| | |
|---|---|
| masthead | a `.grid` of two: the pitch, `.specs` with three numbers, the buttons and a `.spot-toc` of jump links -- beside the one screendump that proves it |
| `.spot-band` | inverted full-width section heading, each with one line saying why you would read that section. These are the anchors somebody skims by |
| gallery | the screendumps in a plain `.grid`, two abreast |
| `.spot-cards` | a `.grid` of short titled cards, for what would otherwise be one window with four `h3`s in it |
| `.spot-steps` | numbered instructions, for the part somebody follows while typing |

Order: masthead, what it *is* for someone who has never heard of it, the video,
the gallery, how it works, what it deliberately does not do, how to run it.

The copy rules above apply, plus one more: **a Spotlight page is written for
someone who does not know the subject at all**, so explain the domain and not
only the change. The Frotz page spends three paragraphs on what a Z-machine is
before it says a word about the implementation. That is the right proportion.
Still no marketing, still numbers instead of adjectives, and still no `§`
numbers or symbol names.

Two things a reader gets in the first five seconds, so write them last and
hardest: the **deck** (`.spot-deck`, one or two sentences that are the whole
page) and the **three numbers** in the `.specs` block. If you cannot fill those
three cells with facts, the page is probably a highlight and not a spotlight.

The copy rules above apply, plus one more: **a Spotlight page is written for
someone who does not know the subject at all**, so explain the domain and not
only the change. The Frotz page spends three paragraphs on what a Z-machine is
before it says a word about the implementation. That is the right proportion.
Still no marketing, still numbers instead of adjectives, and still no `§`
numbers or symbol names.

**3. Embed the video, if there is one.** Reuse the markup on the Frotz page --
an `<a class="vid__poster" data-video="<id>">` around an
`<img src="/videos/thumb/<id>.jpg">`, plus `<script src="/js/videos.js" defer>`
at the foot of the page. That gets the same bargain `/videos/` makes: the
poster is a plain link until someone presses play, the still is proxied through
this site so loading the page tells YouTube nothing, and the embed is
`youtube-nocookie.com`, which is the one host `frame-src` allows.

Get the real title from YouTube rather than inventing one:

```bash
curl -sS "https://www.youtube.com/oembed?url=https%3A//www.youtube.com/watch%3Fv%3D<id>&format=json"
```

`/videos/thumb/` is served by the Worker, not from `public/`, so **the poster
is a broken image on a local `http.server` and correct in production.** That is
expected. `tools/linkcheck.py` knows -- `WORKER_ROUTES` -- and a new
Worker-served path has to be added there or the link check fails on a link that
works.

**4. Add it to the index and link it.** One `figure.win.shot` block at the top
of the list in `site/spotlight.html` (newest first), and a pointer from
wherever a reader would otherwise look for it -- for Frotz that is
`site/applications.html`, which lists the programs it is *not* among. Link the
page from the matching `highlights[].body` in `data/releases.json` too.

**5. Rebuild, link-check, and look at both pages** in the browser, the same way
step 4a makes you look at the releases page. Three things that will fool you:

- **Look at it at 1600px wide.** A row of two screendumps needs a 1,360px
  viewport, so at anything narrower the page folds to one column and looks
  exactly like the thing this layout replaced. Check 390px too -- nothing may
  scroll sideways.
- **The stylesheet is cached and `build.py` does not touch it.** `public/css/`
  is committed by hand, so a CSS change plus a reload shows you the old page. A
  band that renders as plain text on the dither is that, not a broken rule.
- **The video poster is a broken image locally** and correct in production; see
  step 3.

#### 4c. The Wire's library, every time

**The site is now where the machine gets its software from** (SPEC.md 92), so
a release ships a *catalog* as well as four floppy images, and nothing on the
OS side can tell you it went wrong: a stale `catalog.bin` is a Wire that lists
last release's programs at last release's sizes and hands out last release's
bytes, and the window looks perfectly healthy while it does it.

Three things, in this order:

**1. `tools/release.py` fills `public/wire/pkg/`.** It copies every file
`data/wire.json` names out of `<os-repo>/build/` — the `.o88`s and their
sidecars — the way it already copies floppy images into `public/disk/`. A name
it cannot find is a refusal, not a warning. **If a package was added, renamed
or split this release, `data/wire.json` needs the edit before this runs**, and
the tier and the description are a judgement somebody makes rather than a
field to fill: check the entry against that program's own SPEC section or its
Spotlight page, and flag every tier you touched as reviewable in the PR.

**Then check the cell fence (SPEC.md 92.2).** A package that calls an API
cell an older kernel at the same package format lacks needs
`"needs_wire": 3` (or the current top level) on its `data/wire.json` entry,
or an older machine's Wire offers a program that calls past its kernel's
table. The website's writer cannot see the calls; step 3's `--verify
--pkgdir` from the OS repo can, and it fails on a missing or stale
declaration and on a call newer than any level. When it reports a call past
`WIRE_TABLE_END`, the release adds a level row in `tools/os88wire.py` and
raises `WIRE_LEVEL` there and in `apps/thewire/wcat.inc` -- an OS change
that has to merge before the images are built.

**2. The site build packs the catalog.** `tools/wire.py`, called by
`build.py`, writes `public/wire/catalog.bin` and `public/wire/pic/<STEM>.PIC`
from `data/wire.json` and `public/wire/pkg/`. It is a build product committed
under `public/` like every other one, so the deploy job's
`git status --porcelain public/` check is what catches a build that was not
re-run.

**3. Check it changed, and verify it FROM THE OS REPO.** A package changed and
a catalog that did not is the failure this step exists for:

```bash
git -C "$WEB_REPO" status --porcelain public/wire/
python3 tools/os88wire.py --verify "$WEB_REPO/public/wire/catalog.bin" \
                          --pkgdir "$WEB_REPO/public/wire/pkg"
python3 tools/os88wire.py --dump   "$WEB_REPO/public/wire/catalog.bin"
```

Run from the **OS** repo on purpose. `tools/os88wire.py` and the website's
`tools/wire.py` are two independent writers of one format (SPEC.md 92.2), and
this is the one moment they meet: the OS repo's reader checking the website's
bytes, with `--pkgdir` cross-checking every declared size and every embedded
icon against the files actually published. Read the `--dump` output against
what you know shipped — a missing program, a stale size or a `NEW` mark left
on last release's entry are all things only a person notices.

Nothing may redirect `http://` to `https://` for `/wire/*`. The machine
cannot follow one, and a redirect there is a Wire that says
`The Wire did not answer (301)` on every fetch.

#### 4d. The Docs section, every time

`/docs/` (`$WEB_REPO/site/docs.html`) is source-backed, and every part of it
names the OS commit it was read from. Bring it to the release commit in the
same PR; a Docs section one release behind is wrong in ways no build check
catches. It has three kinds of content, and `docsync.py` treats each the way
it can honestly be treated:

| content | where | how it moves |
|---|---|---|
| the **disk guide's inventory** -- every path, size, attribute, free-cluster count and embedded README on every image | `public/guide/disk-guide.json` | **measured**: the web repo's `tools/sync_disk_guide.py --write`, then `--check`, reading `$OS_REPO/build/` |
| the **memory model's kernel figures** | `public/js/memory-map.js`'s `builds` | **measured**: `tools/kernsize.py --json --strict` on kern_big and kern_small |
| captions quoting the snapshot (`N images built from source commit X (build N)`, `Layout measured from source commit X`) | `site/docs.html` | moved with the measurement |
| articles captioned **`Checked against source commit X`** (the Bootloader), and the guide's **manuals**, which link their sources at a pinned commit | `site/docs.html`, `disk-guide.json` `manuals` | **bumped only when nothing they link to changed**; otherwise left on X and listed |
| every other article -- Quick start, Supported Hardware, Storage, Networking, the developer guide | `site/docs.html` | **listed** with the commits since X that touched the files it links to |

```bash
cd "$OS_REPO"
python3 .claude/skills/release-os8088/docsync.py --web "$WEB_REPO"          # report
python3 .claude/skills/release-os8088/docsync.py --web "$WEB_REPO" --write  # apply
```

Run it after `release.py`, from the OS repo, with step 2's build still in
`build/` (it reads `build/smallk/` too, which `make budget-check` leaves).
The report is the work list; `--write` applies only the measured parts.
**Then do what it lists, which is the part that is yours:**

- **Each listed article and manual: read the commits it names against what
  the page says**, and fix what they made untrue -- a new driver on the
  Supported Hardware pages, a changed boot message in the Bootloader's table, a
  renamed image in Quick start. Each article's GitHub links ARE its source map,
  so a commit that touched none of them is not listed. Files nearly every
  commit touches (`Makefile`, `SPEC.md`, `README.md`, `kernel/kernel.asm`,
  `docs/INDEX.md`) do not trigger a listing; step 4a's read of the whole log
  covers those. When an article with a `Checked against` caption, or a manual
  with pinned links, has been reviewed, move its commit to the release commit
  by hand -- **never move one you did not read**, because that caption is the
  claim that someone did.
- **New files with no manual.** A new package, driver or folder on a disk
  shows in the guide as a bare file until `disk-guide.json` gets a `manuals`
  entry (keyed `NAME_EXT`) or an `entries` one. Write it in the voice of the
  manuals beside it and the "Writing the copy" standard -- what it is, how to
  start it, what it needs -- and take any screenshot it carries from a real
  boot into `public/guide/shots/`, looked at, like a Spotlight shot.
- **Release images the guide does not list** -- a new on-demand disk in
  `mkzip.py`'s `ITEMS` -- need a `disks` entry (and a `sets`/`diskpages` one
  if it starts a new set) before the next `--write` will read them.
- **Images the guide lists that are not built** make it refuse the inventory
  outright: the guide's caption says all of them came from one commit, so a
  partial sync is a false caption. Build them, or leave the whole guide on its
  previous snapshot and say so in the PR body and the report.
- **Dead GitHub links** (an OS file an article links to that has gone) and
  image names the prose quotes that nothing builds any more.

Then rebuild, which re-versions `docs.js`, `memory-map.js` and the guide JSON
so a returning reader's browser does not keep last release's copy:

```bash
cd "$WEB_REPO"
python3 tools/build.py                                  # 0 problems
python3 tools/linkcheck.py                              # 0 dead
python3 tools/sync_disk_guide.py --source "$OS_REPO" --check
git status --porcelain public/guide/ public/js/memory-map.js site/docs.html
```

**Look at `/docs/` before you commit it**, the way 4a looks at the releases
page: serve `public/`, open `#disk-guide`, pick a disk this release changed and
a file it added, and open `#memory` at 128K and 640K on both kernel builds. An
inventory that parses and a guide that renders an empty pane read the same in
`git diff`.

### 5. Commit and open the pull request

```bash
cd "$WEB_REPO"
git add <explicit paths changed by this release>
git commit -m "Release $VERSION"
git push -u origin "release/$VERSION"
gh pr create --title "Release $VERSION" --body "<body>"
```

The PR body should state: the version, the OS commit it was built from, the
kernel size and its change since the last release, the four image sizes, and
whether screenshots were recaptured. If `--shots` ran, say which screenshots
actually changed (`git diff --stat public/img/shots/`) -- if a UI change was
expected and nothing changed, that is a signal something is wrong.

The diff carries no network tree: those go to R2 in step 6a, never into
this repo. Check the diff includes `data/releases.json` with its prose filled
in. A release PR that touches the images and the manifest but not the release
log means step 4a was skipped.

Say what step 4d did to the Docs section: the snapshot commit it moved from
and to, the images whose inventory changed, the manuals and articles you
revised (and any you left on the old commit, with why), and the files still
without a manual. If the guide stayed on the previous snapshot because an image
was not built, the PR body says that in its first lines.

If step 4b produced a Spotlight page, say so in the PR body and give its path,
list the scenes captured, and name the video it embeds. Mention that the video
poster only resolves once the Worker is serving the page, so a reviewer reading
a local build does not file it as a broken image.

When the session calls for an attribution line, name the host that actually
performed the release. For Claude Code:

```
🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

For Codex:

```
Generated with [Codex](https://developers.openai.com/codex/)
```

### 6. Tag and cut the GitHub release

Only after the images are booted and the zips are unpacked and checked.

```bash
cd "$OS_REPO"
git tag -a "$VERSION" -m "os8088 $VERSION"
git push origin "$VERSION"
gh release create "$VERSION" \
  --title "os8088 $VERSION" \
  --notes "<notes>" \
  build/os8088-"$VERSION".zip build/os8088-"$VERSION"-*.zip
```

**The assets are the zips `mkzip.py` wrote, and nothing else.** Do not attach
loose `.img` files beside them -- that is the arrangement this replaced, and
half of each is worse than either. Confirm the files exist before running the
command (`ls build/os8088-"$VERSION"*.zip`): a missing asset makes `gh
release create` fail with the tag already pushed, which is the one failure in
this whole procedure that cannot simply be re-run. A companion `mkzip.py`
named as skipped is simply absent from the glob; say so in the notes.

Because the release page no longer lists a file per image, the notes have to
say what each zip contains and how big it is. For the main zip: one sentence
naming the pair a reader actually needs -- `1440K/OS88-144.IMG` and
`1440K/APPS-144.IMG` -- that the disks are in a folder per disk size with the
larger programs in `PROGRAMS`, the packed size, and the sha256 step 3a
printed. Name the emulator disk: someone running os8088 in QEMU, VMware,
VirtualBox or a browser should hear that `1440K/VMSYS144.IMG` goes in drive A
in place of `OS88-144.IMG` and that the pointer then follows their own mouse
with no click to capture it. Then one line per companion with its size and
sha256: the live zip -- a reader with no floppy drive needs to hear that
`LIVEUSB.IMG` and `OS8088.ISO` boot a PC or an emulator directly -- the
videos zip, three 86Box machines to drop into 86Box's machines folder, and
the tools zip, the firmware and the utilities. Someone who wants a per-image
checksum finds it in the SHA256SUMS inside the main and live zips, or on the
download page, and the notes should say which.

`gh` infers the repository from the checkout's remote, so this works for a
fork or a rename without any edit here.

The notes should name what changed since the previous tag
(`git log <prev>..HEAD --oneline`), the kernel size, and point at the download
page of whatever site this project publishes to (os8088.com/download/ for the
upstream project). A commit log is not release notes -- "Writing the copy"
applies here exactly as it does on the website, and a reader who lands on the
release from a search has no more context than one who lands on the page.

These are the notes you already wrote in step 4a. Say the same thing in both
places -- the releases page exists to mirror this, and the two disagreeing is
worse than either alone. If the wording improved while writing these, go back
and update `data/releases.json` to match before the website PR is merged.

### 6a. Publish the network trees (SPEC.md 117.10.2)

Only after the GitHub release exists. Publishing is the one act every
installed machine notices by itself - INSTALL's check then tells it a newer
release is out - so it comes last, when the page and the zips it would go
looking for are already there. With `--no-pr` there is no release, so
there is no publishing either.

```bash
cd "$WEB_REPO"
python3 tools/netpublish.py --release YYYYMMDD "$OS_REPO/build/nettree"
python3 tools/netpublish.py --verify
```

The first uploads every file of `build/nettree/` to the R2 bucket
`os8088-net` under `YYYYMMDD/<kernel>/<path>` (`big`, `mega` and `emu`), and
only then writes `CURRENT`, the one pointer the Worker reads, so `/net/`
switches from the last release to this one whole. The website commit carries
no tree. The second fetches
`http://os8088.com/net/{big,mega,emu}/SYSTEM/OS88VER.TXT` and checks that
each answers `200` with no `Location`, carries a `Content-Length`, says
`YYYYMMDD` on line 1, and answers a Range request with `206`. **If it fails,
stop and report**: a redirect, a 404 or a chunked answer reads as "no answer"
to every installed machine. Leave the last release's prefix in the bucket;
deleting it is the maintainer's, and a machine mid-install is still reading
it.

### 7. Report

Tell the user: the version, the version-file PR step 1a merged, the PR URL,
the release URL, the kernel size and its delta, each zip's packed size and
how many images went into the main one, what step 6a's `--verify` printed
for each of the three trees, the
Spotlight page's URL if step 4b ran, the Docs snapshot commit step 4d moved
to (or that it did not, and why), and anything you skipped or that needs
their attention -- naming any on-demand disk or companion zip that did not
get built, since a reader of the README cannot tell a disk that was left out
from one that does not exist. If the website PR is merged, the site's own CI
deploys from its `main` branch -- say so, and say the site is not live until
that merge happens.

Driving the OS to take screenshots is also the most thorough anyone uses it all
week, and it turns defects up. Report those separately from the release, with
what you did and what happened -- they are not release business, and burying
them in a status line is how they get lost.

## Rules

- **Never publish an unbooted image.** Step 3 is not optional, and packing one
  into a zip does not make it booted -- which is why the video machines are
  booted out of their zip by `vidsmoke.py`.
- **The GitHub release gets the zips `mkzip.py` wrote and nothing else.** Never
  attach loose images beside them, and never build a zip from a glob of
  `build/` -- the `ITEMS` table in `mkzip.py` is the list, and it exists so a
  story disk or a test gate's scratch image cannot ride out with a release.
- **Never release a companion from another tree.** The live media, the video
  machines and the tools carry this build's kernel or ROMs cut against it;
  `mkzip.py` refuses one older than `build/kernel.sys`, and the answer is to
  rebuild it, never to touch its timestamp.
- **Never publish an emulator disk whose driver did not attach.** `emusmoke.py`
  is how step 3 knows, and a desktop screenshot is not.
- **Never hand-assemble a zip.** `mkzip.py` writes the READMEs, the
  checksums, the 8.3 layout and reproducible archives; a `zip -r` by hand gets
  none of those and looks identical until somebody checks.
- **Never invent a checksum or a size.** Everything on the download page comes
  from `releases.json`, which `release.py` computes from the actual bytes.
- **Never ship a release with an empty entry on the releases page.** Step 4a is
  not optional either: the numbers are generated, the notes are not, and a
  version with nothing written against it is what an unfilled entry looks like
  to a reader.
- **Never ship copy that only a contributor can read.** Spec section numbers,
  symbol names and register talk stay out of the notes, and so does marketing
  language. "Writing the copy" is the standard for the website entry, the
  GitHub release and any Spotlight page.
- **Never put a screenshot on a Spotlight page that you have not looked at.**
  Step 4b captures them from a real boot and you read every one. A mockup, a
  crop of an old shot, or a scene whose click was lost are all the same defect
  to a reader: a picture of something that did not happen.
- **Never move a Docs "checked against" commit you did not check.** Step 4d's
  script moves only what it measured or what nothing touched; the rest is a
  person reading the commits it lists.
- **The licence is MIT** -- `LICENSE` is in the root of this repo and the
  website's colophon says so. Do not restate the terms in release copy, and do not claim a
  different one.
- **Never publish the network trees before the release they name.** Step 6a
  comes after the GitHub release, and the version file reaches `main` through
  step 1a's pull request before anything is built.
- Both repositories get branches, never direct commits to `main`.
- If any step fails, stop and report rather than continuing with a partial
  release. A half-published release is worse than none.
