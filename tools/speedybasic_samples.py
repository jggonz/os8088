#!/usr/bin/env python3
"""Materialize the Speedy BASIC demo corpus from its vendored source.

The web project keeps its demos as TypeScript template literals.  os8088
ships ordinary .BAS files and cannot depend on a sibling checkout, so the
canonical extracted files live in apps/speedybasic/demos.  When the sibling
source is present this tool also proves that the vendored copy still matches
it byte for byte before writing build/speedybasic-demos.
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
from pathlib import Path

SAMPLE_RE = re.compile(
    r"\{\s*id:\s*'([^']+)',\s*name:\s*'([^']+\.BAS)',.*?"
    r"\bsource:\s*`(.*?)`,?\s*\}", re.S)
EXPECTED = 29
# One upstream display name is longer than FAT 8.3 permits. The source bytes
# are unchanged; only its disk-facing name is shortened deterministically.
DISK_NAMES = {"CUBEPOKEHD.BAS": "CUBEPKHD.BAS"}


def fail(message):
    print("speedybasic_samples: " + message, file=sys.stderr)
    raise SystemExit(1)


def parse_typescript(path):
    text = Path(path).read_text(encoding="utf-8")
    rows = []
    for _sample_id, name, source in SAMPLE_RE.findall(text):
        # The upstream corpus deliberately contains no template substitutions
        # or escaped backticks. Refuse those constructs rather than silently
        # producing BASIC different from what the web interpreter receives.
        if "${" in source or "\\`" in source:
            fail(f"{name}: unsupported TypeScript template construct")
        name = DISK_NAMES.get(name, name)
        rows.append((name, source.replace("\r\n", "\n").encode("ascii")))
    if len(rows) != EXPECTED:
        fail(f"{path}: found {len(rows)} samples, expected {EXPECTED}")
    names = [name for name, _ in rows]
    if len(set(names)) != len(names):
        fail(f"{path}: duplicate sample name")
    return rows


def read_vendored(directory):
    base = Path(directory)
    names = [line.strip() for line in
             (base / "MANIFEST.TXT").read_text(encoding="ascii").splitlines()
             if line.strip() and not line.startswith("#")]
    rows = [(name, (base / name).read_bytes()) for name in names]
    if len(rows) != EXPECTED:
        fail(f"{base}/MANIFEST.TXT lists {len(rows)} samples, expected {EXPECTED}")
    extra = sorted(p.name for p in base.glob("*.BAS") if p.name not in names)
    if extra:
        fail("unlisted vendored samples: " + ", ".join(extra))
    return rows


def digest(rows):
    h = hashlib.sha256()
    for name, data in rows:
        h.update(name.encode("ascii") + b"\0")
        h.update(len(data).to_bytes(4, "little"))
        h.update(data)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--vendor", default="apps/speedybasic/demos")
    ap.add_argument("--source-ts",
                    default="../speedybasic/src/features/samples/samples.ts")
    ap.add_argument("--check", action="store_true",
                    help="check the output directory instead of rewriting it")
    args = ap.parse_args()

    vendored = read_vendored(args.vendor)
    source = Path(args.source_ts)
    if source.exists():
        upstream = parse_typescript(source)
        if upstream != vendored:
            vmap, umap = dict(vendored), dict(upstream)
            changed = sorted(set(vmap) ^ set(umap) |
                             {n for n in set(vmap) & set(umap)
                              if vmap[n] != umap[n]})
            fail("vendored corpus differs from sibling web source: " +
                 ", ".join(changed))

    out = Path(args.output)
    if args.check:
        try:
            actual = [(name, (out / name).read_bytes()) for name, _ in vendored]
        except OSError as exc:
            fail(str(exc))
        if actual != vendored:
            fail(f"{out}: generated samples are stale")
    else:
        out.mkdir(parents=True, exist_ok=True)
        for old in out.glob("*.BAS"):
            old.unlink()
        for name, data in vendored:
            (out / name).write_bytes(data)
        shutil.copyfile(Path(args.vendor) / "MANIFEST.TXT", out / "MANIFEST.TXT")

    print(f"speedybasic_samples: {len(vendored)} demos, "
          f"{sum(len(data) for _, data in vendored)} bytes, sha256 {digest(vendored)}")


if __name__ == "__main__":
    main()
