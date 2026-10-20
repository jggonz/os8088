#!/usr/bin/env python3
"""WATCH A GUEST'S INTERRUPT TRAFFIC FROM THE HOST, COSTING IT NOTHING.

    python3 tools/os88intmon.py watch 127.0.0.1:9001 --int 21 --int 13 --for 30
    python3 tools/os88intmon.py watch <addr> --int 13 --time --out td3.json
    python3 tools/os88intmon.py report td3.json
    python3 tools/os88intmon.py diff ours.json dos.json
    python3 tools/os88intmon.py --selfcheck          # no emulator

**IT EXISTS BECAUSE THE OTHER TRACER LIVES IN THE GUEST.**
`tools/os88dosdbg.py` is the instrument for *why a program behaves
differently* under this box, and its method - align two traces on
(function, CALL SITE) - is the thing that finds defects nothing else finds.
What it costs is RAM on both sides: our arm carries a ring in a part of
`DOS.O88`, and the real-DOS arm carries a TSR. For a program that wants the
machine - TD3 is 137,845 bytes of `.EXE` before it loads a scene - that ring
is the difference between running and not, and a tracer you cannot switch on
is not an instrument.

This one lives entirely in the debugger. The guest is not modified, not
rebuilt, and does not know it is being watched: MartyPC stops on the `INT`
INSTRUCTION, the host reads the register file, and the machine runs on.
Nothing is installed, so the same binary is measured whether or not anybody
is looking - which also means a run can be started against a machine that is
ALREADY going, including one a person is driving by hand.

  WHY THE BREAKPOINT IS `int` AND NOT THE HANDLER

An exec breakpoint on the handler needs the handler's address, which means
reading the IVT, which differs between this box and IBM DOS and moves when a
TSR hooks it. An `{"type": "int"}` breakpoint fires at the `INT` instruction
itself, so:

  - it works on BOTH machines with no configuration, which is what makes the
    two runs comparable - the whole point of os88dosdbg's method, kept;
  - `regs()` at the stop is the CALLER's register file, already. There is no
    stack frame to decode and no window in which the handler has changed
    anything;
  - `CS:IP` is the call SITE, which is os88dosdbg's alignment key. A trace
    from here can be aligned against one from there.

  WHAT IT COSTS, AND THE ONE THING TO KNOW BEFORE TRUSTING A NUMBER

**The GUEST pays nothing and the HOST pays a round trip per call.** Counts,
call sites and guest CYCLE stamps are exact at any speed: they are read out
of the machine rather than timed by it. What is NOT exact is how long the run
takes on your desk, and that is the trap - a trace of a chatty vector can be
slower than the program, so `--for` is a budget in GUEST seconds and a run
that overruns says so rather than quietly reporting a partial phase as a
whole one.

`--time` adds a second breakpoint at each call's return address and gives
exact in-call cycles. It doubles the stops, and it is the only way to say
`the ROM had the machine for N seconds`, which is the number a disk argument
turns on. Without it a call's cost is bounded by the NEXT call's stamp, which
is an upper bound and is labelled as one.
"""

import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

GUEST_HZ = 4772727.0                    # the 8088 this project is calibrated to

# **THE POLL IS THE WHOLE HOST COST.** Every stop is a few round trips and one
# sleep, and at wait_stop's own 20ms default the sleep is nearly all of it -
# measured, 26.6 stops a second, which makes a thousand-call program forty
# host seconds of waiting for a machine that answered immediately. A watch
# stops CONSTANTLY by construction, so it wants the tight end.
POLL = 0.001


# --- what the numbers MEAN -------------------------------------------------
# Only the functions a disk or a load argument turns on are named. An unnamed
# one prints as its number, which is honest: a table that guesses is a table
# that mislabels the call you are actually chasing.
DOS_FN = {
    0x09: "print string", 0x0E: "select drive", 0x19: "current drive",
    0x1A: "set DTA", 0x25: "set vector", 0x2A: "get date", 0x2C: "get time",
    0x30: "version", 0x35: "get vector", 0x36: "free space",
    0x39: "mkdir", 0x3A: "rmdir", 0x3B: "chdir",
    0x3C: "create", 0x3D: "open", 0x3E: "close", 0x3F: "READ",
    0x40: "WRITE", 0x41: "delete", 0x42: "SEEK", 0x43: "attributes",
    0x44: "ioctl", 0x47: "current dir", 0x48: "alloc", 0x49: "free",
    0x4A: "resize", 0x4B: "exec", 0x4C: "exit", 0x4E: "find first",
    0x4F: "find next", 0x56: "rename", 0x57: "file date",
}
BIOS_FN = {
    0x00: "reset", 0x01: "status", 0x02: "READ sectors",
    0x03: "write sectors", 0x04: "verify", 0x05: "format",
    0x08: "drive params", 0x15: "disk type", 0x16: "change line",
}
FN_TABLE = {0x21: DOS_FN, 0x13: BIOS_FN}

