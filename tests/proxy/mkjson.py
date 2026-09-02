#!/usr/bin/env python3
"""Build the COMMENTS json fixture from the HTML capture beside it.

    python3 tests/proxy/mkjson.py

**The CONTENT is real and the SHAPE is a reconstruction**, laid out the way
reddit's public endpoint documents a thread: `[post-listing, comment-listing]`
with `t1` children whose `replies` is a whole Listing again, plus the `more`
node that stands where a chain was cut. So it checks the PARSER and it does
not check the SCHEMA.

**Both real fixtures came from the field and this one is what is LEFT** -
`www.reddit.com/r/vintagecomputing/.json`, signed out, from the field - and
- a listing and a thread, both signed-out saves, and both confirmed every
field this reconstruction had guessed. What they do NOT carry is a `more`
node: a cut reply chain only appears in a thread big enough to have one, and
neither capture is. So this script builds `reddit-json-more.json`, whose whole
reason to exist is that one node type, and `check_json` asserts the rest of
the schema against the two real files.
"""
import calendar
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def attrs(tag):
    return dict(re.findall(r'([a-zA-Z0-9-]+)="([^"]*)"', tag))


def epoch(iso):
    m = re.match(r"(\d{4})-(\d\d)-(\d\d)[T ](\d\d):(\d\d):(\d\d)", iso or "")
    return float(calendar.timegm([int(x) for x in m.groups()] + [0, 0, 0])) \
        if m else 0.0


def body_html(src, ident, kind):
    """The rich-text div for one thing, verbatim, as `*_html` carries it."""
    m = re.search(r'<div[^>]*id="%s-%s-rtjson-content"[^>]*>' % (ident, kind),
                  src)
    if not m:
        return ""
    i, depth, j = m.end(), 1, m.end()
    for t in re.finditer(r"</?div\b[^>]*>", src[i:]):
        depth += -1 if t.group(0).startswith("</") else 1
        if depth == 0:
            j = i + t.start()
            break
    return '<div class="md">%s</div>' % src[i:j].strip()


def listing(src):
    kids = []
    for m in re.finditer(r"<shreddit-post\s[^>]*>", src):
        a = attrs(m.group(0))
        kids.append({"kind": "t3", "data": {
            "title": a.get("post-title", ""),
            "permalink": a.get("permalink", ""),
            "num_comments": int(a.get("comment-count", "0") or 0),
            "subreddit_name_prefixed": a.get("subreddit-prefixed-name", ""),
            "subreddit": a.get("subreddit-name", ""),
            "score": int(a.get("score", "0") or 0),
            "author": a.get("author", ""),
            "created_utc": epoch(a.get("created-timestamp", "")),
            "url": a.get("content-href", ""),
            "is_self": a.get("post-type", "") == "text",
            "selftext": "",
            "selftext_html": body_html(src, a.get("id", ""), "post"),
            "name": a.get("id", ""),
        }})
    after = kids[-1]["data"]["name"] if kids else None
    return {"kind": "Listing", "data": {"after": after, "before": None,
                                        "children": kids}}


def comments(src):
    post = attrs(re.search(r"<shreddit-post\s[^>]*>", src).group(0))
    t3 = {"kind": "t3", "data": {
        "title": post.get("post-title", ""),
        "permalink": post.get("permalink", ""),
        "num_comments": int(post.get("comment-count", "0") or 0),
        "subreddit_name_prefixed": post.get("subreddit-prefixed-name", ""),
        "score": int(post.get("score", "0") or 0),
        "author": post.get("author", ""),
        "created_utc": epoch(post.get("created-timestamp", "")),
        "url": post.get("content-href", ""),
        "is_self": False, "selftext": "", "selftext_html": "",
        "name": post.get("id", ""),
    }}
    # The elements in document order, comments and cut-chain stubs alike, then
    # re-nested on `depth` - which is how the JSON carries them.
    flat = []
    for m in re.finditer(r"<shreddit-comment\s[^>]*>|"
                         r"<faceplate-partial\s[^>]*more-comments-partial[^>]*>",
                         src):
        a = attrs(m.group(0))
        if m.group(0).startswith("<shreddit-comment"):
            ident = a.get("thingid", "")
            flat.append((int(a.get("depth", "0")), {"kind": "t1", "data": {
                "author": a.get("author", ""),
                "score": int(a.get("score", "0") or 0),
                "created_utc": epoch(a.get("created", "")),
                "permalink": a.get("permalink", ""),
                "id": ident.split("_")[-1], "name": ident,
                "body": "", "body_html": body_html(src, ident, "comment"),
                "replies": "",
            }}))
        else:
            d = re.search(r"startingDepth=(\d+)", a.get("src", ""))
            flat.append((int(d.group(1)) if d else 1, {"kind": "more", "data": {
                "count": 3, "children": ["deadbee"], "permalink": "",
                "parent_id": "t1_x",
            }}))
    top, stack = [], []
    for depth, node in flat:
        while len(stack) > depth:
            stack.pop()
        if not stack:
            top.append(node)
        else:
            parent = stack[-1]["data"]
            if not isinstance(parent.get("replies"), dict):
                parent["replies"] = {"kind": "Listing",
                                     "data": {"after": None, "children": []}}
            parent["replies"]["data"]["children"].append(node)
        if node["kind"] == "t1":
            stack.append(node)
    return [{"kind": "Listing", "data": {"after": None, "children": [t3]}},
            {"kind": "Listing", "data": {"after": None, "children": top}}]


def main():
    src2 = open(os.path.join(HERE, "reddit-www-comments.htm"),
                encoding="utf-8", errors="replace").read()
    two = comments(src2)
    for name, doc in (("reddit-json-more.json", two),):
        with open(os.path.join(HERE, name), "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1, sort_keys=True)
            fh.write("\n")
        print("%s: %d bytes" % (name, os.path.getsize(os.path.join(HERE, name))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
