#!/usr/bin/env python3
"""The Browser's incrementally grown logical line-index arena.

    make browsertest && python3 tests/brarena.py

Opens LINARENA.HTM from B: in QEMU without a network card. The fixture emits
3,000 short lines: enough records to exceed the old 16 KiB line-index limit,
while keeping source and formatted text small. This isolates the line arena
from NE2000 traffic and tests the same local-file path users have.
"""
import os
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
import dispcp
import os88build
import os88qemu
import os88sym
from ethernet import Qemu, Mouse, settle, SOCK

S = os88sym.linear
PAGE = "LINARENA.HTM"
SENTINEL = b"END OF 3000 LINE ARENA TEST"
WIDE_PAGE = "DOCWIDE.HTM"
WIDE_SENTINEL = b"END OF WIDE DOCUMENT ABOVE 64K"
WIDE_LINK = b"HIGH-OFFSET-LINK"


def main():
    os88qemu.own()
    os88qemu.kill()
    subprocess.run(["make", "browsertest"], check=True)
    subprocess.run(["make", "test", "TESTAPPS=build/brtest360.img"],
                   check=True)
    m = Qemu(SOCK)
    try:
        os88qemu.acted(m, lambda: os88qemu.ui_idle(m, S), secs=30,
                       what="desktop UI idle")
        mo = Mouse()
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            raise RuntimeError("B: did not open")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, PAGE)
        wins = dispcp.win_list(m, S)
        if len(wins) < 2:
            raise RuntimeError("the local HTML file did not open in Browser")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        image = os.path.getsize(os88build.at("build/browser.bin"))

        def symbol(name):
            with open("build/browser.map", encoding="ascii") as f:
                for line in f:
                    fields = line.split()
                    if len(fields) >= 3 and fields[-1] == name:
                        return int(fields[0], 16) - image
            raise RuntimeError(name + " missing from build/browser.map")

        def word(offset):
            return word_in(pseg, offset)

        def word_in(seg, offset):
            raw = m.readseg(seg, image + offset, 2)
            return raw[0] | (raw[1] << 8)

        def byte(offset):
            return m.readseg(pseg, image + offset, 1)[0]

        def dword(low, high):
            return word(low) | (word(high) << 16)

        def dword_in(seg, low, high):
            return word_in(seg, low) | (word_in(seg, high) << 16)

        def dword_at(seg, offset):
            raw = m.readseg(seg, image + offset, 4)
            return int.from_bytes(raw, "little")

        def read_logical(seg, offset, length):
            out = bytearray()
            while length:
                local = offset & 15
                count = min(length, 0x4000)
                out.extend(m.readseg((seg + (offset >> 4)) & 0xFFFF,
                                     local, count))
                offset += count
                length -= count
            return bytes(out)

        linecap_off = symbol("br_linecap")
        if linecap_off < 0:
            raise RuntimeError("invalid br_linecap offset")
        os88qemu.acted(m, lambda: word(38) >= 2750,
                       secs=120, what="3,000-line document layout")
        lines, line_bytes = word(38), word(linecap_off)
        docseg, doclen = word(26), word(36)
        doc = m.readseg(docseg, 0, doclen)
        print("brarena: %d lines, %d line-index bytes, %d document bytes"
              % (lines, line_bytes, doclen))
        if lines < 2750:
            raise RuntimeError("only %d lines laid out" % lines)
        if line_bytes <= 16 * 1024 or line_bytes < lines * 10:
            raise RuntimeError("line-index claim is %d bytes for %d lines"
                               % (line_bytes, lines))
        if SENTINEL not in doc:
            raise RuntimeError("formatted stream lost its final sentinel")

        # Exercise a local source and formatted document that both cross 64 KiB.
        # The final line is painted after End, so this checks the dword line
        # spans at the renderer boundary rather than only inspecting storage.
        m.quit()
        os88qemu.kill()
        subprocess.run(["make", "test", "TESTAPPS=build/brtest360.img"],
                       check=True)
        m = Qemu(SOCK)
        os88qemu.acted(m, lambda: os88qemu.ui_idle(m, S), secs=30,
                       what="desktop UI idle for wide-document fixture")
        mo = Mouse()
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, WIDE_PAGE)
        wins = dispcp.win_list(m, S)
        if len(wins) < 2:
            raise RuntimeError("the wide local HTML file did not open")
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        doclenlo_off = symbol("br_doclen")
        doclenhi_off = symbol("br_doclen_hi")
        os88qemu.acted(
            m, lambda: word(38) >= 1200 and dword(doclenlo_off, doclenhi_off) > 65536,
            secs=120, what="wide local source and formatted document")
        lines = word(38)
        docseg = word(26)
        doclen = dword(doclenlo_off, doclenhi_off)
        line_table = m.readseg(word(28), 0, lines * 10)
        high_span = None
        for i in range(lines):
            start = int.from_bytes(line_table[i * 10:i * 10 + 4], "little")
            end = int.from_bytes(line_table[i * 10 + 4:i * 10 + 8], "little")
            if start >= 65536 and start < end <= doclen:
                high_span = (start, end)
                break
        if high_span is None:
            raise RuntimeError("no valid display-line span above the 64 KiB seam")
        docbytes = read_logical(docseg, 0, doclen)
        if WIDE_SENTINEL not in docbytes:
            raise RuntimeError("the wide formatted document lost its tail sentinel")
        bx, by, bw, bh = dispcp.win_rect(m, S, wins[-1])
        mo.click(bx + min(100, bw // 2),
                 by + dispcp.TITLE_H + min(40, max(20, bh // 4)))
        settle(m)
        m.key("End")
        settle(m)
        top, rows = word(14), word(12)
        if top < max(0, lines - rows):
            raise RuntimeError("End did not scroll to the high-offset lines")
        painted = m.readseg(pseg, image + 100, 100)
        if WIDE_LINK not in painted:
            raise RuntimeError("the painter did not render the high-offset tail line: "
                               "top=%d rows=%d link=%d tail=%r" %
                               (top, rows, docbytes.index(WIDE_LINK), painted))

        # Form records use logical offsets too. These controls and their
        # source tags are all after the 64 KiB seam.
        fn_off = symbol("br_fn")
        fld_off = symbol("br_flds")
        if byte(fn_off) < 2:
            raise RuntimeError("high-offset form fields were not retained")
        field0 = fld_off
        field1 = fld_off + 40
        text_ofs = dword_at(pseg, field0)
        select_ofs = dword_at(pseg, field1)
        first_option = dword_at(pseg, field1 + 36)
        next_option = dword_at(pseg, field1 + 32)
        if min(text_ofs, select_ofs, first_option, next_option) <= 65536:
            raise RuntimeError("form document/source offsets were not widened")
        subn_off, subs_off = symbol("br_subn"), symbol("br_subs")
        if byte(subn_off) < 1:
            raise RuntimeError("high-offset submit button was not retained")
        if min(dword_at(pseg, subs_off), dword_at(pseg, subs_off + 10),
               dword_at(pseg, subs_off + 14)) <= 65536:
            raise RuntimeError("submit button/source attribute offsets were not widened")

        text_line = None
        text_x = 0
        for i in range(lines):
            off = i * 10
            start = int.from_bytes(line_table[off:off + 4], "little")
            end = int.from_bytes(line_table[off + 4:off + 8], "little")
            if start <= text_ofs < end:
                text_line = i
                text_x = line_table[off + 8] + text_ofs - start
                break
        if text_line is None or not top <= text_line < top + rows:
            raise RuntimeError("high-offset text field is not in the visible tail")
        cx, cy = word(2), word(4)
        mo.click(cx + text_x * 8 + 4,
                 cy + (text_line - top) * 8 + 4)
        settle(m)
        m.hmp("sendkey x")
        os88qemu.pace(m, 0.05)
        settle(m)
        if byte(field0 + 5) != 5 or read_logical(docseg, text_ofs + 4, 1) != b"x":
            raise RuntimeError("high-offset text field did not accept a typed byte")

        # Find the select's line from the same logical line records used by
        # the painter, then click one of its option cells in the visible band.
        select_line = None
        select_x = 0
        for i in range(lines):
            off = i * 10
            start = int.from_bytes(line_table[off:off + 4], "little")
            end = int.from_bytes(line_table[off + 4:off + 8], "little")
            if start <= select_ofs < end:
                select_line = i
                select_x = line_table[off + 8] + select_ofs - start
                break
        if select_line is None or not top <= select_line < top + rows:
            raise RuntimeError("high-offset select is not on the visible tail")
        cx, cy = word(2), word(4)
        mo.click(cx + select_x * 8 + 4,
                 cy + (select_line - top) * 8 + 4)
        settle(m)
        select_value = bytes(m.readseg(pseg, image + field1 + 20, 12)) \
            .split(b"\0", 1)[0]
        if select_value != b"one":
            raise RuntimeError("high-offset select did not cycle: %r" % select_value)
        print("brarena: high-offset text/select/button records and select click ok")

        link_ofs = docbytes.index(WIDE_LINK)
        link_line = None
        link_x = 0
        for i in range(lines):
            off = i * 10
            start = int.from_bytes(line_table[off:off + 4], "little")
            end = int.from_bytes(line_table[off + 4:off + 8], "little")
            if start <= link_ofs < end:
                link_line = i
                link_x = line_table[off + 8] + link_ofs - start
                break
        if link_line is None or not top <= link_line < top + rows:
            raise RuntimeError("high-offset link is not in the visible tail")
        cx, cy, rows = word(2), word(4), word(12)
        mo.click(cx + link_x * 8 + 3,
                 cy + (link_line - top) * 8 + 4)
        settle(m)
        target = (bytes(m.readseg(pseg, image + symbol("br_ubuf"), 160))
                  .split(b"\0", 1)[0])
        if target != b"http://10.0.2.2:8090/wide-target":
            raise RuntimeError("high-offset link click resolved to %r" % target)
        print("brarena: %d-byte local document, %d lines, high span %d..%d painted"
              % (doclen, lines, high_span[0], high_span[1]))

        # A second run exercises the submit path itself. The first run checked
        # its stored logical spans, but only clicking the high-offset button
        # proves br_submit remaps its retained-source attribute tag.
        m.quit()
        os88qemu.kill()
        subprocess.run(["make", "test", "TESTAPPS=build/brtest360.img"],
                       check=True)
        m = Qemu(SOCK)
        os88qemu.acted(m, lambda: os88qemu.ui_idle(m, S), secs=30,
                       what="desktop UI idle for high-offset submit fixture")
        mo = Mouse()
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, WIDE_PAGE)
        wins = dispcp.win_list(m, S)
        rec = m.read(S("wm_wins") + wins[-1] * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        os88qemu.acted(
            m, lambda: word(38) >= 1200 and dword(doclenlo_off, doclenhi_off) > 65536,
            secs=120, what="high-offset form page before submit")
        lines = word(38)
        docseg = word(26)
        doclen = dword(doclenlo_off, doclenhi_off)
        line_table = m.readseg(word(28), 0, lines * 10)
        subs_off = symbol("br_subs")
        button_start = dword_at(pseg, subs_off)
        button_line = None
        button_x = 0
        for i in range(lines):
            off = i * 10
            start = int.from_bytes(line_table[off:off + 4], "little")
            end = int.from_bytes(line_table[off + 4:off + 8], "little")
            if start <= button_start < end:
                button_line = i
                button_x = line_table[off + 8] + button_start - start
                break
        if button_line is None:
            raise RuntimeError("high-offset submit button has no display line")
        bx, by, bw, bh = dispcp.win_rect(m, S, wins[-1])
        mo.click(bx + min(100, bw // 2),
                 by + dispcp.TITLE_H + min(40, max(20, bh // 4)))
        settle(m)
        m.key("End")
        settle(m)
        top, rows = word(14), word(12)
        if not top <= button_line < top + rows:
            raise RuntimeError("high-offset submit button is not visible at End")
        cx, cy = word(2), word(4)
        mo.click(cx + button_x * 8 + 4,
                 cy + (button_line - top) * 8 + 4)
        settle(m)
        submit_url = bytes(m.readseg(pseg, image + symbol("br_ubuf"), 160)) \
            .split(b"\0", 1)[0]
        expected_submit = (b"http://10.0.2.2:8090/wide-submit?"
                           b"q=seed&mode=two&go=Send")
        if submit_url != expected_submit:
            raise RuntimeError("high-offset submit built %r, expected %r"
                               % (submit_url, expected_submit))
        print("brarena: high-offset submit remapped its source attributes")

        # Tables append composed rows after D_END. Exercise the dword append
        # cursor, remapped writer and 10-byte line spans on a real local page.
        m.quit()
        os88qemu.kill()
        subprocess.run(["make", "test", "TESTAPPS=build/brtest360.img"],
                       check=True)
        m = Qemu(SOCK)
        os88qemu.acted(m, lambda: os88qemu.ui_idle(m, S), secs=30,
                       what="desktop UI idle for table fixture")
        mo = Mouse()
        dispcp.open_drive(m, mo, S, settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "PUBZONE.HTM")
        wins = dispcp.win_list(m, S)
        doclo, dochi = symbol("br_doclen"), symbol("br_doclen_hi")
        complo, comphi = symbol("br_comp"), symbol("br_comp_hi")
        found = [None]
        seen = []

        def table_layout_done():
            seen.clear()
            for slot in wins:
                rec = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE,
                             dispcp.WIN_SIZE)
                seg = rec[22] | (rec[23] << 8)
                title_off = rec[10] | (rec[11] << 8)
                title = bytes(m.readseg(seg or 0x1000, title_off, 24)) \
                    .split(b"\\0", 1)[0].decode("latin-1", "replace")
                nlines = word_in(seg, 38)
                doclo_now, dochi_now = word_in(seg, doclo), word_in(seg, dochi)
                complo_now, comphi_now = word_in(seg, complo), word_in(seg, comphi)
                doclen_now = doclo_now | (dochi_now << 16)
                comp_now = complo_now | (comphi_now << 16)
                seen.append((slot, title, seg, word_in(seg, 26), doclo_now, dochi_now,
                             complo_now, comphi_now, nlines))
                if (word_in(seg, 26) and nlines > 5
                        and doclen_now + 1 < comp_now
                        < doclen_now + 16 * 1024):
                    found[0] = (seg, doclen_now, comp_now, nlines)
                    return True
            return False

        os88qemu.acted(m, table_layout_done, secs=60,
                       what="PUBZONE table layout and composition")
        if found[0] is None:
            raise RuntimeError("no composed Browser state; windows=%r" % seen)
        pseg, doclen, comp, nlines = found[0]
        line_table = m.readseg(word(28), 0, nlines * 10)
        composed = False
        spans = []
        for i in range(nlines):
            start = int.from_bytes(line_table[i * 10:i * 10 + 4], "little")
            end = int.from_bytes(line_table[i * 10 + 4:i * 10 + 8], "little")
            if i < 8:
                spans.append((start, end))
            if doclen <= start < end <= comp:
                composed = True
                break
        if not composed:
            raise RuntimeError("no composed span: doc=%d comp=%d lines=%d first=%r"
                               % (doclen, comp, nlines, spans))
        print("brarena: table composition span %d..%d after %d-byte document"
              % (start, end, doclen))
        print("brarena: ok")
    finally:
        m.quit()


if __name__ == "__main__":
    main()
