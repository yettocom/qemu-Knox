#!/usr/bin/env python3
"""Recover the symbol table from a raw arm64 kernel Image.

The image carries kallsyms but no symbol table: names are compressed against
`kallsyms_token_table`, and addresses are 32-bit offsets from
`kallsyms_relative_base` when CONFIG_KALLSYMS_BASE_RELATIVE is on.  Nothing here
is a hard-coded file offset, so the same code works on another firmware build.

Layout, verified against this image by decoding all 126452 names and landing
exactly on the marker table:

    kallsyms_offsets[]      int32 per symbol, ascending, offset from the base
    kallsyms_relative_base  u64, the lowest address (_text)
    kallsyms_num_syms       u32
    kallsyms_names[]        compressed: (len, token...) per symbol
    kallsyms_markers[]      u32, byte offset of every 256th symbol
    kallsyms_token_table    256 NUL-terminated strings
    kallsyms_token_index    u16[256], offsets into the token table

Three structural facts do the work.  The token index is non-decreasing, because
two byte values may share one token, and its distinct values tile the token
table end to end.  The relative base is a page-aligned kernel address with the
symbol count right behind it.  And the offsets array is ascending, starts at 0
and ends exactly where the base begins.
"""
import struct
import sys

KERNEL_VA_BITS = 0xFFFF


def token_table_at(d, index_pos):
    """Return (table_start, tokens) if a token_index starts at index_pos."""
    if index_pos + 512 > len(d) or index_pos < 8:
        return None
    idx = struct.unpack_from("<256H", d, index_pos)
    if idx[0] != 0 or idx[255] > 0x2000:
        return None
    for i in range(255):
        if idx[i + 1] < idx[i]:
            return None
    distinct = sorted(set(idx))
    for extra in range(2, 160):
        length = idx[255] + extra
        start = index_pos - length
        if start < 0:
            return None
        tokens = {}
        ok = True
        for off in distinct:
            end = d.find(b"\0", start + off, index_pos)
            if end < 0:
                ok = False
                break
            tokens[off] = d[start + off:end]
        if not ok:
            continue
        pos = 0
        tiled = True
        for off in distinct:
            if off != pos:
                tiled = False
                break
            pos = off + len(tokens[off]) + 1
        if tiled and pos == length:
            return start, [tokens[o] for o in idx]
    return None


def find_token_tables(d):
    out = []
    pos = 0
    while True:
        pos = d.find(b"\0\0", pos)
        if pos < 0:
            return out
        if pos % 2 == 0:
            got = token_table_at(d, pos)
            if got:
                out.append((pos, got))
        pos += 1


def decode_names(d, start, count, tokens, limit):
    pos = start
    out = []
    for _ in range(count):
        if pos >= limit:
            return None
        ln = d[pos]
        pos += 1
        if ln & 0x80:
            if pos >= limit:
                return None
            ln = (ln & 0x7F) | (d[pos] << 7)
            pos += 1
        elif ln == 0:
            if pos + 2 > limit:
                return None
            ln = struct.unpack_from("<H", d, pos)[0]
            pos += 2
        if pos + ln > limit:
            return None
        out.append(b"".join(tokens[t] for t in d[pos:pos + ln]))
        pos += ln
    return out, pos


def base_candidates(d, limit):
    """Yield page-aligned kernel addresses below limit, cheaply."""
    needle = b"\xc0\xff\xff\xff"
    pos = 0
    while True:
        pos = d.find(needle, pos)
        if pos < 0:
            return
        p = pos - 4
        if p >= 0 and p % 8 == 0:
            v = struct.unpack_from("<Q", d, p)[0]
            if (v >> 48) == KERNEL_VA_BITS and (v & 0xFFF) == 0:
                yield p, v
        pos += 1


def recover(path):
    d = open(path, "rb").read()
    tables = find_token_tables(d)
    if not tables:
        raise SystemExit("no kallsyms_token_index found")

    for index_pos, (table_start, tokens) in tables:
        for base_pos, base in base_candidates(d, table_start):
            if base_pos + 16 > table_start:
                continue
            count = struct.unpack_from("<I", d, base_pos + 8)[0]
            if not (10000 <= count <= 1000000):
                continue
            names_start = (base_pos + 12 + 7) & ~7
            got = decode_names(d, names_start, count, tokens, table_start)
            if not got:
                continue
            names, names_end = got
            markers = (count + 255) // 256
            if not (names_end + 4 * markers <= table_start <= names_end + 4 * markers + 16):
                continue
            if struct.unpack_from("<I", d, names_end)[0] != 0:
                continue
            offsets_start = base_pos - 4 * count
            if offsets_start < 0:
                continue
            offs = struct.unpack_from("<%di" % count, d, offsets_start)
            if offs[0] != 0 or offs[-1] > 0x4000000:
                continue
            if not all(offs[i] <= offs[i + 1] for i in range(count - 1)):
                continue
            return {
                "image": path,
                "count": count,
                "offsets": offsets_start,
                "base_pos": base_pos,
                "relative_base": base,
                "names": names_start,
                "names_end": names_end,
                "markers": names_end,
                "token_table": table_start,
                "token_index": index_pos,
                "raw": names,
                "offsets_raw": offs,
            }
    raise SystemExit("no consistent kallsyms layout found")


def symbols(info):
    base = info["relative_base"]
    for name, off in zip(info["raw"], info["offsets_raw"]):
        if not name:
            continue
        yield chr(name[0]), name[1:].decode("ascii", "replace"), base + (off & 0xFFFFFFFF)


def main(argv):
    if len(argv) < 2:
        print("usage: kallsyms.py <Image> [name ...]")
        return 2
    info = recover(argv[1])
    print("num_syms      %d" % info["count"])
    print("relative_base %#x" % info["relative_base"])
    print("offsets       %#x" % info["offsets"])
    print("names         %#x .. %#x" % (info["names"], info["names_end"]))
    print("markers       %#x" % info["markers"])
    print("token_table   %#x (%d bytes)" % (info["token_table"], info["token_index"] - info["token_table"]))
    wanted = argv[2:]
    if not wanted:
        return 0
    table = {}
    for kind, name, addr in symbols(info):
        table.setdefault(name, []).append((kind, addr))
    for w in wanted:
        hits = table.get(w)
        if not hits:
            print("%-38s MISSING" % w)
            continue
        for kind, addr in hits:
            print("%-38s %s %#x  file %#x" % (w, kind, addr, addr - info["relative_base"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
