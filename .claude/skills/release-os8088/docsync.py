#!/usr/bin/env python3
"""Bring the website's Docs section (os8088.com/docs/) up to the release build.

    python3 .claude/skills/release-os8088/docsync.py --web "$WEB_REPO"          # report
    python3 .claude/skills/release-os8088/docsync.py --web "$WEB_REPO" --write  # apply

Run from the OS repo, after step 2's builds and `make budget-check` (which
leaves build/smallk/). The Docs section carries three kinds of content, and
this treats each the way it can honestly be treated:

  MEASURED -- regenerated here, with nothing left to judgement.
    * The disk guide's inventory (public/guide/disk-guide.json): every path,
      size, attribute, free-cluster count and embedded README of every image
      it lists, read off build/ by the WEB repo's own
      tools/sync_disk_guide.py, then re-checked with its --check.
    * The memory model's kernel figures (public/js/memory-map.js `builds`),
      from tools/kernsize.py --json --strict on kern_big and kern_small --
      the same recipe the page's caption names.
    * The two captions that quote the snapshot the above came from
      (the disk guide's "N images built from source commit X (build N)" and
      the memory model's "Layout measured from source commit X").

  CHECKED PROSE -- an article whose caption says "Checked against source
    commit X". Its caption is bumped ONLY when no commit since X touched a
    file the article links to; otherwise it is left alone and listed, because
    moving it would claim a review nobody did.

  PROSE -- every other article. Listed with the commits since the snapshot
    that touched the source files it links to (each article's GitHub links
    ARE its source map), so a person reads those and nothing else.

It also lists what only a person can supply: files new to an image that the
guide has no manual or entry for, release images (mkzip.py's ITEMS) the guide
does not list at all, image names the Docs prose quotes that no longer exist,
and GitHub links to OS files that have gone.

It refuses the inventory when an image the guide lists is not built -- the
guide's caption claims ALL of them came from one commit, so a partial sync is
a false caption. Build the missing disks, or ship the release with the guide
on the previous snapshot and say so in the PR body.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

OS_REPO = Path(__file__).resolve().parents[3]
SKILL = Path(__file__).resolve().parent
GH = r'https://github\.com/jggonz/os8088/'
# Files nearly every commit touches. An article linking one is not flagged
# for it -- it would be flagged every release and the list would stop being
# read. Step 4a's read of the whole release log is what covers them.
HUBS = {'Makefile', 'SPEC.md', 'README.md', 'kernel/kernel.asm', 'docs/INDEX.md'}


def git(*args):
    return subprocess.run(['git', '-C', str(OS_REPO), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def kernsize(build, *defines):
    out = subprocess.run([sys.executable, 'tools/kernsize.py', '--json', '--strict',
                          '--build', build, '--ico', build, *defines],
                         cwd=OS_REPO, check=True, capture_output=True, text=True).stdout
    k = json.loads(out)
    return {'image': k['imgpara'], 'cold': k['coldpara'], 'fat': k['fatpara'],
            'low': k['lowpara'], 'vga': k['vgabufpara']}


def articles(html):
    """(id, start, end, body) for each Docs article, in page order."""
    return [(m.group(1), m.start(), m.end(), m.group(0)) for m in
            re.finditer(r'<article\b[^>]*\bid="([^"]+)".*?</article>', html, re.S)]


def sources(body):
    return sorted(set(re.findall(GH + r'(?:blob|tree)/main/([^"#?]+)', body)))


def commits_touching(old, paths):
    paths = [p for p in paths if p not in HUBS]
    if not paths:
        return []
    log = git('log', '--oneline', '--no-merges', f'{old}..HEAD', '--', *paths)
    return log.splitlines() if log else []


def pin_review(manuals, old, new, apply):
    """Manuals link their sources at a pinned commit. Move a manual's pin to
    `new` when nothing it links to changed; return the ones that did."""
    review = {}
    for key, manual in manuals.items():
        html = manual.get('html', '')
        paths = sorted(set(re.findall(GH + rf'(?:blob|tree)/{old}/([^"#?]+)', html)))
        if not paths or old == new:
            continue
        hits = commits_touching(old, paths)
        if hits:
            review[key] = hits
        elif apply:
            manual['html'] = re.sub(GH + rf'(blob|tree)/{old}/',
                                    rf'https://github.com/jggonz/os8088/\1/{new}/', html)
    return review


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--web', type=Path, required=True, help='the os8088-web checkout')
    ap.add_argument('--write', action='store_true', help='apply; the default only reports')
    args = ap.parse_args()
    web = args.web.resolve()
    guide_path = web / 'public/guide/disk-guide.json'
    docs_path = web / 'site/docs.html'
    mem_path = web / 'public/js/memory-map.js'
    for p in (guide_path, docs_path, mem_path, web / 'tools/sync_disk_guide.py'):
        if not p.is_file():
            sys.exit(f'docsync: {p} is missing -- has the Docs section moved? '
                     'Update this script before releasing.')

    guide = json.loads(guide_path.read_text())
    old = guide['build']['commit']
    new = git('rev-parse', '--short=8', 'HEAD')
    count = re.search(r'%define BUILD_NUM (\d+)',
                      (OS_REPO / 'build/buildnum.inc').read_text()).group(1)
    try:
        git('cat-file', '-e', f'{old}^{{commit}}')
    except subprocess.CalledProcessError:
        sys.exit(f'docsync: the Docs snapshot commit {old} is not in this clone. '
                 'A shallow clone answers ancestry wrongly (docs/UPSTREAM.md Rule 0): '
                 '`git fetch --unshallow` and run again.')
    print(f'Docs snapshot {old} (build {guide["build"]["count"]}) -> {new} (build {count})')
    dirty = [l for l in git('status', '--porcelain', '--untracked-files=no').splitlines()
             if not l.split(None, 1)[-1].startswith('vm/')]   # 86Box rewrites its own configs
    if dirty:
        print('  WARNING: the OS checkout has uncommitted changes; the figures will '
              f'not be the ones commit {new} builds.')

    html = docs_path.read_text()
    mem = mem_path.read_text()
    attention = []

    # --- MEASURED 1: the disk guide inventory -------------------------------
    missing = [d['file'] for d in guide['disks'] if not (OS_REPO / 'build' / d['file']).is_file()]
    inventory_ok = not missing
    if missing:
        attention.append('Disk guide NOT synced -- these listed images are not built: '
                         + ', '.join(missing) + '. Build them (step 2) or leave the '
                         'guide on its previous snapshot and say so in the PR body.')
    else:
        sys.path.insert(0, str(web / 'tools'))
        import sync_disk_guide
        result, report = sync_disk_guide.derive(guide, OS_REPO)
        sys.path.pop(0)
        changed = [r for r in report if r['added'] or r['removed'] or r['changed']]
        print(f'\nDisk guide: {len(report)} images read, {len(changed)} with changes')
        for r in changed:
            print(f'  {r["image"]}: +{len(r["added"])} -{len(r["removed"])} ~{r["changed"]}')
        manuals, entries = result['manuals'], result['entries']
        undescribed = set()
        for disk in result['disks']:
            rows = {row[0]: row for row in disk['tree']}
            for r in report:
                if r['image'] != disk['file']:
                    continue
                for path in r['added']:
                    name = path.rstrip('/').split('/')[-1] + ('/' if path.endswith('/') else '')
                    key = name.rstrip('/').replace('.', '_')
                    if len(rows[path]) > 3 or key in manuals or name in manuals or name in entries:
                        continue
                    undescribed.add(name)
        if undescribed:
            attention.append('New on a disk with no manual or entry in the guide (add one to '
                             'disk-guide.json `manuals`/`entries`, matching the house style): '
                             + ', '.join(sorted(undescribed)))
    listed = {d['file'] for d in guide['disks']}
    shipped = set(re.findall(r'"([\w.-]+\.(?:img|iso|VHD))"', (SKILL / 'mkzip.py').read_text()))
    unlisted = sorted(shipped - listed)
    if unlisted:
        attention.append('Release images the guide does not list at all (add a `disks` '
                         'entry, and a `sets` / `diskpages` entry if it is a new set): '
                         + ', '.join(unlisted))

    # --- CHECKED PROSE 1: the guide's manuals pin their source links --------
    pinned = pin_review(guide['manuals'], old, new, apply=False)
    if pinned:
        print(f'\nDisk guide manuals whose pinned sources changed since {old} '
              '(pin left on it; review the manual, then bump its links):')
        for key, hits in pinned.items():
            print(f'  {key}')
            for c in hits:
                print(f'      {c}')

    # --- MEASURED 2: the memory model ---------------------------------------
    builds = {'big': kernsize('build'), 'small': kernsize('build/smallk', '-DKERN_SMALL')}
    for name, b in builds.items():
        line = f'{name}: {{ ' + ', '.join(f'{k}: {v}' for k, v in b.items()) + ' }'
        mem, n = re.subn(rf'\b{name}: \{{[^}}]*\}}', line, mem, count=1)
        if not n:
            sys.exit(f'docsync: no `{name}: {{...}}` in memory-map.js -- update this script.')
        print(f'{chr(10) if name == "big" else ""}Memory model {name:5}: ' + line.split(': ', 1)[1])
    mem = re.sub(r'Source snapshot [0-9a-f]+\.', f'Source snapshot {new}.', mem)

    # --- captions and prose, article by article -----------------------------
    out, at = [], 0
    print(f'\nArticles whose linked sources changed since {old}:')
    quiet = True
    for aid, start, end, body in articles(html):
        out.append(html[at:start])
        at = end
        srcs = sources(body)
        gone = [p for p in srcs if not (OS_REPO / p).exists()]
        if gone:
            attention.append(f'#{aid} links to OS files that no longer exist: ' + ', '.join(gone))
        if aid == 'disk-guide' and inventory_ok:
            body = re.sub(r'read from \d+ images built from source commit [0-9a-f]+ \(build \d+\)',
                          f'read from {len(guide["disks"])} images built from source commit '
                          f'{new} (build {count})', body)
        elif aid == 'memory':
            body = body.replace(f'commit/{old}">{old}</a>', f'commit/{new}">{new}</a>')
        hits = commits_touching(old, srcs)
        checked = 'Checked against source commit' in body
        if checked and not hits:
            body = body.replace(f'commit/{old}">{old}</a>', f'commit/{new}">{new}</a>')
        if hits and aid not in ('disk-guide', 'memory'):
            quiet = False
            tag = ('CHECKED -- caption left on ' + old + '; review, then bump it'
                   if checked else 'prose')
            print(f'  #{aid} ({tag})')
            for c in hits:
                print(f'      {c}')
        out.append(body)
    out.append(html[at:])
    html = ''.join(out)
    if quiet:
        print('  (none)')

    known = {d['file'] for d in guide['disks']} | set(
        re.findall(r'"([\w.-]+\.(?:img|iso|VHD))"', (SKILL / 'mkzip.py').read_text()))
    quoted = set(re.findall(r'\b([\w-]+\.(?:img|iso))\b', html)) - set(
        re.findall(r'-o build/([\w-]+\.(?:img|iso))', html))   # one the reader makes
    makefile = (OS_REPO / 'Makefile').read_text()   # an on-demand target counts
    stale = sorted(n for n in quoted - known if n not in makefile)
    if stale:
        attention.append('Image names the Docs prose quotes that no release image or '
                         'Makefile target produces: ' + ', '.join(stale))

    if attention:
        print('\nNeeds a person:')
        for a in attention:
            print('  - ' + a)

    if not args.write:
        print('\nReport only. Run again with --write to apply the measured changes.')
        return 0
    if inventory_ok:
        sync = [sys.executable, 'tools/sync_disk_guide.py', '--source', str(OS_REPO)]
        subprocess.run(sync + ['--write'], cwd=web, check=True)
        synced = json.loads(guide_path.read_text())
        pin_review(synced['manuals'], old, new, apply=True)
        guide_path.write_text(json.dumps(synced, ensure_ascii=False, separators=(',', ':')) + '\n')
        subprocess.run(sync + ['--check'], cwd=web, check=True)
    mem_path.write_text(mem)
    docs_path.write_text(html)
    print('\nWritten. Now review the articles listed above, then rebuild the site '
          '(tools/build.py) and run tools/linkcheck.py.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
