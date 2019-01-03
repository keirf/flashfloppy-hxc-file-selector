# amiga_strip_hunks.py
# 
# Strip hunk metadata from an Amiga object file.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

HUNK_HEADER       = 0x3f3
HUNK_UNIT         = 0x3e7
HUNK_NAME         = 0x3e8

HUNK_CODE         = 0x3e9
HUNK_DATA         = 0x3ea
HUNK_BSS          = 0x3eb

HUNK_RELOC32      = 0x3ec
HUNK_RELOC32SHORT = 0x3fc
HUNK_RELOC16      = 0x3ed
HUNK_RELOC8       = 0x3ee
HUNK_DREL32       = 0x3f7
HUNK_DREL16       = 0x3f8
HUNK_DREL8        = 0x3f9
HUNK_ABSRELOC16   = 0x3fd
HUNK_SYMBOL       = 0x3f0
HUNK_DEBUG        = 0x3f1
HUNK_END          = 0x3f2
HUNK_EXT          = 0x3ef
HUNK_OVERLAY      = 0x3f5
HUNK_BREAK        = 0x3f6
HUNK_LIB          = 0x3fa
HUNK_INDEX        = 0x3fb

def main(argv):
    if len(argv) != 3:
        print("%s <input_file> <output_file>" % argv[0])
        return
    in_f = open(argv[1], "rb")
    out_f = open(argv[2], "wb")
    (id, nr) = struct.unpack(">II", in_f.read(2*4))
    assert id == HUNK_UNIT
    in_f.read(nr*4) # skip unit name
    while True:
        _id = in_f.read(1*4)
        if len(_id) == 0:
            break
        (id,) = struct.unpack(">I", _id)
        if id == HUNK_CODE or id == HUNK_DATA:
            (nr,) = struct.unpack(">I", in_f.read(1*4))
            out_f.write(in_f.read(nr*4))
        elif id == HUNK_SYMBOL:
            while True: # skip each Symbol Data Unit
                (nr,) = struct.unpack(">I", in_f.read(1*4))
                if nr == 0: break
                in_f.read((nr+1)*4)
        elif id == HUNK_NAME:
            (nr,) = struct.unpack(">I", in_f.read(1*4))
            in_f.read(nr*4) # skip hunk name
        elif id == HUNK_END:
            pass
        else:
            print("Unexpected hunk 0x%x" % (id))
            assert False
    
if __name__ == "__main__":
    main(sys.argv)
