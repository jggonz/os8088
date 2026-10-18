#!/usr/bin/env python3
"""Build deterministic, oversized Browser pages for the memory-tier tests.

The generated files live under build/ and are served by the host to QEMU's
NE2000 guest at 10.0.2.2. Nothing in this tool is shipped on a system disk.
"""

from __future__ import annotations

import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os


def write_numbered_rows(path: str, target: int, prefix: bytes,
                        row_format: bytes, suffix: bytes) -> None:
    """Write exact-size fixtures whose visible rows identify the cutoff."""
    body = bytearray(prefix)
    row = 0
    while True:
        line = row_format % row
        if len(body) + len(line) + len(suffix) > target:
            break
        body.extend(line)
        row += 1
    body.extend(b"x" * (target - len(body) - len(suffix)))
    body.extend(suffix)
    with open(path, "wb") as f:
        f.write(body)


def write_boundary_page(path: str, target: int) -> None:
    """Put visible sentinels on both sides of the Browser's storage seams.

    Padding is plain text inside PRE so it remains predictable HTML. Each
    sentinel starts at the requested source offset; this makes a screenshot
    or a final-line check identify exactly which storage window was reached.
    """
    body = bytearray(b"<!doctype html><title>Browser boundary test</title>"
                     b"<h1>Browser boundary test</h1><pre>\n")
    for offset in (16 * 1024, 32 * 1024, 64 * 1024, 96 * 1024,
                   128 * 1024, 144 * 1024):
        if offset >= target:
            break
        marker = ("\n--- SOURCE OFFSET %06d ---\n" % offset).encode("ascii")
        if len(body) + len(marker) >= offset:
            raise ValueError("fixture prefix crossed a boundary")
        body.extend(b"x" * (offset - len(body)))
        body.extend(marker)
    suffix = b"\nEND OF 160 KiB BOUNDARY PAGE\n</pre>"
    if len(body) + len(suffix) > target:
        raise ValueError("target is too small for boundary markers")
    body.extend(b"x" * (target - len(body) - len(suffix)))
    body.extend(suffix)
    with open(path, "wb") as f:
        f.write(body)


def write_ui_boundary_page(path: str, target: int) -> None:
    """Put compact HTML structures on both sides of source-window seams."""
    body = bytearray(b"<!doctype html><title>Browser UI boundary test</title>"
                     b"<h1>Browser UI boundary test</h1>\n")
    cases = (
        (16 * 1024, b'<a href="/target/16k">LINK-16K</a>'),
        (32 * 1024, b'<table><tr><td>TABLE-32K</td></tr></table>'),
        (64 * 1024, b'<form action="/submit"><input name="mark" value="64K">'
                   b'<input type="submit" value="Send"></form>'),
        (72 * 1024, b'<a href="/target/72k">LINK-72K</a>'),
    )
    for offset, markup in cases:
        start = offset - 12
        if len(body) >= start or len(markup) <= 12:
            raise ValueError("fixture markup cannot cross the requested boundary")
        body.extend(b" " * (start - len(body)))
        body.extend(markup)
        body.extend(("\n--- CROSSED SOURCE OFFSET %06d ---\n" % offset)
                    .encode("ascii"))
    suffix = b"\nEND OF 100 KiB UI BOUNDARY PAGE\n"
    if len(body) + len(suffix) > target:
        raise ValueError("target is too small for UI boundary cases")
    body.extend(b"x" * (target - len(body) - len(suffix)))
    body.extend(suffix)
    with open(path, "wb") as f:
        f.write(body)


def write_line_arena_page(path: str, lines: int = 3000) -> None:
    """Force the line index past its former 16 KiB allocation ceiling."""
    body = bytearray(b"<!doctype html><title>Browser line arena test</title>"
                     b"<h1>Browser line arena test</h1><pre>\n")
    for _ in range(lines):
        body.extend(b"x<br>")
    body.extend(b"END OF 3000 LINE ARENA TEST\n</pre>")
    with open(path, "wb") as f:
        f.write(body)


def write_wide_document_page(path: str, lines: int = 1200) -> None:
    """Make both source and formatted text cross the 64 KiB offset seam."""
    body = bytearray(b"<!doctype html><title>Wide Document Arena</title><pre>\n")
    row = b"wide-document-content-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    for number in range(lines):
        body.extend(("%04d " % number).encode("ascii"))
        body.extend(row)
        body.extend(b"\n")
    body.extend(b"END OF WIDE DOCUMENT ABOVE 64K\n")
    body.extend(b'<form action="http://10.0.2.2:8090/wide-submit">'
                b'<input name="q" value="seed">'
                b'<select name="mode"><option value="one">one</option>'
                b'<option value="two" selected>two</option></select>'
                b'<input type="submit" name="go" value="Send"></form>\n')
    body.extend(b'<a href="http://10.0.2.2:8090/wide-target">HIGH-OFFSET-LINK</a>')
    with open(path, "wb") as f:
        f.write(body)


