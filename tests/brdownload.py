#!/usr/bin/env python3
"""Download a binary response through Save Link As on the emulated NE2000.

    make && make ethertest && make browsertest && python3 tests/brdownload.py

The small index page links to a 32 KiB binary body. By default the host omits
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
import os88flush                                    # noqa: E402
import os88sym                                      # noqa: E402

S = os88sym.linear
PORT = 8090
URL = "http://10.0.2.2:%d/index.htm" % PORT
WITH_LENGTH = "--content-length" in sys.argv
AUTO = "--automatic" in sys.argv
CANCEL_MODE = next((arg for arg in ("--cancel-active", "--close-active",
                                    "--close-complete")
                    if arg in sys.argv), None)
SHOTDIR = next((arg.split("=", 1)[1] for arg in sys.argv
                if arg.startswith("--shots=")), None)
PAYLOAD = bytes(((i * 37 + (i >> 5) + 11) & 255) for i in range(32771))
PAGE = (b"<html><head><title>Download test</title></head><body>"
        b"<a href=\"http://10.0.2.2:8090/payload.bin\">payload.bin</a>"
        b"</body></html>")


class Server(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
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
                elif path == b"/payload.bin":
                    body, kind = PAYLOAD, b"application/octet-stream"
                else:
                    body, kind = b"missing", b"text/plain"
                response = b"HTTP/1.0 200 OK\r\nContent-Type: " + kind
                if path == b"/payload.bin" and WITH_LENGTH:
                    response += b"\r\nContent-Length: %d" % len(body)
                response += b"\r\n\r\n"
                conn.sendall(response)
                if SHOTDIR and path == b"/payload.bin":
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


def main():
    if SHOTDIR:
        os.makedirs(SHOTDIR, exist_ok=True)
    subprocess.run(["make", "-B", "browsertest"], check=True,
                   stdout=subprocess.DEVNULL)
    if os.path.exists(os88build.at("build/ether360.img")):
        os.remove(os88build.at("build/ether360.img"))
    subprocess.run(["make", "ethertest"], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["python3", "tools/os88disk.py", "-o",
                    "build/brdownload360.img", "--size", "360",
                    "--folder", "SYSTEM/APPDATA", "build/browser.o88"],
                   check=True, stdout=subprocess.DEVNULL)
    for filename in (os88build.at("build/qmp.sock"),
                     os88build.at("build/qemu.pid")):
        if os.path.exists(filename):
            os.remove(filename)
    ethernet.os88qemu.own()
    subprocess.run(["make", "test", "ETHER=1", "TESTIMG=build/ether360.img",
                    "TESTAPPS=build/brdownload360.img"], check=True,
                   stdout=subprocess.DEVNULL)

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
        eseg = int.from_bytes(m.read(dr + ethernet.DRVR_SEG, 2), "little")
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
        if len(links) != 1:
            failures.append("the fixture should render exactly one link")
        else:
            lines_n = word(m, pseg, sy["br_nlines"])
            table = m.readseg(word(m, pseg, sy["br_lseg"]), 0, lines_n * 10)
            u32 = lambda b: b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24
            link, encoded_index = links[0]
            if encoded_index != 0:
                failures.append("fixture link marker has index %d, expected 0"
                                % encoded_index)
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
                    ethernet.os88qemu.acted(
                        m, lambda: m.readseg(
                            pseg, sy["br_dl_state"], 1)[0] == 1,
                        secs=15, what="automatic Save dialog for payload.bin",
                        poll=0.2)
                else:
                    subprocess.run(["python3", "tools/mouse.py", ethernet.SOCK,
                                    "rclick", str(px), str(py)], check=True,
                                   stdout=subprocess.DEVNULL)
                    if m.readseg(pseg, sy["br_ctxopen"], 1)[0] != 1:
                        failures.append("right-click did not open the link menu")
                    else:
                        mx = word(m, pseg, sy["br_cx"]) + word(
                            m, pseg, sy["br_ctxx"]) + 8
                        my = word(m, pseg, sy["br_cy"]) + word(
                            m, pseg, sy["br_ctxy"]) + 30
                        mo.click(mx, my)  # third row: Save Link As...
                        ethernet.gsleep(1.0)

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
                            os88build.at("build/brdownload360.img"), "rb").read())
                        try:
                            volume.read("PAYLOAD.BIN")
                        except os88flush.FlushError:
                            print("cancelled partial file removed from disk")
                        else:
                            entries = ["%s (%d bytes)" % (e.name, e.size)
                                       for e in volume.listdir("")
                                       if not e.is_dir]
                            failures.append("cancel left PAYLOAD.BIN on disk; root has %s"
                                            % ", ".join(entries))
                        m.quit()
                        if failures:
                            raise RuntimeError("; ".join(failures))
                        print("brdownload %s: ok" % CANCEL_MODE[2:])
                        return
                    active_ui = {"rate": False, "eta": False, "frames": set()}

                    def finished():
                        state = m.readseg(pseg, sy["br_dl_state"], 1)[0]
                        if state in range(2, 8):
                            active_ui["frames"].add(
                                m.readseg(pseg, sy["br_dl_anim"], 1)[0] & 7)
                        message = word(m, pseg, sy["br_nmsg"])
                        received = int.from_bytes(m.readseg(
                            pseg, sy["br_dl_recvbytes"], 4), "little")
                        speed = int.from_bytes(m.readseg(
                            pseg, sy["br_dl_speed"], 2), "little")
                        active = (state in range(2, 8) and received > 0
                                  and speed > 0)
                        if active:
                            active_ui["rate"] = True
                            if WITH_LENGTH:
                                display = bytes(m.readseg(
                                    pseg, sy["br_dl_disptxt"], 32)).split(b"\0", 1)[0]
                                if display.endswith(b" sec"):
                                    active_ui["eta"] = True
                        if (SHOTDIR and active
                                and (not WITH_LENGTH or active_ui["eta"])
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
                    if CANCEL_MODE == "--close-complete" and state == 11:
                        mx, my, mw, _ = manager_rect(m, pseg, sy, S)
                        mo.click(mx + 13, my + 9)  # os8088 close box is at the left
                        ethernet.os88qemu.acted(
                            m, lambda: word(m, pseg, sy["br_dl_win"]) == 0,
                            secs=10, what="completed manager to close", poll=0.1)
                        m.quit()
                        volume = os88flush.Volume(open(
                            os88build.at("build/brdownload360.img"), "rb").read())
                        if volume.read("PAYLOAD.BIN") != PAYLOAD:
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
                    display = bytes(m.readseg(
                        pseg, sy["br_dl_disptxt"], 32)).split(b"\0", 1)[0]
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
                    if WITH_LENGTH and display != b"0 sec":
                        failures.append("completed ETA display is %r, expected '0 sec'"
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
                    print("download rate: %d KiB/s" % got_speed)
    finally:
        m.quit()
        try:
            server.sock.close()
        except OSError:
            pass

    try:
        volume = os88flush.Volume(open(
            os88build.at("build/brdownload360.img"), "rb").read())
        got = volume.read("PAYLOAD.BIN")
        if got != PAYLOAD:
            first = next((i for i, (a, b) in enumerate(zip(got, PAYLOAD))
                          if a != b), min(len(got), len(PAYLOAD)))
            failures.append("PAYLOAD.BIN differs at byte %d: got %d bytes, expected %d"
                            % (first, len(got), len(PAYLOAD)))
        else:
            print("exact %s file verified: %d bytes" % (
                "known-length" if WITH_LENGTH else "unknown-length", len(got)))
    except Exception as exc:
        failures.append("could not read back PAYLOAD.BIN: %s" % exc)
    if b"GET /index.htm " not in b"\n".join(server.requests):
        failures.append("the NE2000 server did not receive the index request")
    if b"GET /payload.bin " not in b"\n".join(server.requests):
        failures.append("the NE2000 server did not receive the payload request")
    for failure in failures:
        print("FAIL: " + failure)
    print("brdownload: %s" % ("FAILED" if failures else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
