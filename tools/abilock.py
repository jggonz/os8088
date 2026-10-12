#!/usr/bin/env python3
"""The ABI lock: a format number does not get to stand still while the ABI moves.

    python3 tools/abilock.py                    # check (what `make` runs)
    python3 tools/abilock.py --update "why"     # record the surface as it is

SPEC.md 20.2.0.1. `PKG_FMT` is the byte the loader compares for EQUALITY
before it far-calls a package's cells, and `DRV_VER` is the same byte for a
driver. They are only worth anything if somebody BUMPS them when the surface a
package or a driver is built against changes - and until this file that was a
person remembering to. #214 re-contracted two cells at the same numbers and
the bump happened because a reviewer saw it.

So each number is LOCKED to a fingerprint of the surface it guards:

  PKG_FMT  apps/os88api.inc
  DRV_VER  apps/os88api.inc and drivers/os88drv.inc (a driver far-calls the
           same table, through the same SDK)

and `tools/abi.lock.json` holds, per number, the value and the fingerprint it
was recorded at. The check fails when the fingerprint moved and the lock was
not updated, and says what to decide:

  - the surface changed so that something BUILT BEFORE can no longer run on
    this kernel (a cell moved, a cell's registers or meaning changed, a header
    or record grew): bump the number, then --update with the reason;
  - it changed and everything built before still runs (a cell ADDED at the
    end, a contract clarified, a comment fixed): --update with the reason and
    no bump.

Either way the reason is written into the lock and the diff, which is where a
reviewer meets it. A bump the lock was not told about fails too, so the lock
never describes a number the tree no longer has.

WHAT THE FINGERPRINT READS, and what it deliberately does not:
  - every line of CODE (comments stripped, whitespace collapsed): an equ, a
    %define, a macro's db/dw - the addresses, offsets, flags and the header
    layout;
  - an OSAPI_* %define's CONTRACT: the comment on its own line and every
    indented comment-only line under it. The register contract is written
    there and nowhere else a tool can read, and a contract that changes at the
    same address is the case the address alone cannot see;
  - NOT a comment that starts at column 0. That is the SDK's prose - section
    banners and essays - and making every sentence of it a lock decision would
    make the decision meaningless.
  - NOT the line that defines the guarded number itself, so the bump is the
    decision and not part of what it decides about.
"""
import argparse
import hashlib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "tools", "abi.lock.json")

GUARDS = {
    "PKG_FMT": {"defined": "apps/os88api.inc",
                "files": ["apps/os88api.inc"]},
    "DRV_VER": {"defined": "drivers/os88drv.inc",
                "files": ["apps/os88api.inc", "drivers/os88drv.inc"]},
}
OSAPI_DEF = re.compile(r"^%define\s+OSAPI_")


def surface(text, guard):
    """The lines of `text` that are ABI, normalised (see the module doc)."""
    out, in_contract = [], False
    own = re.compile(r"^%s\s+equ\b" % re.escape(guard))
    for raw in text.splitlines():
        stripped = raw.strip()
        if not stripped:
            in_contract = False
            continue
        if stripped.startswith(";"):
            # A comment-only line: part of an OSAPI contract when it is
            # INDENTED and follows one, prose when it starts at column 0.
            if in_contract and raw[:1].isspace():
                out.append("  ; " + " ".join(stripped[1:].split()))
            else:
                in_contract = False
            continue
        code, _, comment = raw.partition(";")
        code = " ".join(code.split())
        if own.match(code):
            in_contract = False
            continue
        if OSAPI_DEF.match(code):
            out.append(code + " ; " + " ".join(comment.split()))
            in_contract = True
        else:
            out.append(code)
            in_contract = False
    return out


def fingerprint(guard, texts=None):
    """sha256 over the guarded files' surfaces; `texts` overrides the disk."""
    h = hashlib.sha256()
    for rel in GUARDS[guard]["files"]:
        text = (texts or {}).get(rel)
        if text is None:
            text = open(os.path.join(ROOT, rel)).read()
        h.update(("== %s\n" % rel).encode())
        h.update("\n".join(surface(text, guard)).encode())
        h.update(b"\n")
    return h.hexdigest()


def version(guard):
    text = open(os.path.join(ROOT, GUARDS[guard]["defined"])).read()
    m = re.search(r"^%s\s+equ\s+(\d+)" % guard, text, re.M)
    if not m:
        raise SystemExit("abilock: no `%s equ N` in %s"
                         % (guard, GUARDS[guard]["defined"]))
    return int(m.group(1))


def load():
    try:
        return json.load(open(LOCK))
    except OSError:
        return {}


def check(lock):
    """[complaint] per guard whose lock no longer describes the tree."""
    bad = []
    for guard in GUARDS:
        have = lock.get(guard)
        ver, fp = version(guard), fingerprint(guard)
        if have is None:
            bad.append("%s is not in %s - run `python3 tools/abilock.py "
                       "--update \"why\"` to record it"
                       % (guard, os.path.relpath(LOCK, ROOT)))
            continue
        if ver != have["version"]:
            bad.append("%s is %d and the lock recorded %d. The bump is the "
                       "decision; record it: `python3 tools/abilock.py "
                       "--update \"why %s moved\"`" % (guard, ver,
                                                       have["version"], guard))
        elif fp != have["fingerprint"]:
            files = " and ".join(GUARDS[guard]["files"])
            bad.append(
                "the ABI surface %s guards (%s) changed and %s is still %d. "
                "DECIDE: if anything built before this change can no longer "
                "run on this kernel - a cell moved, a cell's registers or "
                "meaning changed, a header or record grew - bump %s, then "
                "record it with `python3 tools/abilock.py --update \"why\"`. "
                "If everything built before still runs - a cell added at the "
                "end, a contract clarified - record it without a bump, the "
                "same command. SPEC.md 20.2.0.1"
                % (guard, files, guard, ver, guard))
    return bad


def update(lock, reason):
    changed = []
    hist = lock.setdefault("history", [])
    for guard in GUARDS:
        ver, fp = version(guard), fingerprint(guard)
        have = lock.get(guard)
        if have and have["version"] == ver and have["fingerprint"] == fp:
            continue
        was = have["version"] if have else None
        lock[guard] = {"version": ver, "fingerprint": fp}
        hist.append({"guard": guard, "from": was, "to": ver,
                     "fingerprint": fp, "reason": reason})
        changed.append("%s %s -> %d" % (guard, was, ver))
    lock["_"] = ("SPEC.md 20.2.0.1. Written by tools/abilock.py --update; "
                 "checked by every `make` (tests/unit/t_abilock.py). Each "
                 "entry in `history` is a decision about whether a change to "
                 "the package or driver ABI breaks what was built before it.")
    with open(LOCK, "w") as f:
        json.dump(lock, f, indent=1, sort_keys=True)
        f.write("\n")
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--update", metavar="REASON",
                    help="record the surface as it stands, with the reason")
    a = ap.parse_args()
    lock = load()
    if a.update is not None:
        if not a.update.strip():
            sys.exit("abilock: --update needs a reason - it is what a "
                     "reviewer reads")
        changed = update(lock, a.update.strip())
        print("abilock: %s" % (", ".join(changed) if changed
                               else "nothing changed; the lock already "
                                    "describes the tree"))
        return 0
    bad = check(lock)
    for b in bad:
        print("abilock: FAIL: " + b)
    if not bad:
        print("abilock: %s" % ", ".join("%s %d" % (g, lock[g]["version"])
                                        for g in GUARDS))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