# **THE CALLS THAT NAME A FILE**, and reading the name is the difference
# between "it opened something" and "it opened A:\TD3.CFG when it was launched
# from B:". DS:DX on every one of them; AH=56h's second name is at ES:DI and
# is read too, because a rename that moves is the interesting rename.
DOS_NAMED = {0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x41, 0x43, 0x4B, 0x4E, 0x56, 0x5B}


def _asciz(m, seg, off, n=64):
    """An ASCIZ out of the guest, or None.

    A name is read AT THE STOP, before the handler has run - so it is what the
    program asked for rather than anything we made of it. Bounded and
    forgiving: a bad pointer must not end a trace.
    """
    try:
        b = m.read(((seg << 4) + off) & 0xFFFFF, n)
    except Exception:
        return None
    out = bytearray()
    for c in b:
        if c == 0:
            break
        if c < 32 or c > 126:
            return None                 # not a name: do not invent one
        out.append(c)
    return out.decode("ascii") if out else None


def fname(vec, ah):
    return FN_TABLE.get(vec, {}).get(ah, "%02Xh" % ah)


def chs_of(r):
    """(cylinder, head, sector) out of an int 13h call's registers.

    CH is the low eight bits of the cylinder and CL carries the sector in
    0-5 with the cylinder's top two in 6-7 - the encoding every BIOS disk
    call has used since 1981. It is here because WHICH sectors a run reads
    is the whole difference between `it went to the disk a lot` and `it read
    the same three sectors ninety times`.
    """
    cx, dx = r["cx"], r["dx"]
    sec = cx & 0x3F
    cyl = ((cx >> 8) & 0xFF) | ((cx & 0xC0) << 2)
    return cyl, (dx >> 8) & 0xFF, sec


def drive_of(vec, r):
    """Which drive a call names, when it names one - else None.

    int 13h puts the unit in DL and 0x80 is the first fixed disk; int 21h's
    drive is a LETTER inside a name, which this cannot see, so the DOS arm
    answers only for the calls that take a number.
    """
    if vec == 0x13:
        dl = r["dx"] & 0xFF
        return ("hd%d" % (dl & 0x7F)) if dl & 0x80 else chr(ord("A") + dl) + ":"
    if vec == 0x21:
        ah = (r["ax"] >> 8) & 0xFF
        if ah in (0x0E, 0x36):          # select drive / free space: DL, 0 = A:
            return chr(ord("A") + (r["dx"] & 0xFF)) + ":"
        if ah == 0x19:
            return None                 # it ANSWERS one rather than naming it
    return None


