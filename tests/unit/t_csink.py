#!/usr/bin/env python3
"""CLEAR SKIES' PARALLEL TABLES stay in step (SPEC.md 88.4.4, 88.6.5, 88.13.9.1).

    python3 tests/unit/t_csink.py

Three families of table in this package are indexed by something declared
somewhere else, and every one of them fails the same way: silently, on one
adapter or one setting, long after the row that was forgotten.

  - **the inks.** `CSI_NINK` rows in each of `cs_inkherc`, `cs_inkcga`,
    `cs_inkmodex`, `cs_inkc160`, `cs_dactab` and the three `cs_inkval_*`. A
    new ink added to some of them draws in whatever byte follows the table on
    the rest - and a new BACKEND arrives with two more tables to forget, which
    is how this row earned its keep on the merge it was written for.
  - **a hill's SHADES** (88.4.4.1): the hill face tables use CSI_HILL2 on
    their +-x sides and CSI_HILLTOP on the cap, and every ink from CSI_HILL2
    up is terrain - the shades, the haze (88.4.1.1) and Ultra's fields
    (88.13.1.5) - which is what cs_fillon's one `jae` counts it as.
  - **the river's LINE.** §88.6.5 gives a river reduced to its far model the
    river's own blue rather than the runway's white, so every `*_f` model
    whose full model is `CSI_RIVER` must carry `CSI_RIVLINE`. One left behind
    is a white river on a colour display and nothing at all to see on the
    others.
  - **an LOD pair's HEIGHT.** A `CSO_FAR` model stands in for its full model
    beyond `CSO_LOD`, so the two must be the same height or the object CHANGES
    SIZE as you fly toward it. The Eiffel's far model was 300 against the full
    model's 324 and popped 24 m at the switch; every other pair in every world
    already agreed, which is what makes this a rule rather than a preference.

  - **the settings.** `cs_set_tab` is one row a setting - where it lives,
    its ceiling, its best - so the three can no longer drift apart in length
    the way the three parallel tables it replaced could; what is left to
    hold is the row's SHAPE (a `dw` and two `db`s) and §88.13.9.1's trap,
    that `best` is NOT `max`: the Mode byte's ceiling is CGA, so a 286 handed
    the best of everything off the ceiling gets the worse of two displays.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, "apps", "skies")
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def equ(path, name):
    m = re.search(r"^%s\s+equ\s+(-?\d+)" % name, open(path).read(), re.M)
    if not m:
        raise SystemExit("t_csink: %s is not defined in %s" % (name, path))
    return int(m.group(1))


def rows(text, label, per, macro=None):
    """The rows of a table, as lists of ints; `per` bytes a row.

    `macro` names a one-row-a-line macro (`CS_C16 9`), which is how the
    160x100 backend writes its inks - a row there is one attribute in four
    identical bytes and spelling that out four times would be the mistake
    the macro exists to prevent.
    """
    lines = []
    for line in text.splitlines():
        if not lines:
            if not line.startswith(label + ":"):
                continue
            rest = line[len(label) + 1:].split(";")[0].strip()
            lines.append(rest)          # a table may open on its own line
            continue
        body = line.split(";")[0].strip()
        if body.startswith("db "):
            lines.append(body)
        elif macro and body.startswith(macro + " "):
            lines.append("db " + ", ".join([body[len(macro) + 1:].strip()] * per))
        elif body == "" or line.lstrip().startswith(";"):
            continue                    # a comment inside the table
        else:
            break
    if not lines:
        raise SystemExit("t_csink: %s is not a db table" % label)
    out = []
    for line in lines:
        body = line.split(";")[0].strip()
        if not body.startswith("db "):
            continue
        vals = [t.strip() for t in body[3:].split(",")]
        vals = [int(t, 0) if re.match(r"^-?(0x)?[0-9a-fA-F]+$", t) and
                (t.startswith("0x") or t.lstrip("-").isdigit()) else t
                for t in vals]
        for i in range(0, len(vals), per):
            out.append(vals[i:i + per])
    return out


def main():
    ras = open(os.path.join(APP, "csraster.inc")).read()
    asm = os.path.join(APP, "skies.asm")
    n = equ(asm, "CSI_NINK")
    for label, per, mac in (("cs_dactab", 3, None), ("cs_inkherc", 4, None),
                            ("cs_inkcga", 4, None), ("cs_inkmodex", 4, None),
                            ("cs_inkc160", 4, "CS_C16")):
        got = rows(ras, label, per, mac)
        check(len(got) == n,
              "%s has CSI_NINK = %d rows (%d)" % (label, n, len(got)))
    for label in ("cs_inkval_cga", "cs_inkval_modex", "cs_inkval_c160"):
        got = rows(ras, label, 1)
        check(len(got) == n,
              "%s has CSI_NINK = %d entries (%d)" % (label, n, len(got)))

    # --- A HILL IS TWO-TONE (88.4.4.1): every face table a hill model uses
    #     gives its +-x sides CSI_HILL2 and its cap CSI_HILLTOP, and no hill
    #     face is left in the one flat ink. The shades are TERRAIN to
    #     cs_fillon because they are the last inks there are, so they must be
    h2, ht = equ(asm, "CSI_HILL2"), equ(asm, "CSI_HILLTOP")
    tail = sorted((int(v), k) for k, v in re.findall(
        r"^(CSI_\w+)\s+equ\s+(\d+)", open(asm).read(), re.M)
        if k != "CSI_NINK" and int(v) >= h2)
    terrain = ("CSI_HILL2", "CSI_HILLTOP", "CSI_HZS2", "CSI_HZS1",
               "CSI_HZG1", "CSI_FIELD", "CSI_FIELD2", "CSI_GRALT",
               "CSI_HZGALT", "CSI_ROAD", "CSI_LIFT")
    check(ht == h2 + 1 and [k for _, k in tail] and
          all(k in terrain for _, k in tail) and tail[-1][0] == n - 1,
          "every ink from CSI_HILL2 (%d) up is TERRAIN - a hill's shade, the "
          "haze or a field - which is what cs_fillon's one `jae` counts it "
          "as (%s)" % (h2, ", ".join(k for _, k in tail)))
    hills = []
    for f in ["csvocab.inc"] + sorted(x for x in os.listdir(APP)
                                      if x.startswith("csw_")):
        s = open(os.path.join(APP, f)).read()
        for name, body in re.findall(r"^(cs_f_\w+):[^\n]*\n((?:\s+CS_\w+ .*\n)+)",
                                     s, re.M):
            if "CSI_HILL" in body:
                hills.append((f, name, body))
    flat = [n_ for f, n_, b in hills
            if re.search(r"CS_SIDES \d+, CSI_HILL, CSI_HILL\s", b)
            or re.search(r"CS_TOP \d+, CSI_HILL\s", b)]
    check(len(hills) >= 2 and not flat,
          "every hill face table is two-tone with a lit cap (%d tables%s)"
          % (len(hills), "; flat: %s" % flat if flat else ""))

    # --- the river's line, in every world -----------------------------------
    rl = equ(asm, "CSI_RIVLINE")
    check(rl == n - 1 or rl < n,
          "CSI_RIVLINE %d is inside CSI_NINK %d" % (rl, n))
    total, miss = 0, []
    for f in sorted(os.listdir(APP)):
        if not f.startswith("csw_") or not f.endswith(".inc"):
            continue
        s = open(os.path.join(APP, f)).read()
        wet = set(re.findall(r"^(cs_m_\w+): db CSM_FLAT,[^;\n]*CSI_RIVER",
                             s, re.M))
        for far, ink in re.findall(
                r"^(cs_m_\w+f): db CSM_FLAT,[^;\n]*?(CSI_\w+)", s, re.M):
            if far[:-1] not in wet:
                continue
            total += 1
            if ink != "CSI_RIVLINE":
                miss.append("%s:%s is %s" % (f, far, ink))
    check(total >= 20,
          "the worlds' river far models are found (%d of them)" % total)
    check(not miss,
          "...and every one of them draws in CSI_RIVLINE (%s)"
          % (miss if miss else "all %d" % total))

    # --- an LOD pair is the same HEIGHT ------------------------------------
    def levels(body):
        out = []
        for line in body.splitlines():
            b = line.split(";")[0].strip()
            b = re.sub(r"^\.\w+:\s*", "", b)     # a `.v:` label opens the list
            g = re.match(r"^dw\s+(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*$", b)
            if g:
                out.append(tuple(int(x) for x in g.groups()))
        return out

    pairs, popped = 0, []
    for f in sorted(os.listdir(APP)):
        if not f.startswith("csw_") or not f.endswith(".inc"):
            continue
        src = open(os.path.join(APP, f)).read()
        mods = {}
        for g in re.finditer(r"^(cs_m_\w+): db (CSM_\w+), *(\d+),", src, re.M):
            i = g.end()
            j = src.find("\ncs_m_", i)
            mods[g.group(1)] = (g.group(2),
                                levels(src[i:j if j > 0 else len(src)]))
        for g in re.finditer(r"CS_OBJ\s+(cs_m_\w+),\s*(cs_m_\w+f)\b", src):
            full, far = g.group(1), g.group(2)
            if full not in mods or far not in mods:
                continue
            if mods[full][0] != "CSM_STACK":
                continue
            pairs += 1
            hf = max((v[1] for v in mods[full][1]), default=0)
            hr = max((v[1] for v in mods[far][1]), default=0)
            if hf != hr:
                popped.append("%s:%s %d against %s %d" % (f, far, hr, full, hf))
    check(pairs >= 5, "the worlds' STACK LOD pairs are found (%d)" % pairs)
    check(not popped,
          "...and a far model is the same HEIGHT as the model it stands in "
          "for, so nothing changes size at the switch (%s)"
          % (popped if popped else "all %d" % pairs))

    # --- the settings' three rows -------------------------------------------
    st = open(os.path.join(APP, "csset.inc")).read()
    tab = st[st.index("cs_set_tab:"):st.index("cs_set_end:")]
    tab = re.sub(r";[^\n]*", "", tab.replace("cs_set_tab:", ""))
    lines = [l.split(None, 1) for l in tab.splitlines() if l.strip()]
    sets = []
    for i in range(0, len(lines) - 1, 2):
        if lines[i][0] == "dw" and lines[i + 1][0] == "db":
            sets.append((lines[i][1].strip(),
                         [v.strip() for v in lines[i + 1][1].split(",")]))
    check(len(lines) == 2 * len(sets) and len(sets) == 5
          and all(len(v) == 2 for _, v in sets),
          "cs_set_tab is five rows of `dw where` + `db ceiling, best` "
          "(%d lines, %d rows)" % (len(lines), len(sets)))
    mode = [v for w, v in sets if w == "cs_modepref"]
    check(mode and mode[0] == ["1", "0"],
          "...and the Mode row's BEST is 0 where its ceiling is 1 - Mode X "
          "and not CGA for a 286 with no file (%s)" % mode)
    print("  %s" % ("ok" if not bad else "FAILED: %d" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
