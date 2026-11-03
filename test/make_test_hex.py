#!/usr/bin/env python3
"""Generate Intel HEX test files and the matching expected flash images.

Usage: make_test_hex.py OUTDIR
"""
import os
import random
import sys

FLASH = 512 * 1024
BLOCK = 512


def rec(addr16, rtype, data=b""):
    b = bytes([len(data), (addr16 >> 8) & 0xFF, addr16 & 0xFF, rtype]) + data
    cs = (-sum(b)) & 0xFF
    return ":" + (b + bytes([cs])).hex().upper()


def to_hex(segments, rec_len=16, lower_every=0):
    """segments: list of (address, bytes) in the order they are emitted."""
    lines = []
    upper = None
    n = 0
    for addr, data in segments:
        off = 0
        while off < len(data):
            a = addr + off
            if (a >> 16) != upper:
                upper = a >> 16
                lines.append(rec(0, 0x04, bytes([upper >> 8, upper & 0xFF])))
            chunk = min(rec_len, len(data) - off, 0x10000 - (a & 0xFFFF))
            line = rec(a & 0xFFFF, 0x00, data[off:off + chunk])
            n += 1
            if lower_every and n % lower_every == 0:
                line = line.lower()
            lines.append(line)
            off += chunk
    return lines


def image_of(segments):
    img = bytearray(b"\xff" * FLASH)
    for addr, data in segments:
        if addr < FLASH:
            img[addr:addr + len(data)] = data
    return img


def block_stats(segments):
    touched = set()
    for addr, data in segments:
        if addr >= FLASH:
            continue
        for a in range(addr, addr + len(data)):
            touched.add(a // BLOCK)
    img = image_of(segments)
    written = sum(1 for b in touched if img[b * BLOCK:(b + 1) * BLOCK] != b"\xff" * BLOCK)
    return written, len(touched) - written


def write(outdir, name, lines, eof=True, final_newline=True):
    if eof:
        lines = lines + [rec(0, 0x01)]
    text = "\r\n".join(lines) + ("\r\n" if final_newline else "")
    with open(os.path.join(outdir, name), "w", newline="") as f:
        f.write(text)


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    rnd = random.Random(52832)

    def rbytes(n):
        return bytes(rnd.getrandbits(8) for _ in range(n))

    # ---- good.hex: a small merged image with all the awkward cases ----------
    mbr = rbytes(0x1000)
    app = bytearray(rbytes(0x5000))
    app[0x0800:0x0A00] = b"\xff" * 0x200          # one whole blank block inside the app
    app[0x1000:0x1010] = b"\xff" * 16             # 0xFF words inside a non-blank block
    boot = rbytes(0x1800)
    odd = rbytes(37)                              # unaligned start and length
    segments = [
        (0x00000, mbr),
        (0x26000, bytes(app[:0x2000])),
        (0x28000 + 0x1000, bytes(app[0x3000:])),  # 4 KB gap inside the app
        (0x3F001, odd),
        (0x74000, boot),
        (0x10001014, bytes.fromhex("00400700")),  # UICR records: must be ignored
        (0x10001018, bytes.fromhex("00E00700")),
    ]
    lines = [rec(0, 0x02, b"\x00\x00")]           # harmless extended segment record
    lines += to_hex(segments, rec_len=16, lower_every=97)
    lines.insert(5, rec(0, 0x03, b"\x00\x00\x00\x00"))   # start segment address (ignored)
    lines.append(rec(0, 0x05, b"\x00\x00\x10\x00"))       # start linear address (ignored)
    write(outdir, "good.hex", lines, final_newline=False)
    with open(os.path.join(outdir, "good.bin"), "wb") as f:
        f.write(image_of(segments))
    w, s = block_stats(segments)
    with open(os.path.join(outdir, "good.txt"), "w") as f:
        f.write(f"{w} {s}\n")

    # ---- typical.hex: SoftDevice + application + bootloader, ~210 KB --------
    segs = [
        (0x00000, rbytes(0x1000)),        # MBR
        (0x01000, rbytes(0x24000)),       # SoftDevice S132 sized (~148 KB)
        (0x26000, rbytes(0xA000)),        # application 40 KB
        (0x74000, rbytes(0x5800)),        # bootloader 22 KB
    ]
    write(outdir, "typical.hex", to_hex(segs, rec_len=32))
    with open(os.path.join(outdir, "typical.bin"), "wb") as f:
        f.write(image_of(segs))
    w, s = block_stats(segs)
    with open(os.path.join(outdir, "typical.txt"), "w") as f:
        f.write(f"{w} {s}\n")

    # ---- error cases ---------------------------------------------------------
    small = [(0x1000, rbytes(256))]
    bad = to_hex(small)
    bad[3] = bad[3][:-2] + "%02X" % ((int(bad[3][-2:], 16) + 1) & 0xFF)
    write(outdir, "bad_checksum.hex", bad)

    write(outdir, "out_of_order.hex", to_hex([(0x2000, rbytes(64)), (0x1000, rbytes(64))]))
    write(outdir, "out_of_range.hex", to_hex([(0x1000, rbytes(64)), (0x80000, rbytes(16))]))
    write(outdir, "no_eof.hex", to_hex(small), eof=False)
    write(outdir, "uicr_only.hex", to_hex([(0x10001014, bytes.fromhex("00400700"))]))
    write(outdir, "garbage.hex", ["hello world"], eof=False)


if __name__ == "__main__":
    main()