# ===========================================================================
# THE WATCH
# ===========================================================================
def watch(m, vectors, budget, do_time=False, out=None, say=print, until=None,
          quiet=None):
    """Stop on every `INT n` in `vectors` until the guest has run `budget`
    seconds of its OWN time.  Returns the record list.

    `until` is an optional predicate on the record just taken - a phase ends
    when it answers True, which is how a load is measured rather than a
    stopwatch-shaped guess at one.

    `quiet` is the other way a phase ends and the one a LOAD wants: stop
    once the guest has run this many of its own seconds making no call at
    all. A program that has finished loading and is waiting for a key makes
    none, so the silence IS the end of the load - and it is the same end on
    two different operating systems, which a fixed budget is not. It has to
    be the guest's clock rather than the host's: a loaded box makes a host
    second mean less work, so a host-timed silence would end the phase
    early under exactly the conditions that make a comparison interesting.
    """
    import os88marty

    base = [{"type": "int", "addr": v} for v in vectors]
    m.breakpoints(base)
    c0 = int(m.status().get("cycles", 0))
    recs = []
    host0 = time.time()
    stalls = 0

    # A silence is only detectable at the grain the wait comes back at, so a
    # run watching for one polls oftener. 20 guest seconds is the plain wait.
    wlim = 20.0 if quiet is None else max(0.2, quiet / 3.0)

    while True:
        m.run()
        if not m.wait_stop(limit=wlim, poll=POLL):
            # **A PROGRAM WAITING FOR A KEY MAKES NO CALLS**, and that is not a
            # machine that died - it is the commonest thing a game does. So a
            # timeout asks the CLOCK before it gives up: the budget is guest
            # seconds and it is still being spent.
            spent = (int(m.status().get("cycles", 0)) - c0) / GUEST_HZ
            if quiet is not None and recs and \
                    spent - recs[-1]["cyc"] / GUEST_HZ >= quiet:
                say("intmon: %.1f guest seconds with no call - the phase "
                    "ended. %d call(s) over %.2f guest s"
                    % (spent - recs[-1]["cyc"] / GUEST_HZ, len(recs),
                       recs[-1]["cyc"] / GUEST_HZ))
                break
            if spent > budget:
                say("intmon: %.1f guest seconds spent with the guest idle - "
                    "the budget. %d call(s)" % (spent, len(recs)))
                break
            stalls += 1
            if stalls > (8 if quiet is None else 8 * max(1, int(3.0 / wlim))):
                say("intmon: no call in %d waits and %.1f guest seconds - the "
                    "guest is stopped, not quiet" % (stalls, spent))
                break
            continue
        stalls = 0
        st = m.status()                 # ONE status a call: the budget and the
        cyc = int(st.get("cycles", 0)) - c0     # stamp are the same reading
        if cyc / GUEST_HZ > budget:
            say("intmon: %.1f guest seconds spent - the budget. %d call(s)"
                % (cyc / GUEST_HZ, len(recs)))
            break
        r = m.regs()
        vec = _which(m, r, vectors)
        rec = {
            "v": vec, "cyc": cyc,
            "ax": r["ax"], "bx": r["bx"], "cx": r["cx"], "dx": r["dx"],
            "si": r["si"], "di": r["di"], "ds": r["ds"], "es": r["es"],
            "cs": r["cs"], "ip": r["ip"],
        }
        # **THE CALL SITE, WHICH IS THE ALIGNMENT KEY** (docs/DOS-DEBUGGING.md).
        # `cs:ip` above is the HANDLER's - MartyPC's INT breakpoint stops with
        # the vector already fetched - and it is the same address for every
        # call, so it aligns nothing. What two runs of one program share is
        # where the PROGRAM called from, and that is the frame the CPU pushed:
        # IP, CS at SS:SP. Quoted as `CS - PSP : IP` it is the same number on
        # two machines that loaded the program at different paragraphs, which
        # is what turns *"they diverge somewhere"* into *"they diverge at this
        # instruction"*. Four bytes a call, and it is what the DOSTRAP ring on
        # the other side has recorded all along.
        if r.get("ss") is not None and r.get("sp") is not None:
            fr = m.read(((r["ss"] << 4) + r["sp"]) & 0xFFFFF, 4)
            rec["c_ip"] = fr[1] << 8 | fr[0]
            rec["c_cs"] = fr[3] << 8 | fr[2]
        if vec == 0x21 and ((r["ax"] >> 8) & 0xFF) in DOS_NAMED:
            nm = _asciz(m, r["ds"], r["dx"])
            if nm:
                rec["name"] = nm
            if ((r["ax"] >> 8) & 0xFF) == 0x56:
                nm2 = _asciz(m, r["es"], r["di"])
                if nm2:
                    rec["name2"] = nm2
        if do_time:
            rec["in"] = _time_one(m, base, r, rec)
            c0 += 0                     # the return stop costs the guest nothing
        recs.append(rec)
        if until is not None and until(rec):
            say("intmon: the phase ended at call %d" % len(recs))
            break

    m.breakpoints([])
    say("intmon: %d call(s) in %.1f host seconds (%.1f/s)"
        % (len(recs), time.time() - host0,
           len(recs) / max(1e-6, time.time() - host0)))
    if out:
        json.dump({"vectors": vectors, "timed": bool(do_time), "recs": recs},
                  open(out, "w"))
        say("intmon: %s" % out)
    return recs


