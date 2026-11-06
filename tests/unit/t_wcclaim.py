#!/usr/bin/env python3
"""tests/wirechat.py's fixture against SPEC.md 113.2.2's claimed nicks.

    python3 tests/unit/t_wcclaim.py

The fixture is the second implementation of SPEC.md 113.2 that the OS is
tested against (hub.js being the first, with its own tests in os8088-web), so
what it says about claims has to be right before `wirechat`'s QEMU row can
mean anything. This drives its `Hub` straight through `answer()` - the same
call the HTTP handler makes - with the clock turned by hand, so the hour- and
day-long limits are checked in a fraction of a second:

- a `claim` answers `K` with a code of SPEC.md's shape, and the fixture keeps
  only a salted hash of it;
- the code is asked for on the nick's next join (428), is compared without
  case, hyphens or spaces, and five wrong ones lock the address out (429);
- a join with the code from ANY address takes the nick over, and the session
  it ended is answered 410 - a poll it is HOLDING included - for a day;
- a join without a code is today's: the same address takes its nick back
  (the old token then 401, not 410), another is 409;
- a new code stops the old one; `release` frees the nick;
- every refusal of SPEC.md 113.2.2's table, in its words.
"""
import os
import re
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tests"))
from harness import check, eq, done                       # noqa: E402
import wirechat                                           # noqa: E402

A, B, C = "10.0.0.1", "10.0.0.2", "10.0.0.3"


class Clock:
    def __init__(self):
        self.t = 1_800_000_000.0

    def __call__(self):
        return self.t


def hub():
    h = wirechat.Hub()
    h.clock = Clock()
    return h


def join(h, nick, code="", addr=A):
    r = h.answer("join", {"v": "0", "st": "0", "x": "1"},
                 nick + ("\n" + code if code else ""), addr)
    tok = r[1].split()[1] if r[0].startswith("OK") else None
    return r, tok


def req(h, verb, tok, body="", addr=A):
    """A verb that answers at once: `say` stands in for poll (which HOLDS)."""
    return h.answer(verb, {"t": tok, "v": "0", "s": str(h.seq), "r": str(h.rver),
                           "st": "0", "x": "1"}, body, addr)


def code_of(r):
    k = [l for l in r if l.startswith("K ")]
    return k[0][2:] if k else None


