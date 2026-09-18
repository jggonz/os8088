#!/usr/bin/env python3
"""Is the CHEAP what-if right?  (docs/plans/REGION-SELF-COMPACT-PLAN.md 5.1)

    python3 tools/heapwhatif.py

The question is whether a package can be told "how much would you have if you
moved too?" WITHOUT the kernel carrying an exact combined plan - which is
~150-250 resident bytes against ~50 for three plans and a subtraction:

    what-if  =  A  +  (D_free - D_pinned)

A being the ascending plan (plain mem_avail's own answer), D_pinned the
descending plan as things stand, and D_free the same with the caller's region
treated as frameless.  All three are arithmetic over 32 records; nothing moves.

It is checked against the TRUE both-passes figure - the descending pass RUN for
real, barriers and all, then the floor planned against the result - over the
layout tests/heapmap read off a running machine and six built to break it.
The reference is tools/heapmap.py's own model, so this asserts the formula and
not the model.

WHY IT IS A SCRIPT AND NOT A ROW: nothing here runs the kernel.  It is the
arithmetic behind a design decision, kept runnable so the decision can be
re-taken against a layout somebody adds rather than re-argued.  The row that
would assert it on the machine is that plan's 7.2, and it does not exist yet.

UNDER-REPORTING IS THE SAFE DIRECTION and the whole reason the approximation
is affordable: a what-if that reads low makes a package skip a post that would
have helped, which is exactly where it stands with no flag at all.  One that
read HIGH would talk it into a compaction that disturbs every other holder's
claims for less than advertised.
"""
import copy, os, sys
# THIS TREE'S root, DERIVED - never a hard-coded path. A literal is right in the
# checkout it was written in and wrong in a git worktree, which is how parallel
# work is done here: os88sym re-assembles ROOT/kernel/kernel.asm and compares it
# against ROOT/build/kernel.bin, so a literal ROOT answers about a DIFFERENT
# kernel from the image being booted.
_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
import heapmap

PARA = 64                                   # paragraphs per KB


class C(object):
    def __init__(self, seg, kb, hi, movable, purge=False):
        self.seg, self.para, self.hi = seg, kb * PARA, hi
        self.rloc = 160 if movable else 0
        self.purgeable, self.own, self.dma = purge, 3, 0
    @property
    def end(self): return self.seg + self.para
    @property
    def pinned(self): return self.rloc == 0


class M(object):
    def __init__(self, base, top, claims):
        self.base, self.top = base, top
        self.claims = sorted(claims, key=lambda c: c.seg)
    runs = heapmap.Map.runs
    compacted = heapmap.Map.compacted


def true_combined(m):
    """Both passes for real: RUN the descending pass - mirroring
    heapmap.compacted(up=True)'s own walk, barriers and all - then plan the
    floor against the result."""
    s = copy.deepcopy(m)
    at = s.top
    for c in reversed(s.claims):
        if c.purgeable:
            continue
        if c.pinned or not c.hi:
            at = c.seg                      # a barrier: the fill point resumes
        else:
            at -= c.para
            c.seg = at                      # ...and it MOVES there
    s.claims.sort(key=lambda c: c.seg)
    return max((p for _, p in s.compacted(up=False)), default=0) / PARA


def cheap(m, me):
    """A + (D_free - D_pinned), three PLANS and no move."""
    A = max((p for _, p in m.compacted(up=False)), default=0) / PARA
    dp = max((p for _, p in m.compacted(up=True)), default=0) / PARA
    f = copy.deepcopy(m)
    for c in f.claims:
        if c.seg == me:
            c.rloc = 160                    # "pretend I am frameless"
    df = max((p for _, p in f.compacted(up=True)), default=0) / PARA
    return A + (df - dp), A


