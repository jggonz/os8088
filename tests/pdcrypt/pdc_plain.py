#!/usr/bin/env python3
"""
os8088 - tests/pdcrypt/pdc_plain.py       RAW_COPY PDRC_PLAIN on encrypted documents (PAGEDESK-SPEC 20.22.5)

For every fixture of tools/os88pdfx_crypt.py, the real PDENG + PDXK (host) copy every object with
PDRC_PLAIN in chunks of 1, 37 and 4096 bytes, and:
  * the three chunkings give the SAME bytes (the verb is resumable at any `done`);
  * a non-stream object comes out as the file's own bytes, with PDRC_STRCIPHER set (and clear for the
    /Encrypt dictionary itself);
  * a stream whose data the document encrypts comes out as the file's own header with /Length's value
    replaced, `stream`, the data, `\nendstream\nendobj\n`; the data equals `qpdf --show-object=N
    --raw-stream-data` of the `qpdf --decrypt` file (decrypted, filters kept), and /Length is its length;
  * a stream the key does not apply to (/Metadata with EncryptMetadata false) is the file's bytes.

    python3 tests/pdcrypt/pdc_plain.py [E2E_BINARY] [WORKDIR]
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
E2E = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "pdc_e2e")
WORK = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "pdcrypt", "e2e")
fails = 0

def fail(m):
    global fails
    fails += 1
    print("FAIL", m)

def plain(path, pw, out, chunk):
    os.makedirs(out, exist_ok=True)
    p = subprocess.run([E2E, "plain", path, pw or "-", out, str(chunk)], capture_output=True, text=True, timeout=600)
    lines = {}
    for l in p.stdout.splitlines():
        m = re.match(r"PLAIN (\d+) bytes=(\d+) crc=[0-9a-f]+ flag=(\d)", l)
        if m: lines[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
        elif l.startswith("PLAIN"):
            m = re.match(r"PLAIN (\d+) rc=(\d+)", l)
            lines[int(m.group(1))] = ("rc", int(m.group(2)))
    return lines

def obj_bytes(data, n):
    m = re.search(rb"(?<![0-9])%d 0 obj" % n, data)
    if not m: return None
    s = m.start()
    # a stream object: through the end of "stream" + its EOL; else through endobj
    e = data.index(b"endobj", s) + 6
    return data[s:e]

def main():
    n = 0
    for line in open(os.path.join(WORK, "MANIFEST.TXT")):
        name, base, upw, opw, desc = line.split()
        upw = "" if upw == "-" else upw
        path = os.path.join(WORK, name)
        orig = open(path, "rb").read()
        dec = os.path.join(WORK, "plain_" + name)
        subprocess.check_call(["qpdf", "--password=" + upw, "--decrypt", "--static-id", path, dec])
        outs = {}
        for chunk in (1, 37, 4096):
            d = os.path.join(WORK, "pl_%s_%d" % (name, chunk))
            outs[chunk] = (d, plain(path, upw, d, chunk))
        d1, l1 = outs[4096]
        enc_obj = None
        m = re.search(rb"/Encrypt (\d+) 0 R", orig)
        if m: enc_obj = int(m.group(1))
        nstream = 0
        for o, (nb, flag) in sorted(l1.items()):
            if nb == "rc":
                if flag != 53 and flag != 1: fail("%s obj %d: rc %d" % (name, o, flag))   # a free object: NOTFOUND
                continue
            blob = open(os.path.join(d1, "%d.obj" % o), "rb").read()
            for chunk in (1, 37):
                if open(os.path.join(outs[chunk][0], "%d.obj" % o), "rb").read() != blob:
                    fail("%s obj %d: chunk %d differs from 4096" % (name, o, chunk))
            n += 1
            want_flag = 0 if o == enc_obj else 1
            if flag != want_flag: fail("%s obj %d: PDRC_STRCIPHER %d, wanted %d" % (name, o, flag, want_flag))
            src = obj_bytes(orig, o)
            if src is None: fail("%s obj %d: not found in the file" % (name, o)); continue
            if b"stream" in src and re.search(rb">>\s*stream\r?\n", src):
                m = re.search(rb"(.*?>>\s*stream\r?\n)(.*)endstream", src, re.S)
                head = m.group(1)
                raw = subprocess.run(["qpdf", "--password=" + upw, "--show-object=%d" % o, "--raw-stream-data", path], capture_output=True).stdout
                if blob == src:                                   # a stream the key does not apply to: the file's bytes
                    if b"/Type /Metadata" not in src and b"/Type/Metadata" not in src: fail("%s obj %d: stream copied exactly but not Metadata" % (name, o))
                    continue
                tail = b"\nendstream\nendobj\n"
                if not blob.endswith(tail): fail("%s obj %d: tail" % (name, o)); continue
                body = blob[:-len(tail)]
                mm = re.search(rb"(.*?)/Length (\d+)(.*?stream\r?\n)(.*)", body, re.S)
                if not mm: fail("%s obj %d: no /Length in the output" % (name, o)); continue
                data = mm.group(4)
                if int(mm.group(2)) != len(data): fail("%s obj %d: /Length %s but %d bytes" % (name, o, mm.group(2).decode(), len(data)))
                if data != raw: fail("%s obj %d: data differs from qpdf's decrypted raw stream (%d vs %d bytes)" % (name, o, len(data), len(raw)))
                oh = re.sub(rb"/Length (\d+)", b"/Length N", head, count=1)
                nh = re.sub(rb"/Length (\d+)", b"/Length N", blob[:len(blob) - len(data) - len(tail)], count=1)
                if oh != nh: fail("%s obj %d: header differs: %r vs %r" % (name, o, oh[-60:], nh[-60:]))
                nstream += 1
            elif blob != src:
                fail("%s obj %d: a non-stream object differs from the file's bytes" % (name, o))
        if not nstream and "NM" not in name: fail("%s: no encrypted stream was compared" % name)
        print("ok   %-12s %2d objects, %d decrypted streams, three chunkings agree" % (name, len(l1), nstream))
        n += 1
    print("pdc_plain: %d checks, %d failures" % (n, fails))
    return 1 if fails else 0

sys.exit(main())
