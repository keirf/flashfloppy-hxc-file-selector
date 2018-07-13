# mk_hxcsdfe.py
#
# Create a blank V2 HXCSDFE.CFG
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

def main(argv):
    out_f = open(argv[1], "wb")
    out_f.write(struct.pack("<16s", b"HXCFECFGV2.0"))
    out_f.write(struct.pack("<BBBBBBBBHBBBBBB",
                            0xff, # step_sound
                            0xff, # ihm_sound
                            20,   # back_light_tmr
                            20,   # standby_tmr
                            0,    # disable_drive_select
                            64,   # buzzer_duty_cycle
                            1,    # number_of_slot
                            0,    # slot_index
                            0,    # update_cnt
                            0,    # load_last_floppy
                            0xe8, # buzzer_step_duration
                            0x96, # lcd_scroll_speed
                            0,    # startup_mode
                            0,    # enable_drive_b
                            0))   # index_mode
    for i in range(2): # drive[i]
        out_f.write(struct.pack("<BBBB",
                                0,  # cfg_from_cfg
                                0,  # interfacemode
                                0,  # pin02_cfg
                                0)) # pin34_cfg
    out_f.write(struct.pack("<B23s",
                            0,    # drive_b_as_motor_on
                            b"")) # pad
    out_f.write(struct.pack("<IIIIII",
                            2,    # slots_map_position
                            1000, # max_slot_number
                            3,    # slots_position
                            1,    # number_of_drive_per_slot
                            0,    # cur_slot_number
                            0))   # ihm_mode
    out_f.seek(65535)
    out_f.write(struct.pack("<B", 0))
    
if __name__ == "__main__":
    main(sys.argv)
