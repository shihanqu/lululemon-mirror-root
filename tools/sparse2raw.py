#!/usr/bin/env python3
import struct, sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
magic, major, minor, fhs, chs, blk, total_blks, total_chunks, csum = struct.unpack_from('<IHHHHIIII', data, 0)
assert magic == 0xED26FF3A, hex(magic)
off = fhs
with open(dst, 'wb') as f:
    for _ in range(total_chunks):
        ctype, _, csz, tsz = struct.unpack_from('<HHII', data, off)
        body = data[off + chs:off + tsz]
        n = csz * blk
        if ctype == 0xCAC1:
            assert len(body) == n
            f.write(body)
        elif ctype == 0xCAC2:
            pad = (blk - len(body) % blk) % blk
            f.write((body + b'\0' * pad) * (n // blk))
        else:
            f.write(b'\0' * n)
        off += tsz
print(f"wrote {dst} {total_blks * blk} bytes, {total_chunks} chunks")
