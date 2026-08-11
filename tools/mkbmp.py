import struct, sys, random
w, h, out, kind = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
stride = ((w + 1) // 2 + 3) & ~3
pal = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),
       (170,85,0),(170,170,170),(85,85,85),(85,85,255),(85,255,85),
       (85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
rows = []
random.seed(1234)
for y in range(h):
    px = []
    x = 0
    while x < w:
        if kind == "noise":                 # runs of 3-8 px, 16 colours
            n = random.randint(3, 8); c = random.randint(0, 15)
        elif kind == "fine":                # runs of 1-2 px: the worst case
            n = random.randint(1, 2); c = random.randint(0, 15)
        else:                               # "flat": one run a row
            n = w; c = 15
        for _ in range(min(n, w - x)):
            px.append(c)
        x += n
    row = bytearray(stride)
    for i in range(0, w, 2):
        hi = px[i] & 15
        lo = px[i + 1] & 15 if i + 1 < w else 0
        row[i // 2] = (hi << 4) | lo
    rows.append(bytes(row))
runs = 0
for y in range(h):
    prev, r = None, 0
    for i in range(w):
        c = (rows[y][i // 2] >> 4) if i % 2 == 0 else (rows[y][i // 2] & 15)
        if c != prev: r += 1; prev = c
    runs += r
img = b"".join(reversed(rows))               # bottom-up
hdr = (b"BM" + struct.pack("<IHHI", 118 + len(img), 0, 0, 118)
       + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0, len(img), 0, 0, 16, 0))
for r_, g_, b_ in pal:
    hdr += bytes((b_, g_, r_, 0))
open(out, "wb").write(hdr + img)
print("%s %dx%d stride %d  %d bytes  %.1f runs/row (%d total)"
      % (out, w, h, stride, 118 + len(img), runs / float(h), runs))
