# mk_adf.py <bootblock> <payload> <output_adf>
#
# Stuff a given bootblock and payload into an output ADF image.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

# Amiga bootblock checksum
def checksum(bb, sum=0):
    while len(bb):
        x, bb = struct.unpack(">L",bb[:4]), bb[4:]
        sum += x[0]
        if sum >= (1<<32):
            sum -= (1<<32)-1
    return sum

def main(argv):
    bb_f = open(argv[1], "rb")
    pl_f = open(argv[2], "rb")
    out_f = open(argv[3], "wb")
    bb_dat = bb_f.read()
    pl_dat = pl_f.read()
    # Payload length is padded to multiple of 512 bytes for trackloader
    pl_len = struct.pack(">L", (len(pl_dat) + 511) & ~511)
    # Compute checksum over 512-byte bootblock, first 512 bytes of payload,
    # and the payload size (which we will stuff into the bootblock later).
    sum = checksum(pl_dat[:512], checksum(bb_dat, checksum(pl_len)))
    sum ^= 0xFFFFFFFF
    # "DOS\0"
    out_f.write(bb_dat[0:4])
    # Checksum
    out_f.write(struct.pack(">L", sum))
    # 880, BRA start
    out_f.write(bb_dat[8:16])
    # Payload length
    out_f.write(pl_len)
    # Bootblock code
    out_f.write(bb_dat[20:])
    # Pad bootblock to 512 bytes
    for x in xrange((512-len(bb_dat))/4):
        out_f.write(struct.pack(">L", 0))
    # Write the payload from sector 1 onwards
    out_f.write(pl_dat)
    # Pad the ADF image to 880kB
    for x in xrange((901120-len(pl_dat)-512)/4):
        out_f.write(struct.pack(">L", 0))
        
if __name__ == "__main__":
    main(sys.argv)
