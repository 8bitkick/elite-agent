#!/usr/bin/env python3
"""pack.py <app.elf-as-binary> <disc.ssd|-> <out.bin>: the app's image for the app slot.

The linked image (objcopy -O binary: the header, the code and constants, .data's first values)
with the disc after it (4-byte aligned), then the header's image fields filled in
(rp2350-bbc-micro's src/common/app_api.h, struct app_header): the disc's place, size and hash
(FNV-1a, as the firmware's drive.c takes it), the image's size and its CRC-32 (zlib's, taken
with the CRC field as 0). The firmware checks all of them before it runs the app.
"""
import struct, sys, zlib

SLOT_XIP = 0x10e00000
MAGIC, API = 0x50504142, 2
# struct app_header (API 2): the offsets of what's filled in here
HEADER_SIZE, IMAGE_SIZE, CRC, DISC_HASH, DISC_LOAD, DISC_SIZE = 152, 60, 64, 116, 120, 124

def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h

def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    img = bytearray(open(sys.argv[1], 'rb').read())
    magic, api, hsize = struct.unpack_from('<IHH', img, 0)
    if magic != MAGIC or api != API or hsize != HEADER_SIZE:
        sys.exit('%s: not an app header for API %d (%08x, API %d, %d bytes)' % (sys.argv[1], API, magic, api, hsize))
    img += bytes(-len(img) % 4)
    if sys.argv[2] != '-':
        disc = open(sys.argv[2], 'rb').read()
        struct.pack_into('<III', img, DISC_HASH, fnv(disc), SLOT_XIP + len(img), len(disc))
        img += disc + bytes(-len(disc) % 4)
    if len(img) > 0x100000:
        sys.exit('%d KB: more than the 1 MB app slot' % (len(img) // 1024))
    struct.pack_into('<II', img, IMAGE_SIZE, len(img), 0)
    struct.pack_into('<I', img, CRC, zlib.crc32(img) & 0xffffffff)
    open(sys.argv[3], 'wb').write(img)
    print('%s: %d KB, CRC %08x' % (sys.argv[3], len(img) // 1024, struct.unpack_from('<I', img, CRC)[0]))

if __name__ == '__main__':
    main()
