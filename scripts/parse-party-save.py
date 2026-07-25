#!/usr/bin/env python3
"""Find valid PK6 mons in an ORAS main save (party / box)."""
import struct
import sys

positions = [
    [0, 1, 2, 3], [0, 1, 3, 2], [0, 2, 1, 3], [0, 3, 1, 2],
    [0, 2, 3, 1], [0, 3, 2, 1], [1, 0, 2, 3], [1, 0, 3, 2],
    [2, 0, 1, 3], [3, 0, 1, 2], [2, 0, 3, 1], [3, 0, 2, 1],
    [1, 2, 0, 3], [1, 3, 0, 2], [2, 1, 0, 3], [3, 1, 0, 2],
    [2, 3, 0, 1], [3, 2, 0, 1], [1, 2, 3, 0], [1, 3, 2, 0],
    [2, 1, 3, 0], [3, 1, 2, 0], [2, 3, 1, 0], [3, 2, 1, 0],
]


def try_pk6(b: bytes, off: int):
    if off + 232 > len(b):
        return None
    pk = bytearray(b[off : off + 232])
    ec = struct.unpack_from("<I", pk, 0)[0]
    if ec == 0:
        return None
    chk = struct.unpack_from("<H", pk, 6)[0]
    seed = ec
    for i in range(8, 232, 2):
        seed = (0x41C64E6D * seed + 0x6073) & 0xFFFFFFFF
        r = (seed >> 16) & 0xFFFF
        pk[i] ^= r & 0xFF
        pk[i + 1] ^= (r >> 8) & 0xFF
    sv = (ec >> 13) & 31
    order = positions[sv % 24]
    blocks = [bytes(pk[8 + i * 56 : 8 + (i + 1) * 56]) for i in range(4)]
    un = [None] * 4
    for i in range(4):
        un[order[i]] = blocks[i]
    for i in range(4):
        pk[8 + i * 56 : 8 + (i + 1) * 56] = un[i]
    s = sum(struct.unpack_from("<H", pk, i)[0] for i in range(8, 232, 2)) & 0xFFFF
    if s != chk:
        return None
    sp = struct.unpack_from("<H", pk, 8)[0]
    if 1 <= sp <= 721:
        return sp
    return None


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else r"C:\hoenn-forge\local\saves\latest\sdmc-title\main"
    b = open(path, "rb").read()
    print(f"size={len(b)}")
    found = []
    for i in range(0, len(b) - 232, 4):
        sp = try_pk6(b, i)
        if sp:
            found.append((i, sp))
    print(f"valid PK6: {len(found)}")
    offs = {f[0] for f in found}
    spmap = dict(found)
    runs = []
    for o, sp in found:
        if (o - 0x104) in offs:
            continue
        run = [(o, sp)]
        n = o + 0x104
        while n in offs:
            run.append((n, spmap[n]))
            n += 0x104
        runs.append(run)
    runs.sort(key=len, reverse=True)
    for run in runs[:20]:
        print(f"run n={len(run)} @0x{run[0][0]:X}: {[x[1] for x in run]}")
    print("--- fixed probes ---")
    for base in (0x14200, 0x19600, 0x1C600, 0x33000):
        print(f"base 0x{base:X}:")
        for s in range(6):
            off = base + s * 0x104
            print(f"  slot{s} @0x{off:X}: {try_pk6(b, off)}")
        if base + 4 <= len(b):
            print(f"  u32@base={struct.unpack_from('<I', b, base)[0]}")


if __name__ == "__main__":
    main()
