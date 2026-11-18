#!/usr/bin/env python3
"""raw ext4 -> Android sparse image (raw / fill / dont-care chunks)."""
import struct, sys

src, dst = sys.argv[1], sys.argv[2]
BLK = 4096
data = open(src, 'rb').read()
assert len(data) % BLK == 0
nblk = len(data) // BLK

chunks = []          # (type, nblocks, payload)
i = 0
while i < nblk:
    blk = data[i * BLK:(i + 1) * BLK]
    if blk == b'\0' * BLK:
        j = i
        while j < nblk and data[j * BLK:(j + 1) * BLK] == blk:
            j += 1
        chunks.append((0xCAC3, j - i, b''))
        i = j
        continue
    j = i
    while j < nblk and data[j * BLK:(j + 1) * BLK] != b'\0' * BLK:
        j += 1
    body = data[i * BLK:j * BLK]
    chunks.append((0xCAC1, j - i, body))
    i = j

hdr = struct.pack('<IHHHHIIII', 0xED26FF3A, 1, 0, 28, 12, BLK, nblk, len(chunks), 0)
with open(dst, 'wb') as f:
    f.write(hdr)
    for typ, n, payload in chunks:
        total = 12 + len(payload)
        f.write(struct.pack('<HHII', typ, 0, n, total))
        if payload:
            f.write(payload)
print(f"{dst}: {nblk} blocks, {len(chunks)} chunks")
