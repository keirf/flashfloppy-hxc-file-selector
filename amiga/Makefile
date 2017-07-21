CC = m68k-amigaos-gcc
AS = m68k-amigaos-as
OBJCOPY = m68k-amigaos-objcopy

CFLAGS += -Os -I./inc -I.. -I../fat32 -nostdlib -DFATFS_IS_LITTLE_ENDIAN=0
CFLAGS += -m68000 -Wall -Werror -std=gnu99 -msoft-float -fno-builtin
#CFLAGS += -DDEBUG
LDFLAGS += $(CFLAGS) -s -Wl,-Map,foo.map
AFLAGS += $(CFLAGS) -D__ASSEMBLY__ -Wa,--register-prefix-optional
AFLAGS += -Wa,-l -Wa,--bitwise-or -Wa,--base-size-default-16
AFLAGS += -Wa,--disp-size-default-16 -Wa,--pcrel

EXEC = FFManager

all: $(EXEC)
	m68k-amigaos-strip -s $(EXEC)
	exe2adf -i $(EXEC) -a AUTOBOOT_AMIGADOS.ADF -0 >/dev/null
	$(MAKE) bootblock.bin
	python adf/mk_adf.py bootblock.bin $(EXEC) AUTOBOOT_TRACKLOAD.ADF
	disk-analyse AUTOBOOT_TRACKLOAD.ADF AUTOBOOT_TRACKLOAD.HFE
	disk-analyse AUTOBOOT_AMIGADOS.ADF AUTOBOOT_AMIGADOS.HFE

$(EXEC): entry.o fectrl.o gui_utils.o amiga.o libnix.o math.o crc.o fat_access.o fat_filelib.o fat_misc.o fat_string.o fat_table.o fat_write.o fat_cache.o reboot.o msg_txt.o
	$(CC) -o $@    $^ $(LDFLAGS)

bootblock.bin: bootblock.o
	$(OBJCOPY) -O binary $< $@
	chmod a-x $@

bootblock.o: adf/bootblock.S
	$(CC) -o $@ -c $< $(AFLAGS)

entry.o: tinynix/entry.S
	$(CC) -o $@ -c $< $(AFLAGS)

reboot.o: reboot.S
	$(CC) -o $@ -c $< $(AFLAGS)

fectrl.o: ../fectrl.c
	$(CC) -o $@ -c $< $(CFLAGS)

gui_utils.o: ../gui_utils.c
	$(CC) -o $@ -c $< $(CFLAGS)

msg_txt.o: ../msg_txt.c
	$(CC) -o $@ -c $< $(CFLAGS)

amiga.o: amiga.c
	$(CC) -o $@ -c $< $(CFLAGS)

libnix.o: tinynix/libnix.c
	$(CC) -o $@ -c $< $(CFLAGS)

math.o: tinynix/math.c
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
	rm -rf *.o *.adf *.ADF *.map $(EXEC) *.bin

.PHONY: clean