def write_source_pressure_page(path: str, target: int = 768 * 1024) -> None:
    """Force the network source claim to refuse growth without bloating layout.

    Collapsible whitespace keeps the formatted page tiny, so the test reaches
    the network source-growth refusal rather than the document or line arena.
    The Browser must drain the rest of the response, parse the retained prefix,
    settle, and report truncation.
    """
    prefix = (b"<!doctype html><title>Source pressure</title>"
              b"<h1>Source pressure</h1>\n")
    prefix += b"".join(
        ("<p>START-SOURCE-PRESSURE-%02d</p>\n" % row).encode("ascii")
        for row in range(8))
    suffix = b"\n<p>END-SOURCE-PRESSURE</p>"
    if len(prefix) + len(suffix) > target:
        raise ValueError("source pressure target is too small")
    with open(path, "wb") as f:
        f.write(prefix)
        remaining = target - len(prefix) - len(suffix)
        block = b" " * min(4096, remaining)
        while remaining:
            n = min(len(block), remaining)
            f.write(block[:n])
            remaining -= n
        f.write(suffix)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="build/brmemory")
    ap.add_argument("--serve", action="store_true",
                    help="serve the generated fixtures on 0.0.0.0")
    ap.add_argument("--port", type=int, default=8091)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    prefix = (b"<!doctype html><title>Browser Memory Test</title>"
              b"<h1>Browser Memory Test</h1><pre>\n")
    write_numbered_rows(
        os.path.join(args.out, "mem96k.htm"), 96 * 1024, prefix,
        b"row %06d: conventional / EMS / XMS incremental page data\n",
        b"\nEND OF 96 KiB SOURCE TEST\n")
    # Put the marker just before the formatted floor, then continue producing
    # content. A memory-limited run can prove it crossed 196 KiB even when the
    # final tail is correctly refused to preserve the reserve.
    capacity_path = os.path.join(args.out, "mem196k.htm")
    capacity = bytearray(prefix)
    row = 0
    while len(capacity) < 198 * 1024:
        capacity.extend(b"row %06d: formatted-document growth beyond 64 KiB\n" % row)
        row += 1
    capacity.extend(b"END196KFLOOR\n")
    capacity.extend(b"x" * (200 * 1024 - len(capacity)))
    with open(capacity_path, "wb") as f:
        f.write(capacity)
    write_boundary_page(os.path.join(args.out, "boundary100k.htm"), 100 * 1024)
    write_boundary_page(os.path.join(args.out, "boundary160k.htm"), 160 * 1024)
    write_ui_boundary_page(os.path.join(args.out, "boundaryui100k.htm"), 100 * 1024)
    write_line_arena_page(os.path.join(args.out, "linearena.htm"))
    write_wide_document_page(os.path.join(args.out, "docwide.htm"))
    write_source_pressure_page(os.path.join(args.out, "sourcepressure.htm"))

    links = ["<!doctype html><title>400 links and 8 forms</title><h1>"
             "400 links and 8 forms</h1>"]
    for i in range(400):
        links.append('<a href="/target/%03d">link %03d</a> ' % (i, i))
    for i in range(8):
        links.append('<form action="/post/%d"><input name="q" value="%d">'
                     '<input type="submit" value="Send"></form>' % (i, i))
    links.append("<p>END OF LINKS AND FORMS</p>")
    with open(os.path.join(args.out, "links400.htm"), "wb") as f:
        f.write("\n".join(links).encode("ascii"))

    for name in ("mem96k.htm", "mem196k.htm", "boundary100k.htm",
                 "boundary160k.htm",
                 "boundaryui100k.htm", "linearena.htm", "docwide.htm",
                 "sourcepressure.htm", "links400.htm"):
        print("%s: %d bytes" % (name, os.path.getsize(os.path.join(args.out, name))))
    if args.serve:
        os.chdir(args.out)
        server = ThreadingHTTPServer(("0.0.0.0", args.port),
                                     SimpleHTTPRequestHandler)
        print("serving fixtures at http://10.0.2.2:%d/" % args.port, flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
