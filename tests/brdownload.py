#!/usr/bin/env python3
"""Download a binary response through Save Link As on the emulated NE2000.

    make && make ethertest && make browsertest && python3 tests/brdownload.py

The small index page links to a binary body (32 KiB normally, 70 KiB with
--long-percent to cross the 64 KiB counter rollover). By default the host omits
Content-Length; pass --content-length to exercise a known total. The body is
intentionally larger than the Browser's retained HTML page limit: the test
follows the link, accepts the native Save dialog, checks the reported total,
then compares the destination file read back from the FAT image with the exact
bytes sent by the host. Pass --automatic to first verify that an ordinary click
on the non-HTML link opens that same Save dialog without using the context menu.
This checks the separate download stream rather than asking the page renderer
to hold the file.
"""
import os
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import brclick                                      # noqa: E402
import dispcp                                       # noqa: E402
import ethernet                                     # noqa: E402
import os88build                                    # noqa: E402
import os88fixture                                  # noqa: E402
import os88flush                                    # noqa: E402
import os88sym                                      # noqa: E402
import shot                                         # noqa: E402

S = os88sym.linear
PORT = 8090
URL = "http://10.0.2.2:%d/index.htm" % PORT
WITH_LENGTH = "--content-length" in sys.argv
LONG_PERCENT = "--long-percent" in sys.argv
AUTO = "--automatic" in sys.argv
CANCEL_MODE = next((arg for arg in ("--cancel-active", "--close-active",
                                    "--close-complete")
                    if arg in sys.argv), None)
SHOTDIR = next((arg.split("=", 1)[1] for arg in sys.argv
                if arg.startswith("--shots=")), None)
PAYLOAD = bytes(((i * 37 + (i >> 5) + 11) & 255)
                for i in range(70003 if LONG_PERCENT else 8482))
PAGE = (b"<html><head><title>Download test</title></head><body>"
        b"<p><a href=\"/ems-pressure.htm\">EMS pressure</a></p>"
        b"<p><a href=\"/emsgrowth.htm\">EMS growth</a></p>"
        b"<p><a href=\"/mem96k.htm\">Memory 96K</a></p>"
        b"<p><a href=\"/mem196k.htm\">Memory 196K</a></p>"
        b"<p><a href=\"/download-test.zip\">download-test.zip</a></p>"
        b"</body></html>")


