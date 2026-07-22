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
    return files


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
                    b2, b3 = data[src], data[src + 1]
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


def compress(data: bytes) -> bytes:
    out = bytearray()
    out.append(0x11)
    out.append(len(data) & 0xFF)
    out.append((len(data) >> 8) & 0xFF)
    out.append((len(data) >> 16) & 0xFF)
    pos = 0
    n = len(data)
    while pos < n:
        flag_i = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if pos >= n:
                break
            best_len = 0
            best_disp = 0
            start = max(0, pos - 0x1000)
            max_len = min(0x10110, n - pos)
            if max_len >= 3:
                s = start
                while s < pos:
                    length = 0
                    while length < max_len and data[s + length] == data[pos + length]:
                        length += 1
                    if length >= 3 and length > best_len:
                        best_len = length
                        best_disp = pos - s - 1
                        if best_len >= 18:
                            break
                    s += 1
            if best_len >= 3:
                flags |= 0x80 >> bit
                disp = best_disp
                length = best_len
                if length < 0x11:
                    typ = length - 1
                    out.append(((typ << 4) | ((disp >> 8) & 0xF)) & 0xFF)
                    out.append(disp & 0xFF)
                elif length < 0x111:
                    nn = length - 0x11
                    out.append((nn >> 4) & 0xF)
                    out.append((((nn & 0xF) << 4) | ((disp >> 8) & 0xF)) & 0xFF)
                    out.append(disp & 0xFF)
                else:
                    nn = length - 0x111
                    out.append(0x10 | ((nn >> 12) & 0xF))
                    out.append((nn >> 4) & 0xFF)
                    out.append((((nn & 0xF) << 4) | ((disp >> 8) & 0xF)) & 0xFF)
                    out.append(disp & 0xFF)
                pos += length
            else:
                out.append(data[pos])
                pos += 1
        out[flag_i] = flags
    return bytes(out)


def main():
    p = Path("local/dumps/romfs/000400000011C500/a/0/1/3")
    files = open_garc(p.read_bytes())
    fit = fail = rt_ok = 0
    for i, start, end, length, slot, blob in files:
        if i == 0:
            continue
        if i > 100:
            break
        raw = lz11_decompress(blob)
        raw_b = bytearray(raw)
        if len(raw_b) > 0x20:
            off = struct.unpack_from("<I", raw_b, 0x10)[0] + 0xE
            if 0 <= off < len(raw_b) - 1:
                raw_b[off] = (raw_b[off] + 1) & 0xFF
        raw = bytes(raw_b)
        c = compress(raw)
        d = lz11_decompress(c)
        if d != raw:
            print("roundtrip fail", i, len(raw), len(c), len(d))
            fail += 1
            if fail > 3:
                break
            continue
        rt_ok += 1
        if len(c) <= slot:
            fit += 1
        else:
            fail += 1
            if fail <= 8:
                print(f"nofit f{i} c={len(c)} slot={slot} raw={len(raw)} orig={length}")
    print("rt_ok", rt_ok, "fit", fit, "fail", fail)
    i, start, end, length, slot, blob = files[1]
    raw = lz11_decompress(blob)
    c = compress(raw)
    print(
        "EN pack orig",
        length,
        "slot",
        slot,
        "recomp",
        len(c),
        "fit",
        len(c) <= slot,
        "rt",
        lz11_decompress(c) == raw,
    )


if __name__ == "__main__":
    main()
