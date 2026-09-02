#!/usr/bin/env python3
"""Make a saved old.reddit page safe to commit as a fixture.

    python3 tests/proxy/scrub.py <saved.htm> <fixture.htm>

A page saved from a SIGNED-IN session carries the account with it: the
username in four places, a live **modhash** (reddit's CSRF token, valid for
that session), a `loid` tracking id, the subscription list, and the karma
count. None of that may go into a public repository, and none of it is what
the fixture is for - the fixture is for the MARKUP.

What it removes, and nothing else:

  * the body of every <script> and <style>, which is where the modhash, the
    loid and the config blob live. The TAGS stay, because "drops the content
    of a <script>" is one of the things the fixture is checking;
  * the account name, everywhere, replaced by a fixed one;
  * the personal subreddit lists in the header (`srdrop` and the sr-bar);
  * the karma figure;
  * the local `*_files/` image paths a browser save rewrites, which point at a
    directory nobody has.

It then REFUSES to write the file if any of the markers are still in it, so a
scrub that silently missed something cannot produce a fixture.
"""
import re
import sys

USER = "os8088user"
# WORD boundaries, not substrings: `tabloid` contains `loid` and a subreddit's
# rules table is full of the word, so a substring check refuses a file that is
# already clean and teaches whoever hits it to pass a flag.
MARKERS = (r"\bmodhash\b", r"\bloid\b", r"_files/")


def scrub_json(raw, user):
    """A saved `.json` endpoint. Far less to do - the API answers a signed-out
    reader with no account in it at all - but `modhash` is in every Listing
    (empty when logged out, a live CSRF token when not), so it is blanked and
    then checked, which is what makes committing one of these safe."""
    raw = re.sub(r'"modhash"\s*:\s*"[^"]*"', '"modhash": ""', raw)
    if user:
        raw = re.sub(re.escape(user), USER, raw, flags=re.I)
    return raw


def scrub(raw, user):
    if raw.lstrip()[:1] in ("{", "["):
        return scrub_json(raw, user)
    def blank(m):
        return "%s/* scrubbed - tests/proxy/scrub.py */%s" % (m.group(1), m.group(3))
    raw = re.sub(r"(?is)(<script[^>]*>)(.*?)(</script>)", blank, raw)
    raw = re.sub(r"(?is)(<style[^>]*>)(.*?)(</style>)", blank, raw)
    if user:
        raw = re.sub(re.escape(user), USER, raw, flags=re.I)
    # the header's subscription lists: personal, and nothing parses them
    raw = re.sub(r'(?is)(<div class="drop-choices srdrop">).*?(</div>)',
                 r"\1\2", raw)
    raw = re.sub(r'(?is)(<ul class="sr-bar[^"]*"[^>]*>).*?(</ul>)', r"\1\2", raw)
    raw = re.sub(r'(?is)(<span class="userkarma"[^>]*>)[^<]*(</span>)',
                 r"\g<1>0\2", raw)
    # ...and the session ids MODERN reddit hangs off its elements rather than
    # burying in a script: `<shreddit-app loid="...">` is the tracking id the
    # old pages kept in a config blob, and `navigationSessionId` is one visit.
    # Both are REMOVED rather than blanked, so the marker check below keeps
    # meaning what it says.
    raw = re.sub(r'\s+loid="[^"]*"', "", raw)
    raw = re.sub(r'navigationSessionId=[^&"\']*', "navigationSessionId=", raw)
    # The AD payloads, which are tracking URLs with a signed blob in them and
    # 38KB of markup apiece. Nothing here parses an ad - `<shreddit-ad-post>`
    # is a different element from `<shreddit-post>`, which is exactly why the
    # listing skips ads for free - so what is kept is the SHAPE and not the
    # beacon.
    raw = re.sub(r'ad-events="[^"]*"', 'ad-events=""', raw)
    raw = re.sub(r'https://alb\.reddit\.com/[^"\'<> ]*',
                 "https://alb.reddit.com/i.gif", raw)
    raw = re.sub(r'src="[^"]*_files/[^"]*"', 'src="https://i.redd.it/thumb.jpg"',
                 raw)
    raw = re.sub(r'href="[^"]*_files/[^"]*"', 'href="https://old.reddit.com/"',
                 raw)
    # ...and every OTHER attribute a browser save rewrites - srcset, poster,
    # data-src. The two above were written for one capture and the next one
    # had `srcset` in it, which is why the refusal at the end of this file is
    # worth more than the substitutions: it caught that, rather than shipping
    # a fixture full of paths to a directory nobody has.
    raw = re.sub(r'="[^"]*_files/[^"]*"', '="removed-by-scrub"', raw)
    raw = re.sub(r"='[^']*_files/[^']*'", "='removed-by-scrub'", raw)
    return raw


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    user = sys.argv[3] if len(sys.argv) > 3 else ""
    raw = open(src, encoding="utf-8", errors="replace").read()
    out = scrub(raw, user)
    # `"modhash": ""` is the blanked form and is not a leak; anything else
    # carrying the word still refuses.
    probe = out.replace('"modhash": ""', "")
    left = [m for m in MARKERS if re.search(m, probe)]
    if user and re.search(re.escape(user), out, re.I):
        left.append(user)
    if left:
        sys.stderr.write("REFUSING to write %s: still carries %s\n"
                         % (dst, ", ".join(left)))
        return 1
    open(dst, "w", encoding="utf-8").write(out)
    print("%s -> %s  (%d -> %d bytes)" % (src, dst, len(raw), len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
