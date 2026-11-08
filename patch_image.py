#!/usr/bin/env python3
"""Produce the image variant QEMU can boot, and record exactly what was changed.

The vendor Image is a PE/COFF file that is also a valid arm64 Image: offset 0
holds the "MZ" signature encoded as a harmless `ccmp`, offset 4 is
`b stext`, and stext sits at file offset 0x27D0000.  QEMU loads the file at
0x40000000 with text_offset 0 and enters at offset 0, so the vendor header
works as-is.

What does not work is Samsung's secure monitor.  `back` (file offset 0x152F8)
is the retry wrapper around `smc #0`:

    smc #0
    mov x8, x0
    cmp x8, #1
    b.eq  retry            // busy
    ... restore x0..x7 ...
    cmp x8, #0
    b.ne retry             // any other failure retries forever
    ret

With no EL3, `smc` raises an Undefined Instruction and the kernel panics in
`paging_init`.  Replacing that one `smc` with `mov x0, #0` lets the wrapper
report success and boot continues -- until the RKP-aware page-table code
trips a BUG_ON because the kernel now believes the secure side accepted its
request.

So this script only gets us to `paging_init`.  Booting further needs RKP
disabled at the source (patch `rkp_started` / `rkp_paging_init`), which needs
symbol addresses -- see the kallsyms note in README.
"""
import struct
import sys

SRC = "image/Image"
DST = "image/Image.norkp"

# file offset of the `smc #0` inside `back`
BACK_SMC = 0x152F8
SMC = 0xD4000003
MOV_X0_0 = 0xD2800000


def main() -> int:
    d = bytearray(open(SRC, "rb").read())
    old = struct.unpack_from("<I", d, BACK_SMC)[0]
    if old != SMC:
        print("unexpected word at %#x: %#010x" % (BACK_SMC, old))
        return 1
    struct.pack_into("<I", d, BACK_SMC, MOV_X0_0)
    open(DST, "wb").write(d)
    print("wrote %s: %#x %#010x -> %#010x" % (DST, BACK_SMC, SMC, MOV_X0_0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
