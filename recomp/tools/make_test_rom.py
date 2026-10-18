#!/usr/bin/env python3
"""Make a fake 32 MB "ROM" for testing the build without the real game.

It contains no game data: every code region is filled with the same tiny
function (save stack, return), plus a few calls to the functions the port
cares about (the overlay loader, libultra) so that the generated code
exercises those paths. The result builds and links but does not run a game.

    python make_test_rom.py out.z64
"""

import struct
import sys

SIZE = 0x2000000
CODE_START = 0x1000
CODE_END = 0x418A50  # end of the last overlay in marioparty2.yaml


def jal(target: int) -> int:
    return 0x0C000000 | ((target >> 2) & 0x3FFFFFF)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 1

    rom = bytearray(SIZE)
    rom[0:4] = b"\x80\x37\x12\x40"
    rom[0x20:0x34] = b"MP2 TEST ROM        "

    block = bytearray(0x80)
    block[0:4] = struct.pack(">I", 0x27BDFFE8)      # addiu sp, sp, -0x18
    block[0x78:0x7C] = struct.pack(">I", 0x03E00008)  # jr ra
    block[0x7C:0x80] = struct.pack(">I", 0x27BD0018)  # addiu sp, sp, 0x18
    for off in range(CODE_START, CODE_END, 0x80):
        rom[off:off + 0x80] = block

    calls = [
        0x27BDFFE8,
        jal(0x8007C4E4), 0,  # HuRomDmaCodeRead (replaced by native code)
        jal(0x800A6030), 0,  # osCreateThread (runtime)
        jal(0x800AE1F0), 0,  # osPfsIsPlug (not implemented anywhere -> stub)
        jal(0x800A5210), 0,  # bcopy
        0x3C198010, 0x37392800, 0x0320F809, 0,  # jalr to the overlay window
    ]
    for i, word in enumerate(calls):
        rom[0x2004 + i * 4:0x2008 + i * 4] = struct.pack(">I", word)
    # An overlay calling into itself.
    rom[0xD57F4:0xD57F8] = struct.pack(">I", jal(0x80102880))

    with open(sys.argv[1], "wb") as f:
        f.write(rom)
    print(f"wrote {sys.argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
