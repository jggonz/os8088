---
name: release-os8088
description: Build os8088 and publish the floppy images to the os8088.com website repo as a pull request, plus a GitHub release on the OS repo. Use when the user asks to cut a release, publish a new build, ship the latest images to the website, or update os8088.com with a new version.
---

# Release os8088

Builds the four floppy images, publishes them into the website repository next
door along with the notes for its releases page, opens a pull request there,
and cuts a GitHub release on this repo with the images attached.

The release notes get written once and used twice: `data/releases.json` in the
website repo (step 4a) and the GitHub release (step 6) say the same thing, and
the site's /releases/ page is that file rendered.

**Read "Writing the copy" below before writing a word of either.**

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

### 2. Build the images

```bash
cd "$OS_REPO"
make clean && make
ls -l build/os8088.img build/os8088-360.img build/apps.img build/apps360.img
```

All four must exist. The build enforces its own invariants -- a 512-byte boot
sector and a kernel that fits under offset 0xA000 -- so a build failure here is
a real problem, not something to work around. Report the kernel size; if it has
grown, say by how much and how much headroom is left (the ceiling is 0xA000 =
40,960 bytes for image + bss).

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
menu bar across the top with the chip glyph, File and Special; the dithered
desktop; a Disk A (and Disk B) icon at the right; the dock strip at the bottom.
If the screen is blank or garbled, stop -- do not publish. Delete the two
scratch files afterwards; `build/` is gitignored, but leave it tidy.

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

The optional `ramBytes` / `ramCap` / `sourceLines` / `modules` fields render
the size figures on the page. `ramCap` is always 40960 (`0xA000`). The other
three are not printed by the build, so measure them -- from `$OS_REPO`:

```bash
# ramBytes: image + .bss, the number the build-time assertion guards.
# The kernel refuses to assemble over the cap, so bypass the %error to read it.
sed 's/%error "kernel too big.*/%warning bypassed/' kernel/kernel.asm > /tmp/ksz.asm
printf '%%assign KT KTEXT_SIZE\n%%assign KB KBSS_SIZE\n%%warning KTEXT=KT KBSS=KB\n' >> /tmp/ksz.asm
nasm -f bin -I kernel/ -o /dev/null /tmp/ksz.asm     # warning prints both; ramBytes = KT + KB

# sourceLines and modules, counted the way the FAQ counts them: the boot
# sector, kernel.asm and everything it actually includes, the SDK header and
# the example packages. The dead .inc files are not included and do not count.
# Keep the include list as an inline $(...): this shell is zsh, where an
# unquoted $VAR does NOT word-split and wc would be handed one long filename.
wc -l boot/boot.asm kernel/kernel.asm \
      $(grep '%include' kernel/kernel.asm | sed -E 's/.*"(.*)".*/kernel\/\1/') \
      apps/os88api.inc apps/*/*.asm | tail -1      # sourceLines
grep -c '%include' kernel/kernel.asm                # modules
```

If you update these, the same figures are hardcoded in the website's prose --
`site/index.html`, `site/faq.html`, `site/how-it-works.html`,
`site/download.html` and `site/how-it-works/graphics.html` all quote the kernel
size, the RAM footprint, the headroom or the line count. Grep the old numbers
across `$WEB_REPO/site/` and fix them in the same PR; nothing validates them.

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

### 5. Commit and open the pull request

```bash
cd "$WEB_REPO"
git add -A
git commit -m "Release $VERSION"
git push -u origin "release/$VERSION"
gh pr create --title "Release $VERSION" --body "<body>"
```

The PR body should state: the version, the OS commit it was built from, the
kernel size and its change since the last release, the four image sizes, and
whether screenshots were recaptured. If `--shots` ran, say which screenshots
actually changed (`git diff --stat public/img/shots/`) -- if a UI change was
expected and nothing changed, that is a signal something is wrong.

Check the diff includes `data/releases.json` with its prose filled in. A
release PR that touches the images and the manifest but not the release log
means step 4a was skipped.

End the PR body with:

```
🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

### 6. Tag and cut the GitHub release

Only after the images are verified.

```bash
cd "$OS_REPO"
git tag -a "$VERSION" -m "os8088 $VERSION"
git push origin "$VERSION"
gh release create "$VERSION" \
  --title "os8088 $VERSION" \
  --notes "<notes>" \
  build/os8088.img build/os8088-360.img build/apps.img build/apps360.img
```

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

### 7. Report

Tell the user: the version, the PR URL, the release URL, the kernel size and
its delta, and anything you skipped or that needs their attention. If the
website PR is merged, the site's own CI deploys from its `main` branch -- say
so, and say the site is not live until that merge happens.

## Rules

- **Never publish an unbooted image.** Step 3 is not optional.
- **Never invent a checksum or a size.** Everything on the download page comes
  from `releases.json`, which `release.py` computes from the actual bytes.
- **Never ship a release with an empty entry on the releases page.** Step 4a is
  not optional either: the numbers are generated, the notes are not, and a
  version with nothing written against it is what an unfilled entry looks like
  to a reader.
- **Never ship copy that only a contributor can read.** Spec section numbers,
  symbol names and register talk stay out of the notes, and so does marketing
  language. "Writing the copy" is the standard for both the website entry and
  the GitHub release.
- **Do not claim a software license.** This repo carries no LICENSE file. If
  the user wants one, that is a separate decision, not part of a release.
- Both repositories get branches, never direct commits to `main`.
- If any step fails, stop and report rather than continuing with a partial
  release. A half-published release is worse than none.