def _which(m, r, vectors):
    """WHICH vector stopped us, read out of the instruction itself.

    The breakpoint set does not say, and a run watching two vectors that
    guessed would mislabel every call of one of them. CS:IP is AT the `int`,
    whose encoding is CD nn - so the byte after the opcode is the number.
    Only when it is one we asked for: a stop somewhere else is reported as 0
    rather than as a plausible wrong vector.
    """
    if len(vectors) == 1:
        return vectors[0]
    try:
        b = m.read(((r["cs"] << 4) + r["ip"]) & 0xFFFFF, 2)
        if b and b[0] == 0xCD and b[1] in vectors:
            return b[1]
    except Exception:
        pass
    return 0


def _time_one(m, base, r, rec=None):
    """Guest cycles from this `INT` to the instruction after it - AND THE ANSWER.

    The return site is CS:IP+2 - `INT nn` is two bytes - which is an ordinary
    exec breakpoint. NESTING is why the base set stays armed: a handler that
    itself calls a watched vector stops again first, and those stops are run
    past rather than counted, because the record for them was already taken
    at their own entry.

    **IT BANKS THE RETURN REGISTERS, and that is most of why it is worth
    paying for** (SPEC.md 96.22). An entry-only trace says what a program
    ASKED; the failures this family keeps producing are ones where the answer
    was wrong and the program believed it - a file classified as a character
    device, a search that never ends, a read reported short. Prince of Persia
    has been lost to that shape five times, and every one of them is invisible
    in the registers going in. The stop is already being made for the cycle
    count, so the answer costs one `regs` call.
    """
    # **THE RETURN SITE IS IN THE FRAME, NOT AT CS:IP+2** - and getting that
    # wrong is why every `--time` figure this tool ever printed was 20 cycles.
    # MartyPC's INT breakpoint stops INSIDE the handler, with the vector
    # already fetched: `cs:ip` is the handler's entry (the DOS box's own, at
    # 9060:05C9 windowed and 0060:05C9 under kern_dos - the same IP for every
    # call, which is the tell), so `+2` is an address two bytes into the
    # handler and the guest reaches it at once. What the program will come
    # back to is the three words the CPU pushed: IP, CS, FLAGS at SS:SP.
    st_ss, st_sp = r.get("ss"), r.get("sp")
    if st_ss is None or st_sp is None:
        return 0
    fr = m.read(((st_ss << 4) + st_sp) & 0xFFFFF, 4)
    ret = (((fr[3] << 8 | fr[2]) << 4) + (fr[1] << 8 | fr[0])) & 0xFFFFF
    c0 = int(m.status().get("cycles", 0))
    m.breakpoints(base + [{"type": "exec", "addr": ret}])
    rr = None
    for _ in range(64):                 # bounded: a call that never returns
        m.run()                         # must not hang the whole run
        if not m.wait_stop(limit=30.0):
            break
        rr = m.regs()
        if ((rr["cs"] << 4) + rr["ip"]) & 0xFFFFF == ret:
            break
    cyc = int(m.status().get("cycles", 0)) - c0
    m.breakpoints(base)
    if rec is not None and rr is not None:
        for k in ("ax", "bx", "cx", "dx", "si", "di", "ds", "es", "flags"):
            if k in rr:
                rec["r_" + k] = rr[k]
    return cyc


