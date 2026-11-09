#!/usr/bin/env python3
"""Patch the MGC 2011 demo (Retroactive, mgc_2011.z64) with the resident timing logger.

usage: make_patch.py mgc_2011.z64 hook.bin mgc_tasklog.z64
The logger (hook.bin, 4 KiB max) is appended to the ROM at 0xE80000 (the ROM is padded with 0xFF up to there),
the entry point gets a stub that copies it to RDRAM 0x7E6000 and jumps to it, and the CIC-6102 checksum is recomputed.
"""
import struct, sys

CIC6102_SEED = 0xF8CA4DDC
BLOB_ROM = 0xE80000
BLOB_RAM = 0x807E6000

def rol(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF if n else v

def checksum_6102(rom):
    t1 = t2 = t3 = t4 = t5 = t6 = CIC6102_SEED
    for d in struct.unpack('>%dI' % 0x40000, rom[0x1000:0x101000]):
        if ((t6 + d) & 0xFFFFFFFF) < t6: t4 = (t4 + 1) & 0xFFFFFFFF
        t6 = (t6 + d) & 0xFFFFFFFF
        t3 ^= d
        r = rol(d, d & 0x1F)
        t5 = (t5 + r) & 0xFFFFFFFF
        if t2 > d: t2 ^= r
        else: t2 ^= t6 ^ d
        t1 = (t1 + (t5 ^ d)) & 0xFFFFFFFF
    return (t6 ^ t4 ^ t3) & 0xFFFFFFFF, (t5 ^ t2 ^ t1) & 0xFFFFFFFF

def main():
    src, hook, dst = sys.argv[1:4]
    rom = bytearray(open(src, 'rb').read())
    if rom[:4] == b'\x37\x80\x40\x12':      # byte-swapped (.v64)
        rom[0::2], rom[1::2] = rom[1::2], rom[0::2]
    elif rom[:4] == b'\x40\x12\x37\x80':    # little-endian (.n64)
        rom = bytearray(b''.join(rom[i:i+4][::-1] for i in range(0, len(rom), 4)))
    if rom[:4] != b'\x80\x37\x12\x40':
        sys.exit('not an N64 ROM image (first bytes %s)' % bytes(rom[:4]).hex())
    if rom[0x20:0x31] != b'shut up and code ' or struct.unpack('>I', rom[8:12])[0] != 0x80001000:
        sys.exit('this does not look like the MGC 2011 demo (title %r, entry %08X)' % (bytes(rom[0x20:0x34]), struct.unpack('>I', rom[8:12])[0]))
    c1, c2 = checksum_6102(rom)
    if rom[0x10:0x18] != struct.pack('>II', c1, c2):
        sys.exit('the CIC-6102 checksum of this image does not match its header (%s): a bad dump?' % rom[0x10:0x18].hex())
    # the libultra functions we hook, as they sit in the ROM (boot segment: ROM 0x1000 -> RAM 0x80001000)
    def romw(ram): return struct.unpack('>I', rom[ram - 0x80001000 + 0x1000:][:4])[0]
    expect = {0x80015010: 0x27BDFFE0, 0x80015014: 0xAFB00010,   # osSpTaskLoad
              0x8000DFFC: 0x03E09021, 0x8000E000: 0x3C0A8018,   # send_mesg (exception handler)
              0x80015234: 0x0C005594, 0x80015238: 0x24040125,   # osSpTaskStartGo: jal __osSpSetStatus(0x125)
              0x80015668: 0x0C005594, 0x8001566C: 0x24040400,   # osSpTaskYield: jal __osSpSetStatus(0x400)
              0x80001000: 0x3C1D803F, 0x80001004: 0x37BDFFF0, 0x80001030: 0x0C000CB0, 0x80001038: 0x0001008D}
    for a, v in expect.items():
        if romw(a) != v: sys.exit('unexpected code at %08X: %08X (another build of the demo?)' % (a, romw(a)))
    blob = open(hook, 'rb').read()
    assert len(blob) <= 0x1000
    if len(rom) > BLOB_ROM: sys.exit('ROM larger than expected (%d bytes)' % len(rom))
    rom += b'\xff' * (BLOB_ROM - len(rom)) + blob + b'\xff' * (0x1000 - len(blob))
    # entry stub: copy the 4 KiB logger from ROM BLOB_ROM to RDRAM with uncached loads/stores, then jump to it
    src_rom = 0xB0000000 | BLOB_ROM
    dst_ram = 0xA0000000 | (BLOB_RAM & 0x1FFFFFFF)
    assert (src_rom & 0xFFFF) == 0
    stub = [0x3C080000 | (src_rom >> 16),     # lui t0, src
            0x3C090000 | (dst_ram >> 16),     # lui t1, dst
            0x35290000 | (dst_ram & 0xFFFF),  # ori t1,t1,dst_lo
            0x340A0400,  # ori t2,zero,0x400    1024 words
            0x8D0B0000,  # lw  t3,0(t0)
            0x25080004,  # addiu t0,t0,4
            0x254AFFFF,  # addiu t2,t2,-1
            0xAD2B0000,  # sw  t3,0(t1)
            0x1540FFFB,  # bnez t2,-5
            0x25290004,  # addiu t1,t1,4        (delay slot)
            0x08000000 | ((BLOB_RAM >> 2) & 0x03FFFFFF),  # j BLOB_RAM
            0x00000000,
            0x00000000]
    rom[0x1000:0x1034] = struct.pack('>13I', *stub)
    c1, c2 = checksum_6102(rom)
    rom[0x10:0x18] = struct.pack('>II', c1, c2)
    rom[0x20:0x34] = b'MGC 2011 TASKLOG    '
    open(dst, 'wb').write(rom)
    print('wrote %s (%d bytes), checksum %08X %08X' % (dst, len(rom), c1, c2))

if __name__ == '__main__':
    main()