class Server(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ethernet.os88qemu.lease_port(PORT)  # (SOAK-PARALLEL.md 17.3)
        self.sock.bind(("0.0.0.0", PORT))
        self.sock.listen(4)
        self.requests = []

    def run(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            try:
                conn.settimeout(30)
                req = b""
                while b"\r\n\r\n" not in req and len(req) < 4096:
                    part = conn.recv(1024)
                    if not part:
                        break
                    req += part
                self.requests.append(req.split(b"\r\n", 1)[0])
                path = req.split(b" ", 2)[1] if b" " in req else b"/"
                if path == b"/index.htm":
                    body, kind = PAGE, b"text/html"
                elif path == b"/download-test.zip":
                    body, kind = PAYLOAD, b"application/octet-stream"
                else:
                    body, kind = b"missing", b"text/plain"
                response = b"HTTP/1.0 200 OK\r\nContent-Type: " + kind
                if path == b"/download-test.zip" and WITH_LENGTH:
                    response += b"\r\nContent-Length: %d" % len(body)
                response += b"\r\n\r\n"
                conn.sendall(response)
                if SHOTDIR and path == b"/download-test.zip":
                    cancel_test = CANCEL_MODE in ("--cancel-active", "--close-active")
                    chunk = 512 if cancel_test else 2048
                    delay = 2.0 if cancel_test else 0.15
                    for offset in range(0, len(body), chunk):
                        conn.sendall(body[offset:offset + chunk])
                        time.sleep(delay)
                else:
                    conn.sendall(body)
            except OSError:
                pass
            finally:
                conn.close()


def word(m, pseg, offset):
    b = m.readseg(pseg, offset, 2)
    return b[0] | b[1] << 8


def manager_rect(m, pseg, sy, S):
    """Find this package's manager window, not merely the newest slot."""
    ptr = word(m, pseg, sy["br_dl_win"])
    base = S("wm_wins") - (dispcp.KERNEL_SEG << 4)
    for slot in dispcp.win_list(m, S):
        if base + slot * dispcp.WIN_SIZE == ptr:
            return dispcp.win_rect(m, S, slot)
    raise RuntimeError("Download Manager window pointer is not in wm_wins")


# --- WHAT THE ETA FIELD SHOWS, read off the glass ----------------------------
# The completed assertion read `br_dl_disptxt`, which is not the ETA's buffer:
# it is ONE scratch line that br_dl_drawcut (the File, Source, Destination and
# Status rows), the Speed field and the ETA field each compose into and draw
# from at once (apps/browser/brdownload_paint.inc). Every painter runs on the
# UI task and draws what it composed before the next one writes, so the SCREEN
# is never wrong - but the buffer is only "the ETA" once a paint has finished,
# and br_dl_onwake writes DL_DONE and THEN repaints the whole manager. The
# row polls that state byte, so it read the buffer INSIDE the repaint whenever
# the guest was between drawcut(br_dl_url) and drawcut(br_dl_dest): one pass
# at two lanes failed `completed ETA display is b'http://10.0.2.2:8090/
# download-te'` - the Source row (docs/plans/SOAK-PARALLEL.md 17.7). The
# field's own pixels are the answer the row was asking for.
FONT_FIRST, FONT_N = 32, 95


def eta_cells(width):
    """br_dl_setetawidth's arithmetic: the cells the ETA value is drawn in."""
    n = ((width - 264) & 0xFFFF) >> 3
    return max(7, min(n, 10))


def glass_eta(m, pseg, sy, mo):
    """The ETA value as the GLASS shows it - each 8x8 cell of the field read
    off a screendump and matched against the kernel's own glyphs
    ([font_seg]:[font_base], tests/glass.py's reason), trailing blanks dropped.
    A cell no glyph matches reads '?'. The pointer is moved off the field
    first: it is drawn into the framebuffer, so it would read as ink."""
    x0 = word(m, pseg, sy["br_dl_x"]) + 200
    y0 = word(m, pseg, sy["br_dl_y"]) + word(m, pseg, sy["br_dl_yspeed"])
    n = eta_cells(word(m, pseg, sy["br_dl_w"]))
    px, py, _ = ethernet.os88qemu.mouse_where(m, S)
    if x0 - 16 <= px < x0 + n * 8 and y0 - 20 <= py < y0 + 8:
        mx, my, mw, _ = manager_rect(m, pseg, sy, S)
        mo.run("to", str(mx + mw // 2), str(my + 4))    # the title: inert
    seg = word(m, 0, S("font_seg"))
    base = word(m, 0, S("font_base"))
    raw = m.read(seg * 16 + base, FONT_N * 8)
    glyphs = {bytes(8): " "}
    for i in range(FONT_N):
        glyphs.setdefault(bytes(raw[i * 8:i * 8 + 8]), chr(FONT_FIRST + i))
    ppm = os.path.join(ethernet.os88qemu.RUNDIR, "eta.ppm")
    m.hmp('screendump "%s"' % ppm)
    w, _h, pix = shot.read_ppm(ppm)
    os.unlink(ppm)
    out = ""
    for c in range(n):
        cell = bytearray(8)
        for r in range(8):
            for b in range(8):
                o = ((y0 + r) * w + x0 + c * 8 + b) * 3
                if sum(pix[o:o + 3]) < 384:         # ink: black on white
                    cell[r] |= 0x80 >> b
        out += glyphs.get(bytes(cell), "?")
    return out.rstrip()


# ONE PATH FOR THE IMAGE THIS ROW WRITES AND READS BACK, and it is THIS
# PROCESS'S (tests/os88qemu.py RUNDIR): os88qemu.make_test boots an image
# inside RUNDIR in place and copies the shared one (ether360.img), so the
# guest never dirties a built image and the row builds nothing - where it was
# `make -B browsertest`, `make ethertest` and `make test` on build/qmp.sock,
# which kept all three brdownload rows out of every shared lane
# (docs/plans/SOAK-PARALLEL.md 17). The three share PORT, and say so in their
# `locks=`; the lease in Server is the cross-runner half.
def main():
    if SHOTDIR:
        os.makedirs(SHOTDIR, exist_ok=True)
    os88fixture.need("build/browser.o88", "build/ether360.img")
    dlimg = os.path.join(ethernet.os88qemu.RUNDIR, "brdownload360.img")
    subprocess.run(["python3", "tools/os88disk.py", "-o",
                    dlimg, "--size", "360",
                    "--folder", "SYSTEM/APPDATA",
                    os88build.at("build/browser.o88")],
                   check=True, stdout=subprocess.DEVNULL)
    ethernet.os88qemu.make_test("ETHER=1", "TESTIMG=build/ether360.img",
                                "TESTAPPS=" + dlimg)

    server = Server()
    server.start()
    m = ethernet.Qemu()
    failures = []
    try:
        ethernet.os88qemu.acted(
            m, lambda: dispcp.drive_ordinal(m, S, "B") is not None,
            secs=60, what="B: to mount as the apps drive", poll=0.4)
        esy = ethernet.ether_syms()
        dr = S("drv_tab") + ethernet.ETH_ROW * ethernet.DRVR_SZ
        ethernet.os88qemu.acted(
            m, lambda: int.from_bytes(
                m.read(dr + ethernet.DRVR_SEG, 2), "little") != 0,
            secs=60, what="ETHER.DRV to attach", poll=0.4)
        # ...TAKEN ONCE THE BOOT IS OVER AND IT HAS HELD STILL: the row's
        # segment is armed before the image is read and moves once more
        # after the desktop is up (os88qemu.kernel_up; tests/ethernet.py)
        ethernet.os88qemu.kernel_up(m, S, secs=120)
        eseg = ethernet.os88qemu.quiesce(
            m, lambda: int.from_bytes(m.read(dr + ethernet.DRVR_SEG, 2),
                                      "little"),
            secs=0.5, stable=3, what="ETHER.DRV's segment")
        ethernet.os88qemu.acted(
            m, lambda: m.readseg(eseg, esy["dhcp_st"], 1)[0]
            == ethernet.DH_BOUND, secs=60, what="DHCP to bind", poll=0.4)
        mo = ethernet.Mouse()
        dispcp.open_drive(m, mo, S, ethernet.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, ethernet.settle, wx, wy, "BROWSER.O88")
        browser_slot = dispcp.win_list(m, S)[-1]
        rec = m.read(S("wm_wins") + browser_slot * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | rec[23] << 8
        sy = brclick.browser_syms()
        nstate = sy["br_nstate"]
        ethernet.gsleep(1.0)
        ethernet.type_url(URL[7:])
        ethernet.gsleep(1.0)
        subprocess.run(["python3", "tools/qmp.py", ethernet.SOCK,
                        "sendkey ret"], check=True, capture_output=True)
        ethernet.os88qemu.acted(
            m, lambda: m.readseg(pseg, nstate, 1)[0] in (6, 7), secs=60,
            what="the download fixture page", poll=0.3)
        if m.readseg(pseg, nstate, 1)[0] != 6:
            failures.append("the fixture page did not render successfully")
        page_location = bytes(m.readseg(
            pseg, sy["br_ubuf"], 256)).split(b"\0", 1)[0]
        page_history_count = int.from_bytes(m.readseg(
            pseg, sy["br_histn"], 2), "little")

        # ...AND THE LAYOUT FINISHED (brcapacity's lesson, one page along):
        # state 6 is the fetch's, and the line table is still being built
        # behind it - a starved guest was read with the page in and no line
        # holding the link yet (docs/plans/SOAK-PARALLEL.md 17.5)
        ethernet.os88qemu.quiesce(
            m, lambda: (word(m, pseg, sy["br_nlines"]),
                        word(m, pseg, sy["br_doclen"])),
            secs=0.5, stable=3, what="the page's layout")
        # Locate the anchor from the Browser's formatted marker and line table.
        doclen = word(m, pseg, sy["br_doclen"])
        docseg = word(m, pseg, sy["br_docseg"])
        doc = m.readseg(docseg, 0, doclen)
        links = []
        for cursor in range(max(0, len(doc) - 3)):
            marker = doc[cursor:cursor + 4]
            if (marker[0] == 14
                    and all(16 <= digit <= 31 for digit in marker[1:])):
                index = ((marker[1] & 15) | ((marker[2] & 15) << 4)
                         | ((marker[3] & 15) << 8))
                links.append((cursor, index))
        if len(links) != 5:
            failures.append("the fixture should render five links; found %d"
                            % len(links))
        else:
            lines_n = word(m, pseg, sy["br_nlines"])
            table = m.readseg(word(m, pseg, sy["br_lseg"]), 0, lines_n * 10)
            u32 = lambda b: b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24
            # The download is deliberately the fifth link, after four normal
            # links. This reproduces the server index where a bad hit offset
            # can accidentally navigate to EMS-PRESSURE.HTM instead.
            link, encoded_index = links[-1]
            if encoded_index != 4:
                failures.append("download link marker has index %d, expected 4"
                                % encoded_index)
            offsets = [word(m, pseg, sy["br_lnkoff"] + i * 2)
                       for i in range(5)]
            hrefs = [bytes(m.readseg(word(m, pseg, sy["br_lnkseg"]), off, 64))
                     .split(b"\0", 1)[0] for off in offsets]
            if hrefs[-1] != b"/download-test.zip":
                failures.append("fifth href record is %r, expected /download-test.zip"
                                % hrefs[-1])
            line = next((i for i in range(lines_n)
                         if u32(table[i * 10:i * 10 + 4]) <= link + 4
                         < u32(table[i * 10 + 4:i * 10 + 8])), None)
            if line is None:
                failures.append("fixture link has no visible display line")
            else:
                start = u32(table[line * 10:line * 10 + 4])
                col = table[line * 10 + 8]
                row = m.readseg(docseg, start, link + 4 - start)
                for ch in row:
                    if ch == 14:
                        continue
                    if ch >= 0x20:
                        col += 1
                px = word(m, pseg, sy["br_cx"]) + col * 8 + 3
                py = word(m, pseg, sy["br_cy"]) + (line - word(m, pseg, sy["br_top"])) * 8 + 3
                if AUTO:
                    subprocess.run(["python3", "tools/mouse.py", ethernet.SOCK,
                                    "click", str(px), str(py)], check=True,
                                   stdout=subprocess.DEVNULL)
                    got_dialog = ethernet.os88qemu.acted(
                        m, lambda: m.readseg(
                            pseg, sy["br_dl_state"], 1)[0] == 1,
                        secs=15, what="automatic Save dialog for DOWNLOAD.ZIP",
                        poll=0.2)
                    if not got_dialog:
                        print("automatic click diagnostic: target index=%d line=%d xy=%d,%d nstate=%d requests=%r"
                              % (encoded_index, line, px, py,
                                 m.readseg(pseg, sy["br_nstate"], 1)[0],
                                 server.requests))
                    else:
                        location = bytes(m.readseg(
                            pseg, sy["br_ubuf"], 256)).split(b"\0", 1)[0]
                        if location != page_location:
                            failures.append(
                                "download action changed the page URL to %r; expected %r"
                                % (location.decode("ascii", "replace"),
                                   page_location.decode("ascii", "replace")))
                        history_count = int.from_bytes(m.readseg(
                            pseg, sy["br_histn"], 2), "little")
                        if history_count != page_history_count:
                            failures.append(
                                "download action changed history size from %d to %d"
                                % (page_history_count, history_count))
                else:
                    subprocess.run(["python3", "tools/mouse.py", ethernet.SOCK,
                                    "rclick", str(px), str(py)], check=True,
                                   stdout=subprocess.DEVNULL)
                    # the menu is OPENED by the Browser's click handler, so
                    # it is waited for rather than read the instant the
                    # tool returns, and painted before its row is clicked
                    if not ethernet.os88qemu.acted(
                            m, lambda: m.readseg(
                                pseg, sy["br_ctxopen"], 1)[0] == 1,
                            secs=15, what="the link menu", poll=0.2):
                        failures.append("right-click did not open the link menu")
                    else:
                        ethernet.settle(m)
                        mx = word(m, pseg, sy["br_cx"]) + word(
                            m, pseg, sy["br_ctxx"]) + 8
                        my = word(m, pseg, sy["br_cy"]) + word(
                            m, pseg, sy["br_ctxy"]) + 30
                        mo.click(mx, my)  # third row: Save Link As...

                if not failures:
                    # ...and the Enter goes to the CHOOSER, so it waits for
                    # one: DL_PICK is written before OSAPI_FILE_DLG opens it,
                    # and a guest second of pause was what stood in for
                    # that (a pause on QEMU's clock buys a starved guest
                    # less of the machine, docs/plans/SOAK-PARALLEL.md 17.5)
                    if not ethernet.os88qemu.acted(
                            m, lambda: word(m, 0, S("fdlg_win")) != 0,
                            secs=15, what="the Save chooser", poll=0.2):
                        failures.append("no Save file chooser opened for the link")
                    ethernet.settle(m)
                if not failures:
                    m.key("Enter")  # prefilled 8.3 name, default Save
                    if CANCEL_MODE in ("--cancel-active", "--close-active"):
                        ethernet.os88qemu.acted(
                            m,
                            lambda: (m.readseg(pseg, sy["br_dl_state"], 1)[0]
                                     in range(2, 8)
                                     and int.from_bytes(m.readseg(
                                         pseg, sy["br_dl_recvbytes"], 4),
                                         "little") > 0
                                     and int.from_bytes(m.readseg(
                                         pseg, sy["br_dl_recvbytes"], 4),
                                         "little") < len(PAYLOAD)),
                            secs=30, what="active download data before cancel",
                            poll=0.1)
                        received_before_cancel = int.from_bytes(m.readseg(
                            pseg, sy["br_dl_recvbytes"], 4), "little")
                        print("received before %s: %d / %d" % (
                            CANCEL_MODE[2:], received_before_cancel, len(PAYLOAD)))
                        if CANCEL_MODE == "--cancel-active":
                            rect = m.readseg(pseg, sy["br_dl_btnrects"], 8)
                            x1 = int.from_bytes(rect[0:2], "little")
                            y1 = int.from_bytes(rect[2:4], "little")
                            x2 = int.from_bytes(rect[4:6], "little")
                            y2 = int.from_bytes(rect[6:8], "little")
                            mo.click((x1 + x2) // 2, (y1 + y2) // 2)
                        else:
                            mx, my, mw, _ = manager_rect(m, pseg, sy, S)
                            wptr = word(m, pseg, sy["br_dl_win"])
                            wseg = dispcp.KERNEL_SEG << 4
                            close_cb = int.from_bytes(m.read(
                                wseg + wptr + 38, 2), "little")
                            if not close_cb:
                                failures.append("Download Manager has no close negotiator")
                            mo.click(mx + 13, my + 9)  # os8088 close box is at the left
                        immediate = (m.readseg(pseg, sy["br_dl_state"], 1)[0],
                                     m.readseg(pseg, sy["br_dl_cancel_req"], 1)[0],
                                     m.readseg(pseg, sy["br_dl_mgr_close_after"], 1)[0],
                                     word(m, pseg, sy["br_dl_win"]),
                                     m.readseg(pseg, sy["br_dl_fail_reason"], 1)[0],
                                     word(m, pseg, sy["br_dl_msg"]))
                        print("after input: state=%d cancel=%d close_after=%d win=%04X fail=%d msg=%04X"
                              % immediate)
                        settled = ethernet.os88qemu.acted(
                            m,
                            lambda: (word(m, pseg, sy["br_dl_win"]) == 0
                                     and m.readseg(
                                         pseg, sy["br_dl_state"], 1)[0]
                                     in (12, 13)),
                            secs=30, what="cancel cleanup and manager close",
                            poll=0.1)
                        if not settled:
                            failures.append("cancel did not delete the partial and close the manager; state=%d close_after=%d win=%04X"
                                            % (m.readseg(pseg, sy["br_dl_state"], 1)[0],
                                               m.readseg(pseg, sy["br_dl_mgr_close_after"], 1)[0],
                                               word(m, pseg, sy["br_dl_win"])))
                        if SHOTDIR:
                            os.makedirs(SHOTDIR, exist_ok=True)
                            subprocess.run(["python3", "tools/shot.py",
                                            ethernet.SOCK,
                                            os.path.join(SHOTDIR,
                                                         "download-cancelled.png")],
                                           check=True,
                                           stdout=subprocess.DEVNULL)
                        if CANCEL_MODE == "--close-active" and not m.readseg(
                                pseg, sy["br_dl_cancel_req"], 1)[0]:
                            failures.append("closing active manager did not request cancellation")
                        volume = os88flush.Volume(open(
                            dlimg, "rb").read())
                        try:
                            volume.read("DOWNLOAD.BIN")
                        except os88flush.FlushError:
                            print("cancelled partial file removed from disk")
                        else:
                            entries = ["%s (%d bytes)" % (e.name, e.size)
                                       for e in volume.listdir("")
                                       if not e.is_dir]
                            failures.append("cancel left DOWNLOAD.BIN on disk; root has %s"
                                            % ", ".join(entries))
                        m.quit()
                        if failures:
                            raise RuntimeError("; ".join(failures))
                        print("brdownload %s: ok" % CANCEL_MODE[2:])
                        return
                    active_ui = {"rate": False, "eta": False, "frames": set(),
                                 "percent": []}

                    def finished():
                        state = m.readseg(pseg, sy["br_dl_state"], 1)[0]
                        if state in range(2, 8):
                            active_ui["frames"].add(
                                m.readseg(pseg, sy["br_dl_anim"], 1)[0] & 7)
                        message = word(m, pseg, sy["br_nmsg"])
                        received = int.from_bytes(m.readseg(
                            pseg, sy["br_dl_recvbytes"], 4), "little")
                        if (WITH_LENGTH and state in range(2, 8)
                                and m.readseg(
                                    pseg, sy["br_dl_have_len"], 1)[0] == 1):
                            active_ui["percent"].append(word(
                                m, pseg, sy["br_dl_pct"]))
                        speed = int.from_bytes(m.readseg(
                            pseg, sy["br_dl_speed"], 2), "little")
                        active = (state in range(2, 8) and received > 0
                                  and speed > 0)
                        if active:
                            active_ui["rate"] = True
                            if WITH_LENGTH:
                                # the shared scratch line (glass_eta's note),
                                # which is sound HERE and only here: nothing
                                # but br_dl_eta composes a " sec" into it,
                                # and it draws what it composed before any
                                # other painter writes - so the suffix is a
                                # measured ETA having been put on the glass
                                display = bytes(m.readseg(
                                    pseg, sy["br_dl_disptxt"], 32)).split(b"\0", 1)[0]
                                if display.endswith(b" sec"):
                                    active_ui["eta"] = True
                        if (SHOTDIR and active
                                and (not WITH_LENGTH or active_ui["eta"])
                                and (not LONG_PERCENT or word(
                                    m, pseg, sy["br_dl_pct"]) >= 70)
                                and (not WITH_LENGTH or m.readseg(
                                    pseg, sy["br_dl_have_len"], 1)[0] == 1)
                                and not os.path.exists(os.path.join(
                                    SHOTDIR, "download-active.png"))):
                            subprocess.run(["python3", "tools/shot.py",
                                            ethernet.SOCK,
                                            os.path.join(SHOTDIR,
                                                         "download-active.png")],
                                           check=True,
                                           stdout=subprocess.DEVNULL)
                        return state in (11, 12, 13) or message == sy["br_dl_s_url"]

                    ethernet.os88qemu.acted(
                        m, finished, secs=180,
                        what="the streamed download to finish", poll=0.1)
                    state = m.readseg(pseg, sy["br_dl_state"], 1)[0]
                    if AUTO and state == 11:
                        location = bytes(m.readseg(
                            pseg, sy["br_ubuf"], 256)).split(b"\0", 1)[0]
                        if location != page_location:
                            failures.append(
                                "completed download left the page URL at %r; expected %r"
                                % (location.decode("ascii", "replace"),
                                   page_location.decode("ascii", "replace")))
                        history_count = int.from_bytes(m.readseg(
                            pseg, sy["br_histn"], 2), "little")
                        if history_count != page_history_count:
                            failures.append(
                                "completed download changed history size from %d to %d"
                                % (page_history_count, history_count))
                    if CANCEL_MODE == "--close-complete" and state == 11:
                        mx, my, mw, _ = manager_rect(m, pseg, sy, S)
                        mo.click(mx + 13, my + 9)  # os8088 close box is at the left
                        ethernet.os88qemu.acted(
                            m, lambda: word(m, pseg, sy["br_dl_win"]) == 0,
                            secs=10, what="completed manager to close", poll=0.1)
                        m.quit()
                        volume = os88flush.Volume(open(
                            dlimg, "rb").read())
                        if volume.read("DOWNLOAD.BIN") != PAYLOAD:
                            failures.append("closing completed manager changed the downloaded file")
                        if failures:
                            raise RuntimeError("; ".join(failures))
                        print("completed download preserved after closing manager")
                        print("brdownload close-complete: ok")
                        return
                    if SHOTDIR:
                        ethernet.gsleep(0.5)
                        subprocess.run(["python3", "tools/shot.py",
                                        ethernet.SOCK,
                                        os.path.join(SHOTDIR,
                                                     "download-complete.png")],
                                       check=True,
                                       stdout=subprocess.DEVNULL)
                    if state != 11:
                        failures.append("download ended in state %d, not complete"
                                        % state)
                        msgptr = word(m, pseg, sy["br_dl_msg"])
                        seg = word(m, pseg, sy["br_dl_partpaint"] + 2)
                        print("download diagnostic: fail=%d win=%04X msg=%04X text=%r part=%r vec=%r name=%r"
                              % (m.readseg(pseg, sy["br_dl_fail_reason"], 1)[0],
                                 word(m, pseg, sy["br_dl_win"]),
                                 msgptr,
                                 bytes(m.readseg(pseg, msgptr, 48)).split(b"\0", 1)[0]
                                 if msgptr else b"",
                                 m.readseg(pseg, sy["op_table"], 18),
                                 (seg, m.readseg(seg, 0, 6) if seg else b""),
                                 bytes(m.readseg(pseg, sy["op_name"], 13))))
                    got_length = m.readseg(pseg, sy["br_dl_have_len"], 1)[0]
                    got_total = int.from_bytes(m.readseg(
                        pseg, sy["br_dl_total"], 4), "little")
                    got_received = int.from_bytes(m.readseg(
                        pseg, sy["br_dl_recvbytes"], 4), "little")
                    # The UI's numeric formatting buffer is scratch shared by
                    # bytes, speed, and ETA; assert the authoritative counter.
                    if got_length != int(WITH_LENGTH):
                        failures.append("Content-Length presence was misreported")
                    if got_total != (len(PAYLOAD) if WITH_LENGTH else 0):
                        failures.append("download total does not match its HTTP headers")
                    if got_received != len(PAYLOAD):
                        failures.append("received-byte counter is %d, expected %d"
                                        % (got_received, len(PAYLOAD)))
                    got_speed = int.from_bytes(m.readseg(
                        pseg, sy["br_dl_speed"], 2), "little")
                    display = None
                    if WITH_LENGTH and state == 11:
                        # ...AT A MOMENT THE GUEST SAYS IS AFTER THE PAINT:
                        # DL_DONE is written before br_dl_onwake's repaint,
                        # so the UI task finishing with it comes first, and
                        # the glass is then read until the field holds - a
                        # timer pass still armed from the transfer repaints
                        # it once more, and a screendump inside that white
                        # fill is a blank and not an answer
                        ethernet.settle(m)
                        seen = [None]

                        def eta_shown():
                            seen[0] = glass_eta(m, pseg, sy, mo)
                            return seen[0] == "0 sec"
                        ethernet.os88qemu.acted(
                            m, eta_shown, secs=10, poll=0.3,
                            what="the completed ETA on the glass")
                        display = seen[0]
                    if got_speed < 1 or got_speed > 999:
                        failures.append("completed download speed is %d KiB/s, expected 1..999"
                                        % got_speed)
                    if not active_ui["rate"]:
                        failures.append("speed was never calculated while the download was active")
                    if WITH_LENGTH and not active_ui["eta"]:
                        failures.append("active ETA was never rendered from the measured transfer")
                    if SHOTDIR and active_ui["frames"] != set(range(8)):
                        failures.append("animation frames observed %r, expected 0..7"
                                        % sorted(active_ui["frames"]))
                    elif SHOTDIR:
                        print("animation frames observed: 0..7")
                    if WITH_LENGTH and state == 11 and display != "0 sec":
                        failures.append("the completed ETA field shows %r on "
                                        "the glass, expected '0 sec'"
                                        % display)
                    barx = word(m, pseg, sy["br_dl_barx"])
                    barx2 = word(m, pseg, sy["br_dl_barx2"])
                    winx = word(m, pseg, sy["br_dl_x"])
                    winw = word(m, pseg, sy["br_dl_w"])
                    print("progress bounds: x=%d..%d, content=%d..%d, percent=%d"
                          % (barx, barx2, winx, winx + winw,
                             word(m, pseg, sy["br_dl_pct"])))
                    if barx != winx + 24 or barx2 != winx + winw - 48:
                        failures.append("progress bar bounds %d..%d escape content %d..%d"
                                        % (barx, barx2, winx, winx + winw))
                    if winw > 440:
                        failures.append("download window is wider than compact target: %d"
                                        % winw)
                    if WITH_LENGTH and word(m, pseg, sy["br_dl_pct"]) != 100:
                        failures.append("completed progress bar is not at 100%%")
                    if WITH_LENGTH:
                        samples = active_ui["percent"]
                        if any(p > 100 for p in samples):
                            failures.append("live percentage exceeded 100%%: %r"
                                            % samples)
                        if any(a > b for a, b in zip(samples, samples[1:])):
                            failures.append("live percentage moved backwards: %r"
                                            % samples)
                        print("live percent samples: %s"
                              % (samples if samples else "none"))
                    if AUTO and state == 11 and not failures:
                        # A completed file must not become the page's Reload
                        # target. Close the manager, reload the visible page,
                        # and require a fresh index request rather than another
                        # Save dialog for DOWNLOAD.ZIP.
                        mx, my, _, _ = manager_rect(m, pseg, sy, S)
                        mo.click(mx + 13, my + 9)
                        ethernet.os88qemu.acted(
                            m, lambda: word(m, pseg, sy["br_dl_win"]) == 0,
                            secs=10, what="completed manager to close",
                            poll=0.1)
                        before = sum(1 for req in server.requests
                                     if req.startswith(b"GET /index.htm "))
                        reload_rect = [word(m, pseg, sy["br_r3"] + i * 2)
                                       for i in range(4)]
                        mo.click((reload_rect[0] + reload_rect[2]) // 2,
                                 (reload_rect[1] + reload_rect[3]) // 2)
                        reloaded = ethernet.os88qemu.acted(
                            m,
                            lambda: (sum(1 for req in server.requests
                                         if req.startswith(b"GET /index.htm "))
                                     > before
                                     and m.readseg(pseg, nstate, 1)[0] == 6),
                            secs=30, what="Reload to refetch the visible page",
                            poll=0.2)
                        if not reloaded:
                            failures.append(
                                "Reload after a completed download did not refetch the visible page")
                        if (word(m, pseg, sy["br_dl_win"]) != 0
                                or m.readseg(pseg, sy["br_dl_state"], 1)[0] != 11):
                            failures.append(
                                "Reload after a completed download reopened the Download Manager")
                        location = bytes(m.readseg(
                            pseg, sy["br_ubuf"], 256)).split(b"\0", 1)[0]
                        if location != page_location:
                            failures.append(
                                "Reload after download changed page URL to %r"
                                % location.decode("ascii", "replace"))
                        else:
                            print("page URL and history preserved; Reload refetched index")
                    print("download rate: %d KiB/s" % got_speed)
    finally:
        m.quit()
        try:
            server.sock.close()
        except OSError:
            pass

    try:
        volume = os88flush.Volume(open(
            dlimg, "rb").read())
        got = volume.read("DOWNLOAD.BIN")
        if got != PAYLOAD:
            first = next((i for i, (a, b) in enumerate(zip(got, PAYLOAD))
                          if a != b), min(len(got), len(PAYLOAD)))
            failures.append("DOWNLOAD.BIN differs at byte %d: got %d bytes, expected %d"
                            % (first, len(got), len(PAYLOAD)))
        else:
            print("exact %s file verified: %d bytes" % (
                "known-length" if WITH_LENGTH else "unknown-length", len(got)))
    except Exception as exc:
        failures.append("could not read back DOWNLOAD.BIN: %s" % exc)
    if b"GET /index.htm " not in b"\n".join(server.requests):
        failures.append("the NE2000 server did not receive the index request")
    if b"GET /download-test.zip " not in b"\n".join(server.requests):
        failures.append("the NE2000 server did not receive the payload request; requests=%r"
                        % server.requests)
    for failure in failures:
        print("FAIL: " + failure)
    print("brdownload: %s" % ("FAILED" if failures else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
