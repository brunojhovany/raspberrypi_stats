"""Turn preview.cpp output into a PNG (stdlib only).

Usage: render.py in.txt out.png [name-prefix,...]
Optional prefixes keep only matching screens, e.g. "eva " or "saver".
"""
import sys, zlib, struct
blocks, cur = [], None
for line in open(sys.argv[1]):
    line = line.rstrip("\n")
    if line.startswith("SCREEN "):
        cur = (line[7:], []); blocks.append(cur)
    elif cur and line:
        cur[1].append(line)
if len(sys.argv) > 3:
    keep = sys.argv[3].split(",")
    blocks = [b for b in blocks if any(b[0].startswith(k) for k in keep)]
S, GAP, PAD = 5, 24, 16          # pixel scale, gap between screens, border
ON, OFF, BG = (255, 160, 20), (18, 10, 4), (0, 0, 0)
W = PAD * 2 + len(blocks) * 32 * S + (len(blocks) - 1) * GAP
H = PAD * 2 + 128 * S
img = [[BG] * W for _ in range(H)]
for i, (_, rows) in enumerate(blocks):
    ox = PAD + i * (32 * S + GAP)
    for y, r in enumerate(rows):
        for x, v in enumerate(r):
            col = ON if v == "1" else OFF
            for dy in range(S - 1):        # leave a 1px grid line like real OLED dots
                for dx in range(S - 1):
                    img[PAD + y * S + dy][ox + x * S + dx] = col
raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in img)
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
print("screens:", ", ".join(n for n, _ in blocks))
