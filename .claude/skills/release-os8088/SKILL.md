---
name: release-os8088
description: Build os8088 and publish the floppy images to the os8088.com website repo as a pull request, plus a GitHub release on the OS repo. Use when the user asks to cut a release, publish a new build, ship the latest images to the website, or update os8088.com with a new version.
---

# Release os8088

Builds the four floppy images, publishes them into the website repository next
door, opens a pull request there, and cuts a GitHub release on this repo with
the images attached.

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
| `$WEB_REPO` | a sibling checkout of the website | receives the images + manifest, gets the pull request |

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
gzipped copies the browser demo streams, writes `public/releases.json`, and
rebuilds the site. The download page's table of sizes and checksums is
generated from that manifest at build time, so it cannot drift.

Then verify:

```bash
python3 tools/build.py        # must report 0 problems
python3 tools/linkcheck.py    # must report 0 dead
git status --porcelain
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
upstream project).

### 7. Report

Tell the user: the version, the PR URL, the release URL, the kernel size and
its delta, and anything you skipped or that needs their attention. If the
website PR is merged, the site's own CI deploys from its `main` branch -- say
so, and say the site is not live until that merge happens.

## Rules

- **Never publish an unbooted image.** Step 3 is not optional.
- **Never invent a checksum or a size.** Everything on the download page comes
  from `releases.json`, which `release.py` computes from the actual bytes.
- **Do not claim a software license.** This repo carries no LICENSE file. If
  the user wants one, that is a separate decision, not part of a release.
- Both repositories get branches, never direct commits to `main`.
- If any step fails, stop and report rather than continuing with a partial
  release. A half-published release is worse than none.
