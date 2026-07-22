#!/usr/bin/env python3
from pathlib import Path
import struct

def open_garc(data: bytes):
    assert data[:4] == b"CRAG"
    header_size = struct.unpack_from("<I", data, 4)[0]
    data_off = struct.unpack_from("<I", data, 16)[0]
    pos = header_size
    entry_count = struct.unpack_from("<H", data, pos + 8)[0]
    pos += 12 + entry_count * 4
    count = struct.unpack_from("<I", data, pos + 8)[0]
    pos += 12
    files = []
    for i in range(count):
        vector = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        first = None
        for _ in range(32):
            exists = vector & 1
            vector >>= 1
            if exists:
                start, end, length = struct.unpack_from("<III", data, pos)
                pos += 12
                if first is None:
                    first = (start, end, length)
        if first:
            start, end, length = first
            slot = end - start
            blob = data[data_off + start : data_off + start + length]
            files.append((i, start, end, length, slot, blob))
    return data_off, files


def lz11_decompress(data: bytes) -> bytes:
    if not data or data[0] != 0x11:
        return data
    src = 4
    size = data[1] | (data[2] << 8) | (data[3] << 16)
    if size == 0:
        size = struct.unpack_from("<I", data, 4)[0]
        src = 8
    out = bytearray(size)
    dst = 0
    while dst < size and src < len(data):
        flags = data[src]
        src += 1
        for i in range(8):
            if dst >= size:
                break
            if flags & (0x80 >> i):
                b1 = data[src]
                src += 1
                typ = b1 >> 4
                if typ == 0:
                    b2 = data[src]
                    b3 = data[src + 1]
                    src += 2
                    length = (((b1 & 0xF) << 4) | (b2 >> 4)) + 0x11
                    disp = ((b2 & 0xF) << 8) | b3
                elif typ == 1:
                    b2, b3, b4 = data[src], data[src + 1], data[src + 2]
                    src += 3
                    length = (((b1 & 0xF) << 12) | (b2 << 4) | (b3 >> 4)) + 0x111
                    disp = ((b3 & 0xF) << 8) | b4
                else:
                    b2 = data[src]
                    src += 1
                    length = typ + 1
                    disp = ((b1 & 0xF) << 8) | b2
                d = disp + 1
                for _ in range(length):
                    if dst >= size:
                        break
                    out[dst] = out[dst - d]
                    dst += 1
            else:
                out[dst] = data[src]
                dst += 1
                src += 1
    return bytes(out)


def main():
    candidates = [
        Path("local/dumps/romfs/000400000011C500/a/0/1/3"),
        Path("local/mods-pull/romfs/a/0/1/3"),
    ]
    p = next(c for c in candidates if c.exists())
    print("using", p, p.stat().st_size)
    data = p.read_bytes()
    _, files = open_garc(data)
    print("file count", len(files))
    fit = grow = 0
    for i, start, end, length, slot, blob in files:
        raw = lz11_decompress(blob)
        is_lz = bool(blob) and blob[0] == 0x11
        ok = len(raw) <= slot
        if i < 6 or i in (1, 2, 3, 10, 50):
            print(
                f"f{i}: len={length} slot={slot} lz={is_lz} raw={len(raw)} fit={ok} head={blob[:4].hex()}"
            )
        if i >= 2:
            if ok:
                fit += 1
            else:
                grow += 1
    print("maps fit uncompressed", fit, "need grow", grow)
    # decStorage pointers
    _, _, _, _, _, blob = files[1]
    raw = lz11_decompress(blob)
    print("decStorage raw", len(raw), "lz", blob[0] == 0x11)
    for f in range(min(12, len(raw) // 4)):
        ptr = struct.unpack_from("<I", raw, f * 4)[0]
        print(f"  ptr[{f}]={ptr:#x}")


if __name__ == "__main__":
    main()