# ===========================================================================
# THE REPORT
# ===========================================================================
def report(data, say=print, top=12):
    recs = data["recs"]
    vectors = data.get("vectors", [])
    timed = data.get("timed")
    if not recs:
        say("intmon: no calls recorded")
        return
    span = (recs[-1]["cyc"] - recs[0]["cyc"]) / GUEST_HZ
    say("")
    say("  %d calls over %.2f guest seconds on int %s"
        % (len(recs), span, ", ".join("%02Xh" % v for v in vectors)))
    say("")

    for vec in vectors:
        mine = [r for r in recs if r["v"] == vec]
        if not mine:
            continue
        by = {}
        for r in mine:
            ah = (r["ax"] >> 8) & 0xFF
            e = by.setdefault(ah, {"n": 0, "cyc": 0, "drives": {}, "sec": 0})
            e["n"] += 1
            e["cyc"] += r.get("in", 0)
            d = drive_of(vec, r)
            if d:
                e["drives"][d] = e["drives"].get(d, 0) + 1
            if vec == 0x13 and ah in (0x02, 0x03):
                e["sec"] += r["ax"] & 0xFF
        say("  int %02Xh - %d call(s)" % (vec, len(mine)))
        head = "    %-6s %-16s %7s" % ("fn", "what", "calls")
        if timed:
            head += " %10s %8s" % ("guest ms", "share")
        head += "  %s" % ("sectors / drives")
        say(head)
        tot = sum(e["cyc"] for e in by.values()) or 1
        rows = sorted(by.items(), key=lambda kv: -(kv[1]["cyc"] or kv[1]["n"]))
        for ah, e in rows[:top]:
            line = "    %-6s %-16s %7d" % ("%02Xh" % ah, fname(vec, ah), e["n"])
            if timed:
                line += " %10.1f %7.1f%%" % (e["cyc"] / GUEST_HZ * 1000.0,
                                             100.0 * e["cyc"] / tot)
            extra = []
            if e["sec"]:
                extra.append("%d sectors" % e["sec"])
            if e["drives"]:
                extra.append(" ".join("%s x%d" % (k, v) for k, v in
                                      sorted(e["drives"].items())))
            line += "  " + ", ".join(extra)
            say(line)
        if len(rows) > top:
            say("    ...and %d more function(s)" % (len(rows) - top))
        # **WHICH SECTORS, and how often the same ones** - the difference
        # between a run that reads a lot and a run that reads one thing over
        # and over. Only for the transfer functions: AH=00h has no geometry.
        if vec == 0x13:
            spots = {}
            for r in mine:
                ah = (r["ax"] >> 8) & 0xFF
                if ah not in (0x02, 0x03, 0x04):
                    continue
                d = drive_of(vec, r)
                spots.setdefault(d, {})
                key = chs_of(r)
                spots[d][key] = spots[d].get(key, 0) + 1
            for d in sorted(spots):
                hits = spots[d]
                rpt = sorted(((n, k) for k, n in hits.items()), reverse=True)
                say("")
                say("    %s: %d transfer(s) over %d distinct place(s)"
                    % (d, sum(hits.values()), len(hits)))
                for n, (c, h, sx) in rpt[:6]:
                    say("      c%-3d h%d s%-3d  x%d" % (c, h, sx, n))
                if len(rpt) > 6:
                    say("      ...and %d more" % (len(rpt) - 6))

            # ...and the ALTERNATION, which is what a person watching the
            # drive lights actually sees. A run that goes A B A B is not the
            # same finding as one that reads all of A and then all of B.
            seq = [drive_of(vec, r) for r in mine
                   if ((r["ax"] >> 8) & 0xFF) in (0x02, 0x03, 0x04)]
            flips = sum(1 for a, b in zip(seq, seq[1:]) if a != b)
            if seq:
                say("")
                say("    the order: %s%s"
                    % (" ".join(x[0] for x in seq[:40]),
                       " ..." if len(seq) > 40 else ""))
                say("    %d transfer(s), %d change(s) of drive%s"
                    % (len(seq), flips,
                       " - it is ALTERNATING" if flips > len(seq) * 0.4 else ""))

            # **THE PACE**, which IS the speed question. "Slow" has three
            # separate causes and one number cannot tell them apart, so
            # there are three: how much a call MOVES (whether the OS
            # batches), what a call COSTS inside the BIOS (the drive, and
            # the same on both sides of a comparison), and what is spent
            # BETWEEN calls (the OS's own arithmetic and the program's).
            # The third is the only one an operating system can be blamed
            # for, and without the first two it cannot be seen at all.
            xf = [r for r in mine
                  if ((r["ax"] >> 8) & 0xFF) in (0x02, 0x03, 0x04)]
            sect = sum((r["ax"] & 0xFF) for r in xf
                       if ((r["ax"] >> 8) & 0xFF) in (0x02, 0x03))
            if xf:
                # First call to LAST RETURN when the run is timed - a span
                # that stopped at the last call's `int` would exclude that
                # call's own time and make the two halves below fail to add
                # up, which on a short run reads as more BIOS than wall.
                wall = (mine[-1]["cyc"] + (mine[-1].get("in", 0) if timed else 0)
                        - mine[0]["cyc"]) / GUEST_HZ
                gaps = sorted(b["cyc"] - a["cyc"]
                              for a, b in zip(mine, mine[1:]))
                med = (gaps[len(gaps) // 2] / GUEST_HZ * 1000.0) if gaps else 0.0
                say("")
                say("    the pace")
                say("      %d transfer(s), %d sector(s), %.1f sector(s) a call"
                    % (len(xf), sect, sect / float(len(xf))))
                say("      %.2f guest s first call to %s, %.1f ms between "
                    "calls (median)"
                    % (wall, "last return" if timed else "last call", med))
                if sect:
                    say("      %.1f guest ms a sector, over the whole wall"
                        % (wall * 1000.0 / sect))
                if timed:
                    inc = sum(r.get("in", 0) for r in xf) / GUEST_HZ
                    say("      %.2f guest s INSIDE the BIOS - %.0f%% of the "
                        "wall, %.1f ms a call"
                        % (inc, 100.0 * inc / max(1e-9, wall),
                           inc * 1000.0 / len(xf)))
                    say("      %.2f guest s OUTSIDE it - the OS and the "
                        "program" % (wall - inc))

        named = [r for r in mine if r.get("name")]
        if named:
            seen = {}
            for r in named:
                k = (fname(vec, (r["ax"] >> 8) & 0xFF), r["name"])
                seen[k] = seen.get(k, 0) + 1
            say("")
            say("    the names it asked for:")
            for (what, nm), n in sorted(seen.items(), key=lambda kv: -kv[1]):
                say("      %-14s %-40s x%d" % (what, nm, n))
        if timed:
            say("    TOTAL %.1f guest ms inside int %02Xh"
                % (tot / GUEST_HZ * 1000.0, vec))
        say("")

    if not timed:
        say("  (no --time: a call's cost is bounded by the NEXT call's stamp,")
        say("   which is an upper bound. Re-run with --time for exact cycles.)")


def diff(a, b, say=print):
    """Two runs of the same program, side by side.

    COUNTS AND NOT TIMES, unless both were timed: the two machines run at
    different speeds by construction, and a table that put their milliseconds
    next to each other would invite exactly the comparison that means nothing.
    What IS comparable is how many times each was asked for something.
    """
    def tally(d):
        out = {}
        for r in d["recs"]:
            out[(r["v"], (r["ax"] >> 8) & 0xFF)] = \
                out.get((r["v"], (r["ax"] >> 8) & 0xFF), 0) + 1
        return out
    ta, tb = tally(a), tally(b)
    keys = sorted(set(ta) | set(tb))
    say("")
    say("  %-6s %-16s %9s %9s %9s" % ("fn", "what", "A", "B", "A-B"))
    for k in keys:
        na, nb = ta.get(k, 0), tb.get(k, 0)
        say("  int %02Xh %-16s %9d %9d %+9d"
            % (k[0], fname(k[0], k[1]), na, nb, na - nb))
    say("")
    say("  totals: A %d calls, B %d" % (sum(ta.values()), sum(tb.values())))


# ===========================================================================
def selfcheck(say=print):
    """No emulator, no network: the decode and the report, on made-up records.

    It proves the arithmetic and the labels, which is what goes wrong in a
    reporting tool - not the socket, which fails loudly.
    """
    bad = []
    if fname(0x21, 0x3F) != "READ":
        bad.append("int 21h AH=3Fh should be READ")
    if fname(0x13, 0x02) != "READ sectors":
        bad.append("int 13h AH=02h should be READ sectors")
    if fname(0x21, 0xFE) != "FEh":
        bad.append("an unnamed function must print as its number")
    if drive_of(0x13, {"dx": 0x0080}) != "hd0":
        bad.append("int 13h DL=80h is the first fixed disk")
    if drive_of(0x13, {"dx": 0x0001}) != "B:":
        bad.append("int 13h DL=1 is B:")
    if drive_of(0x21, {"ax": 0x0E00, "dx": 0x0002}) != "C:":
        bad.append("int 21h AH=0Eh DL=2 is C:")

    recs = [
        {"v": 0x13, "cyc": 0, "ax": 0x0201, "bx": 0, "cx": 1, "dx": 0x0000,
         "si": 0, "di": 0, "ds": 0, "es": 0, "cs": 0x1000, "ip": 0x10,
         "in": int(GUEST_HZ * 0.4)},
        {"v": 0x13, "cyc": int(GUEST_HZ), "ax": 0x0208, "bx": 0, "cx": 2,
         "dx": 0x0001, "si": 0, "di": 0, "ds": 0, "es": 0,
         "cs": 0x1000, "ip": 0x20, "in": int(GUEST_HZ * 0.8)},
    ]
    lines = []
    report({"vectors": [0x13], "timed": True, "recs": recs}, say=lines.append)
    txt = "\n".join(lines)
    if "9 sectors" not in txt:
        bad.append("AL is the SECTOR COUNT and 1 + 8 is 9: %r" % txt)
    if "A: x1" not in txt or "B: x1" not in txt:
        bad.append("one call on each of A: and B: should both be named")
    if "1200.0" not in txt:
        bad.append("0.4s + 0.8s of in-call time is 1,200 guest ms")

    if chs_of({"cx": 0x0001, "dx": 0x0000}) != (0, 0, 1):
        bad.append("CH=0 CL=1 is cylinder 0, sector 1")
    if chs_of({"cx": 0x0101, "dx": 0x0000}) != (1, 0, 1):
        bad.append("CH is the cylinder's LOW BYTE: 0x0101 is cylinder 1")
    if chs_of({"cx": 0x1309, "dx": 0x0100}) != (0x13, 1, 9):
        bad.append("CH=13h CL=09h DH=1 is cylinder 19, head 1, sector 9")
    if chs_of({"cx": 0x01C1, "dx": 0x0000}) != (769, 0, 1):
        bad.append("CL bits 6-7 are the cylinder's TOP TWO: 0x1C1 is cyl 769")
    if "the order:" not in txt or "the order: A B" not in txt:
        bad.append("the drive ORDER is what a person watching the lights "
                   "sees, and it must be printed: %r" % txt)
    if "4.5 sector(s) a call" not in txt:
        bad.append("the pace must price BATCHING: 9 sectors in 2 calls "
                   "is 4.5 a call: %r" % txt)
    if "1.80 guest s" not in txt:
        bad.append("a TIMED wall runs to the last RETURN, so 1s apart plus "
                   "0.8s in the second call is 1.80: %r" % txt)
    if "1.20 guest s INSIDE" not in txt or "0.60 guest s OUTSIDE" not in txt:
        bad.append("the two halves must ADD UP to the wall: %r" % txt)

    dl = []
    diff({"recs": recs}, {"recs": recs[:1]}, say=dl.append)
    if "+1" not in "\n".join(dl):
        bad.append("diff must show the call B does not make")

    for b in bad:
        say("selfcheck: FAIL: %s" % b)
    say("os88intmon: selfcheck %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("verb", nargs="?", choices=("watch", "report", "diff"))
    ap.add_argument("args", nargs="*")
    ap.add_argument("--int", dest="vec", action="append", default=[],
                    help="a vector to watch, in hex: --int 21 --int 13")
    ap.add_argument("--for", dest="budget", type=float, default=30.0,
                    help="GUEST seconds to watch for (default 30)")
    ap.add_argument("--time", action="store_true",
                    help="also break on each call's return: exact in-call cycles")
    ap.add_argument("--out", help="write the records here as JSON")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args(argv)

    if a.selfcheck:
        return selfcheck()
    if not a.verb:
        ap.print_help()
        return 2

    if a.verb == "report":
        if not a.args:
            sys.exit("intmon: report wants a json file")
        report(json.load(open(a.args[0])), top=a.top)
        return 0
    if a.verb == "diff":
        if len(a.args) < 2:
            sys.exit("intmon: diff wants two json files")
        diff(json.load(open(a.args[0])), json.load(open(a.args[1])))
        return 0

    import os88marty
    if not a.args:
        sys.exit("intmon: watch wants an address - `os88marty.py instances`")
    vectors = [int(v, 16) for v in a.vec] or [0x21]
    m = os88marty.Marty(a.args[0])
    print("intmon: watching int %s on %s, %.0f guest seconds%s"
          % (", ".join("%02Xh" % v for v in vectors), a.args[0], a.budget,
             ", timed" if a.time else ""))
    recs = watch(m, vectors, a.budget, do_time=a.time, out=a.out)
    report({"vectors": vectors, "timed": a.time, "recs": recs}, top=a.top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