def main():
    # --- a claim, and the code it makes -------------------------------------
    h = hub()
    r, t1 = join(h, "Amy")
    r = req(h, "claim", t1)
    code = code_of(r)
    check(r[0].startswith("OK ") and r[1].startswith("K "),
          "a claim answers OK, then K, then the delta", got=r[:3])
    check(code is not None and wirechat.CODE_RE.match(code) is not None,
          "the code is four groups of four of SPEC.md 113.2.2's 32 letters",
          got=code)
    check(any(l.startswith("C ") for l in r[2:]), "...and the usual delta follows",
          got=r)
    check(code not in repr(h.claims) and code.replace("-", "") not in repr(h.claims),
          "only a salted hash is kept, never the code",
          why="SPEC.md 113.2.2: the server cannot tell anyone the code again")

    # --- the code at the next join --------------------------------------------
    eq(join(h, "amy", addr=B)[0], ["ER 428 0 That nick needs its code"],
       "a claimed nick without its code is 428, in any case")
    eq(join(h, "Amy", "aaaa-bbbb-cccc-dddd", B)[0], ["ER 428 0 Wrong code for that nick"],
       "a wrong code is 428")
    loose = code.upper().replace("-", "")[:8] + " " + code.upper().replace("-", "")[8:]
    r, t2 = join(h, "AMY", loose, B)
    check(r[0].startswith("OK") and t2, "the code is compared without case, - or space",
          got=r[:2])

    # --- the takeover: from B, with the code, while A's session lives ---------
    eq(req(h, "say", t1, "hello"), ["ER 410 3600 Signed in elsewhere"],
       "the session a takeover ended is answered 410, not 401",
       why="a client rejoins on 401 by itself, and with the code that is the "
           "takeover loop SPEC.md 113.2.3 exists to stop")
    check(t2 in h.sessions and h.sessions[t2]["keyed"] and t1 not in h.sessions,
          "...and the nick is B's, held by its code")

    # A HELD poll of the session being taken over is answered 410 too
    r, t3 = join(h, "Amy", code, C)
    out = []
    th = threading.Thread(target=lambda: out.append(
        h.answer("poll", {"t": t3, "v": "0", "s": str(h.seq), "r": str(h.rver),
                          "x": "1"}, "", C)), daemon=True)
    th.start()
    for _ in range(200):
        if h.held:
            break
        time.sleep(0.01)                    # host: a poll thread to start
    check(h.held == 1, "the poll is being held")
    r, t4 = join(h, "Amy", code, A)
    th.join(3.0)
    eq(out, [["ER 410 3600 Signed in elsewhere"]],
       "a poll HELD when its session is taken over is answered 410",
       why="hub.js answers it goneElsewhere(); a 401 here would restart the loop")
    eq(req(h, "say", t2, "x", B), ["ER 410 3600 Signed in elsewhere"],
       "every taken-over token is remembered, not only the last")

    # --- 410 is remembered for a day, and for 200 tokens ---------------------
    h.clock.t += wirechat.GONE_AGE + 1
    eq(req(h, "say", t2, "x", B), ["ER 401 0 Session ended - joining again"],
       "a day later a taken-over token is a plain 401")
    for i in range(wirechat.GONE_KEEP + 5):
        h.gone["old%09d" % i] = h.clock()
    with h.lock:                            # drop()'s contract: under it
        h.drop("nobody", True)
    check(len(h.gone) == wirechat.GONE_KEEP, "at most %d tokens are remembered"
          % wirechat.GONE_KEEP, got=len(h.gone))

    # --- a nick neither owned nor claimed: today's rule -----------------------
    h = hub()
    r, u1 = join(h, "Dan", addr=A)
    eq(join(h, "Dan", addr=B)[0], ["ER 409 0 That nick is in use"],
       "another address may not take an unclaimed nick in use")
    r, u2 = join(h, "dan", addr=A)
    check(r[0].startswith("OK"), "the same address takes its own nick back")
    eq(req(h, "say", u1, "x"), ["ER 401 0 Session ended - joining again"],
       "...and the session it replaced is 401 (it was not taken over)")
    r = join(h, "Dan", "k7qm-2xfp-9wce-hd3t", B)[0]
    eq(r, ["ER 409 0 That nick is in use"],
       "a second line on an unclaimed nick is ignored: no code takes it")

    # --- a new code, and the old one stops ------------------------------------
    h = hub()
    r, t = join(h, "Eve")
    c1 = code_of(req(h, "claim", t))
    c2 = code_of(req(h, "claim", t))
    check(c1 and c2 and c1 != c2, "a claim from the session holding the code makes "
          "a new one", got=(c1, c2))
    eq(join(h, "Eve", c1, B)[0], ["ER 428 0 Wrong code for that nick"],
       "the old code stops working")
    r, tb = join(h, "Eve", c2, B)
    check(r[0].startswith("OK"), "the new one works")
    c3 = code_of(req(h, "claim", tb, addr=B))
    check(c3 is not None, "a session that JOINED with the code may make a new one")
    check(h.claims["eve"]["addr"] == A,
          "a new code keeps the address that made the claim")

    # --- release ---------------------------------------------------------------
    r = req(h, "claim", tb, "release", B)
    check(r[0].startswith("OK") and r[1] == "N Nick released",
          "release answers N Nick released", got=r[:2])
    check("eve" not in h.claims and not h.sessions[tb]["keyed"],
          "...and the nick is no longer claimed")
    eq(req(h, "claim", tb, "release", B), ["ER 404 0 That nick is not claimed"],
       "releasing a nick that is not claimed is 404")
    r, tc = join(h, "Eve", addr=B)
    check(r[0].startswith("OK"), "a released nick joins with no code")

    # --- the refusals (SPEC.md 113.2.2's table) -------------------------------
    h = hub()
    h.owned["george"] = "0b5e-c0de-42"
    r, tg = join(h, "George", "0B5E-C0DE-42")
    check(r[0].startswith("OK"), "an owned nick's code is compared case-blind")
    eq(req(h, "claim", tg), ["ER 409 0 That nick has an owner"],
       "an owned nick cannot be claimed")
    eq(req(h, "claim", tg, "release"), ["ER 409 0 That nick has an owner"],
       "...nor released")
    r, tf = join(h, "Fay", addr=B)
    h.claims["fay"] = {"hash": "x", "addr": C, "ts": 0}     # made elsewhere
    eq(req(h, "claim", tf, addr=B), ["ER 409 0 That nick is claimed already"],
       "a claimed nick held without its code cannot be claimed again")
    eq(req(h, "claim", tf, "release", B), ["ER 404 0 That nick is not claimed"],
       "...nor released by that session")
    r, ts = join(h, "SysOp", addr=B)
    check(r[0].startswith("OK"), "a reserved name may still be used")
    eq(req(h, "claim", ts, addr=B), ["ER 422 0 That nick cannot be claimed"],
       "a reserved name cannot be claimed, in any case")
    eq(req(h, "claim", ts, "please", B), ["ER 400 0 Bad request"],
       "a body neither empty nor release is 400")
    r, tm = join(h, "Mia", addr=C)
    h.mutes[C] = h.clock() + 9 * 60 + 30
    eq(req(h, "claim", tm, addr=C), ["ER 403 0 You are muted for 10 min"],
       "a muted address cannot claim")

    h = hub()
    toks = [join(h, n)[1] for n in ("One", "Two", "Three")]
    check(code_of(req(h, "claim", toks[0])) and code_of(req(h, "claim", toks[1])),
          "an address may claim two nicks")
    eq(req(h, "claim", toks[2]), ["ER 403 0 Two claimed nicks per address"],
       "...and not a third")
    c = code_of(req(h, "claim", toks[0]))
    check(c is not None, "a new code is a claim: the third of three")
    eq(req(h, "claim", toks[1]), ["ER 429 28800 Too many claims - wait 480 min"],
       "the fourth claim or new code in eight hours is 429, in minutes",
       why="SPEC.md 113.7: rotations count, or they would be free hashing")
    h.clock.t += 8 * 3600
    check(code_of(req(h, "claim", toks[1])) is not None,
          "eight hours later there is one more")
    h.clock.t += 8 * 3600 - 90
    eq(req(h, "claim", toks[1]), ["ER 429 90 Too many claims - wait 90s"],
       "under two minutes the wait is in seconds (hub.js's mins())")

    h = hub()
    h.claims_max = 1
    r, ta = join(h, "Ada", addr=A)
    r, tb = join(h, "Bea", addr=B)
    check(code_of(req(h, "claim", ta)) is not None, "the first claim of a cap of one")
    eq(req(h, "claim", tb, addr=B), ["ER 503 3600 No more nicks can be claimed"],
       "the server's cap is 503 3600")
    check(code_of(req(h, "claim", ta)) is not None,
          "...and a new code is not a new claim: the cap does not refuse it")

    # --- five wrong codes, then the address waits -----------------------------
    h = hub()
    r, t = join(h, "Kim", addr=A)
    kc = code_of(req(h, "claim", t))
    for i in range(5):
        join(h, "Kim", "aaaa-aaaa-aaaa-aaa%d" % (i + 2), B)
    r = join(h, "Kim", kc, B)[0]
    check(len(r) == 1 and re.match(r"^ER 429 720 Too many wrong codes - wait 12 min$", r[0]),
          "five wrong codes from an address and even the right one waits", got=r)
    h.clock.t += 720
    r, t = join(h, "Kim", kc, B)
    check(r[0].startswith("OK"), "...one more try twelve minutes later")

    done("t_wcclaim")


if __name__ == "__main__":
    main()
