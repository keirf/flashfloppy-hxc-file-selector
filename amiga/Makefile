CC = m68k-amigaos-gcc
AS = m68k-amigaos-as
CFLAGS += -Os -I ./inc -I .. -noixemul -I ../fat32 -DFATFS_IS_LITTLE_ENDIAN=0
CFLAGS += -m68000 -Wall
#CFLAGS += -DDEBUG
LDFLAGS = -s  -Wl,-Map,foo.map -noixemul -m68000
EXEC = HXCFEMNG

all: $(EXEC)
	m68k-amigaos-strip -s $(EXEC)
	exe2adf -i $(EXEC) -a AUTOBOOT.ADF -0 >/dev/null

HXCFEMNG:      fectrl.o gui_utils.o amiga.o crc.o fat_access.o fat_filelib.o fat_misc.o fat_string.o fat_table.o fat_write.o fat_cache.o reboot.o msg_txt.o
	$(CC) -o $@    $^ $(LDFLAGS)

reboot.o: reboot.S
	$(AS) -o $@ $< 

fectrl.o: ../fectrl.c
	$(CC) -o $@ -c $< $(CFLAGS)

gui_utils.o: ../gui_utils.c
	$(CC) -o $@ -c $< $(CFLAGS)

msg_txt.o: ../msg_txt.c
	$(CC) -o $@ -c $< $(CFLAGS)

amiga.o: amiga.c
	$(CC) -o $@ -c $< $(CFLAGS)

crc.o: crc.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_access.o: ../fat32/fat_access.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_filelib.o: ../fat32/fat_filelib.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_misc.o: ../fat32/fat_misc.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_string.o: ../fat32/fat_string.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_table.o: ../fat32/fat_table.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_write.o: ../fat32/fat_write.c
	$(CC) -o $@ -c $< $(CFLAGS)

fat_cache.o: ../fat32/fat_cache.c
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -rf *.o *.adf *.ADF *.map $(EXEC)

.PHONY: clean