def after_the_pass(m, me):
    """What PLAIN mem_avail reads once the posted compaction has actually run -
    the ascending plan over a heap that is already packed BOTH ways.  No
    what-if, no combined plan, no new kernel arithmetic at all."""
    s = copy.deepcopy(m)
    for c in s.claims:
        if c.seg == me:
            c.rloc = 160                    # the service point: I am frameless
    at = s.top                              # ...the descending pass, RUN
    for c in reversed(s.claims):
        if c.purgeable:
            continue
        if c.pinned or not c.hi:
            at = c.seg
        else:
            at -= c.para
            c.seg = at
    s.claims.sort(key=lambda c: c.seg)
    at = s.base                             # ...and the ascending pass, RUN
    for c in list(s.claims):
        if c.purgeable:
            continue
        if c.pinned or c.hi:
            at = c.end
        else:
            c.seg = at
            at = c.end
    s.claims.sort(key=lambda c: c.seg)
    # ...then PLAIN mem_avail: the ascending PLAN over the packed heap.
    return max((p for _, p in s.compacted(up=False)), default=0) / PARA


def exact(m, me):
    """THE COMBINED PLAN: one walk, TWO fill points, each claim routed by its
    own door.  Nothing moves - which is what makes two fill points sound here
    and unsound in mem_cp_run (a top-down claim packed up inside an ascending
    walk writes onto claims the walk has not visited yet).

    The floor fill rises from [mem_base] over the bottom-up claims; the ceiling
    fill falls from [mem_top] over the top-down ones; a PINNED claim of either
    door stays where it is, splits a run off and moves that fill past it.  The
    answer is the largest of the barrier gaps and the middle."""
    runs, lo, hi = [], m.base, m.top
    live = [c for c in m.claims if not c.purgeable]
    def movable(c):
        return (not c.pinned) or c.seg == me     # the what-if excuses ME
    for c in sorted([c for c in live if not c.hi], key=lambda c: c.seg):
        if movable(c):
            lo += c.para
        else:
            if c.seg > lo:
                runs.append(c.seg - lo)
            lo = c.end
    for c in sorted([c for c in live if c.hi], key=lambda c: -c.seg):
        if movable(c):
            hi -= c.para
        else:
            if c.end < hi:
                runs.append(hi - c.end)
            hi = c.seg
    if hi > lo:
        runs.append(hi - lo)
    return max(runs, default=0) / PARA


def exact2(m, me):
    """...and the same thing IN THE ORDER THE PASSES RUN, which is what makes
    it right where `exact` above is 12-20K high: the two stacks can WALL EACH
    OTHER IN.  A bottom-up claim sitting above a top-down one is a barrier to
    the descending pass, so the claim below it cannot reach the ceiling - and
    once the descending pass has left it there, it is a barrier to the
    ascending pass in turn.  Two independent sweeps cannot see that; two
    sweeps IN ORDER can.

    The kernel cannot mutate mem_tab inside a PLAN.  Its first spelling of
    the second sweep asked "where will this top-down claim be?" per claim -
    two O(n) scans each, B - S looked up ahead; mem_cp_both now reaches the
    same B - S by DEFERRING it (SPEC.md 66.4.3.1): a ceiling mover adds its
    size to the fill point and raises a flag, and the next floor mover or
    pinned claim is where the run stops and the hole is measured.  Modelled
    here with a position map in the look-ahead form on purpose, so the two
    spellings check each other - it is the same arithmetic either way."""
    live = [c for c in m.claims if not c.purgeable]
    def movable(c):
        return (not c.pinned) or c.seg == me
    def ceilmover(c):
        return movable(c) and c.hi        # what the DESCENDING pass may move

    # NO SCRATCH: a ceiling mover's post-descending base is
    #     B - S
    # where B is the base of the lowest claim above it that the descending
    # pass may NOT move (mem_top if there is none) and S the paragraphs of
    # every ceiling mover from it up to B.  Two O(n) scans, so the whole plan
    # stays O(MEM_MAX^2) like every other walk here and needs no table.
    def newbase(c):
        B = m.top
        for d in live:
            if d.seg > c.seg and not ceilmover(d) and d.seg < B:
                B = d.seg
        S = sum(d.para for d in live
                if ceilmover(d) and c.seg <= d.seg < B)
        return B - S

    pos = {}
    for c in live:
        pos[id(c)] = newbase(c) if ceilmover(c) else c.seg
    runs, lo = [], m.base
    for c in sorted(live, key=lambda c: pos[id(c)]):    # ...then the ascending
        b = pos[id(c)]
        if movable(c) and not c.hi:
            lo += c.para
        else:
            if b > lo:
                runs.append(b - lo)
            lo = b + c.para
    if m.top > lo:
        runs.append(m.top - lo)
    return max(runs, default=0) / PARA


def case(name, base, top, claims, me):
    m = M(base, top, claims)
    free = copy.deepcopy(m)                 # what-if: my region movable
    for c in free.claims:
        if c.seg == me:
            c.rloc = 160
    want = true_combined(free)
    got, plain = cheap(m, me)
    woke = after_the_pass(m, me)
    ex = exact2(m, me)
    # ...and what PLAIN mem_avail should now report: the combined figure with
    # the caller's region still PINNED, which is what mem_claim can deliver
    # since the both-passes fix. Today it reports `plain` - the ascending
    # plan alone.
    owed = exact2(m, None)
    err = got - want
    verdict = "ok " if abs(err) < 0.01 else ("OVER" if err > 0 else "UNDER")
    eerr = ex - want
    print("  %-35s mem_avail %6.1f (owes %6.1f%s) | what-if %6.1f %-7s | wake %6.1f %s"
          % (name, plain, owed,
             "" if abs(owed - plain) < 0.01 else "  SHORT %+.0f" % (plain - owed),
             ex,
             "ok" if abs(eerr) < 0.01 else ("OVER%+.0f" % eerr if eerr > 0
                                            else "UNDER%+.0f" % eerr),
             woke, "ok" if abs(woke - want) < 0.01 else "WRONG"))


B, T = 0x1B20, 0xA000                       # the measured machine's arena
# the measured layout, CALC closed: 8K hole above a 20K movable region
base_lo = [C(0x1B20, 3, False, True), C(0x1D20, 3, False, True),
           C(0x2FC0, 3, False, True)]
case("measured (8K hole above me)", B, T,
     base_lo + [C(0x9900, 20, True, False)], 0x9900)
case("no hole above me", B, T,
     base_lo + [C(0x9900, 20, True, False), C(0x9E00, 8, True, False)], 0x9900)
case("hole above, another mover below me", B, T,
     base_lo + [C(0x9400, 20, True, True), C(0x9900, 20, True, False)], 0x9900)
case("PINNED driver between me and roof", B, T,
     base_lo + [C(0x9900, 20, True, False), C(0x9E00, 4, True, False),
                C(0x9F00, 4, True, False)], 0x9900)
case("me at the very ceiling", B, T,
     base_lo + [C(0x9B00, 20, True, False)], 0x9B00)
case("two holes, one above one below me", B, T,
     base_lo + [C(0x9000, 12, True, True), C(0x9900, 20, True, False)], 0x9900)
# --- the reported layout: free at the top, me, another package under me ----
case("[free][me][another pkg], adjacent", B, T,
     base_lo + [C(0x9400, 20, True, True), C(0x9900, 20, True, False)], 0x9900)
case("[free][me][another], another PINNED", B, T,
     base_lo + [C(0x9400, 20, True, False), C(0x9900, 20, True, False)], 0x9900)
case("[free][me][gap][another movable]", B, T,
     base_lo + [C(0x9000, 20, True, True), C(0x9900, 20, True, False)], 0x9900)
case("[free][me][gap][gap][2 movable]", B, T,
     base_lo + [C(0x8800, 8, True, True), C(0x9000, 12, True, True),
                C(0x9900, 20, True, False)], 0x9900)
case("INTERLEAVED: a lo claim above a hi one", B, T,
     base_lo + [C(0x9000, 20, True, True), C(0x9600, 8, False, True),
                C(0x9900, 20, True, False)], 0x9900)
case("INTERLEAVED, the lo claim PINNED", B, T,
     base_lo + [C(0x9000, 20, True, True), C(0x9600, 8, False, False),
                C(0x9900, 20, True, False)], 0x9900)
case("big floor barrier (pinned cache)", B, T,
     [C(0x1B20, 3, False, True), C(0x4000, 40, False, False),
      C(0x2FC0, 3, False, True)] + [C(0x9900, 20, True, False)], 0x9900)
