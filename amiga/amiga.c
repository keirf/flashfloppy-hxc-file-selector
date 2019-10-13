/* 
 * Modifications for the FlashFloppy project:
 * Copyright (c) 2017 Keir Fraser
 * 
 * Original HxC version:
 * Copyright (C) 2009-2017 Jean-François DEL NERO
 *
 * This file is part of the FlashFloppy file selector.
 *
 * FlashFloppy file selector may be used and distributed without
 * restriction provided that this copyright statement is not removed from the
 * file and that any derivative work contains the original copyright notice and
 * the associated disclaimer.
 *
 * FlashFloppy file selector is free software; you can redistribute it
 * and/or modify  it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the License, 
 * or (at your option) any later version.
 *
 * FlashFloppy file selector is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *   See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along 
 * with FlashFloppy file selector; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <exec/execbase.h>
#include <devices/trackdisk.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/alib.h>

#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "conf.h"
#include "types.h"
#include "cfg_file.h"
#include "ui_context.h"
#include "keysfunc_defs.h"
#include "keys_defs.h"
#include "keymap.h"
#include "hardware.h"
#include "amiga_hw.h"
#include "m68k.h"
#include "gui_utils.h"
#include "reboot.h"
#include "crc.h"
#include "color_table.h"
#include "mfm_table.h"
#include "errors_def.h"
#include "hal.h"
#include "../graphx/font.h"

uint8_t *screen_buffer;

static int start_unit = -1;
static int td_boot_unit = -1;

static uint8_t *mfmtobinLUT_L;
static uint8_t *mfmtobinLUT_H;
#define MFMTOBIN(W) (mfmtobinLUT_H[W>>8] | mfmtobinLUT_L[W&0xFF])

#define RD_TRACK_BUFFER_SIZE 10*1024
#define WR_TRACK_BUFFER_SIZE 600

static uint16_t *track_buffer_rd;
static uint16_t *track_buffer_wr;

static uint8_t validcache;

#define MAX_CACHE_SECTOR 16
uint16_t sector_pos[MAX_CACHE_SECTOR];

#if __GNUC__ < 3
#define attribute_used __attribute__((unused))
#define likely(x) x
#define unlikely(x) x
#else
#define attribute_used __attribute__((used))
#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#endif

/* Write to INTREQ twice at end of ISR to prevent spurious re-entry on 
 * A4000 with faster processors (040/060). */
#define IRQ_RESET(x) do {                       \
    uint16_t __x = (x);                         \
    cust->intreq = __x;                         \
    cust->intreq = __x;                         \
} while (0)

#define IRQ(name)                               \
static void c_##name(void) attribute_used;      \
void name(void);                                \
asm (                                           \
"    .text; .align 2                \n"         \
"_"#name":                          \n"         \
"    movem.l %d0-%d1/%a0-%a1,-(%sp) \n"         \
"    jbsr    _c_"#name"              \n"        \
"    movem.l (%sp)+,%d0-%d1/%a0-%a1 \n"         \
"    rte                            \n"         \
)

volatile struct m68k_vector_table *m68k_vec;

static volatile struct amiga_custom * const cust =
    (struct amiga_custom *)0xdff000;
static volatile struct amiga_cia * const ciaa =
    (struct amiga_cia *)0x0bfe001;
static volatile struct amiga_cia * const ciab =
    (struct amiga_cia *)0x0bfd000;

/* Division 32:16 -> 32q:16r */
#define do_div(x, y) ({                                             \
    uint32_t _x = (x), _y = (y), _q, _r;                            \
    asm volatile (                                                  \
        "swap %3; "                                                 \
        "move.w %3,%0; "                                            \
        "divu.w %4,%0; "  /* hi / div */                            \
        "move.w %0,%1; "  /* stash quotient-hi */                   \
        "swap %3; "                                                 \
        "move.w %3,%0; "  /* lo / div */                            \
        "divu.w %4,%0; "                                            \
        "swap %1; "                                                 \
        "move.w %0,%1; "  /* stash quotient-lo */                   \
        "eor.w %0,%0; "                                             \
        "swap %0; "                                                 \
        : "=&d" (_r), "=&d" (_q) : "0" (0), "d" (_x), "d" (_y));    \
   (x) = _q;                                                        \
   _r;                                                              \
})

static uint32_t div32(uint32_t dividend, uint16_t divisor)
{
    do_div(dividend, divisor);
    return dividend;
}

#ifdef DEBUG

static void push_serial_char(uint8_t byte)
{
    cust->serper = 0x1e; /* 115200 baud */
    while (!(cust->serdatr & 0x2000))
        continue;
    cust->serdat = byte | 0x100;
}

void dbg_printf(char *fmt, ...)
{
    char txt_buffer[1024];
    int i;

    va_list marker;
    va_start(marker, fmt);

    vsnprintf(txt_buffer, sizeof(txt_buffer), fmt, marker);

    for (i = 0; txt_buffer[i]; i++) {
        if (txt_buffer[i] == '\n')
            push_serial_char('\r');
        push_serial_char(txt_buffer[i]);
    }

    va_end(marker);
}

#else

#define dbg_printf(_f, ...) ((void)0)

#endif

/****************************************************************************
 *                              Time
 ****************************************************************************/

/* PAL/NTSC and implied CPU frequency. */
static uint8_t is_pal;
static unsigned int cpu_hz;
#define PAL_HZ 7093790
#define NTSC_HZ 7159090

/* VBL IRQ: 16- and 32-bit timestamps, and VBL counter. */
static volatile uint32_t stamp32;
static volatile uint16_t stamp16;
static volatile unsigned int vblank_count;

/* Loop to get consistent current CIA timer value. */
#define get_ciatime(_cia, _tim) ({              \
    uint8_t __hi, __lo;                         \
    do {                                        \
        __hi = (_cia)->_tim##hi;                \
        __lo = (_cia)->_tim##lo;                \
    } while (__hi != (_cia)->_tim##hi);         \
    ((uint16_t)__hi << 8) | __lo; })

static uint16_t get_ciaatb(void)
{
    return get_ciatime(ciaa, tb);
}

static uint32_t get_time(void)
{
    uint32_t _stamp32;
    uint16_t _stamp16;

    /* Loop to get consistent timestamps from the VBL IRQ handler. */
    do {
        _stamp32 = stamp32;
        _stamp16 = stamp16;
    } while (_stamp32 != stamp32);

    return -(_stamp32 - (uint16_t)(_stamp16 - get_ciaatb()));
}

static void delay_ms(unsigned int ms)
{
    uint16_t ticks_per_ms = div32(cpu_hz+9999, 10000); /* round up */
    uint32_t s, t;

    s = get_time();
    do {
        t = div32(get_time() - s, ticks_per_ms);
    } while (t < ms);
}

static void delay_us(int us)
{
    uint32_t s;
    s = get_time();
    /* CIA timer ticks a bit slower than 1MHz, but it'll do. */
    while ((get_time() - s) < us)
        continue;
}

static uint8_t detect_pal_chipset(void)
{
    return !(cust->vposr & (1u<<12));
}

void waitms(int ms)
{
    delay_ms(ms);
}

void waitsec(int secs)
{
    while (secs--)
        delay_ms(1000);
}

void lockup()
{
    dbg_printf("lockup : Sofware halted...\n");
    for (;;)
        continue;
}

/****************************************************************************
 *                              FDC I/O
 ****************************************************************************/

/* CIAB IRQ: FLAG (disk index) pulse counter. */
static volatile unsigned int disk_index_count;

IRQ(CIAB_IRQ);
static void c_CIAB_IRQ(void)
{
    uint8_t icr = ciab->icr;

    if (icr & CIAICR_FLAG)
        disk_index_count++;

    /* NB. Clear intreq.ciab *after* reading/clearing ciab.icr else we get a 
     * spurious extra interrupt, since intreq.ciab latches the level of CIAB 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciab second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    IRQ_RESET(INT_CIAB);
}

static void drive_deselect(void)
{
    ciab->prb |= 0xf9; /* motor-off, deselect all */
}

/* Select @drv and set motor on or off. */
static void drive_select(unsigned int drv, int on)
{
    drive_deselect(); /* motor-off, deselect all */
    if (on)
        ciab->prb &= ~CIABPRB_MTR; /* motor-on */
    ciab->prb &= ~(CIABPRB_SEL0 << drv); /* select drv */
}

/*
 * Returns the unit number of the underlying device of a filesystem lock.
 * Returns -1 on failure.
 */
static LONG GetUnitNumFromLock(BPTR lock)
{
    LONG unitNum = -1;

    if (lock != 0) {
        struct InfoData *infoData = AllocMem(sizeof(*infoData), MEMF_ANY);
        if (infoData != NULL) {
            if (Info(lock, infoData))
                unitNum = infoData->id_UnitNumber;
            FreeMem(infoData, sizeof(*infoData));
        }
    }

    dbg_printf("GetUnitNumFromLock : %d\n", unitNum);
    return unitNum;
}

static UWORD GetLibraryVersion(struct Library *library)
{
    dbg_printf("GetLibraryVersion : %d\n",library->lib_Version);

    return library->lib_Version;
}

static bool_t test_drive(int drive)
{
    bool_t is_emulated_unit;
    uint8_t ciaapra;

    /* No need to test a drive we were booted from. This avoids test failure
     * on unmodded ESCOM boards. */
    if (drive == td_boot_unit)
        return TRUE;

    drive_select(drive, 1);
    delay_us(10);

    /* We let the motor spin down and then re-enable the motor and 
     * almost immediately check if the drive is READY. A real mechanical 
     * drive will need to time to spin back up. */
    ciaapra = ~ciaa->pra;
    is_emulated_unit = (ciaapra & CIAAPRA_RDY) && !(ciaapra & CIAAPRA_CHNG);

    drive_select(drive, 0);
    drive_deselect();

    return is_emulated_unit;
}

/* Allocate a message port (must be later initialised with InitPort()). */
static struct MsgPort *AllocPort(void)
{
    int sig_bit;
    struct MsgPort *mp;

    if ((sig_bit = AllocSignal(-1L)) == -1)
        return NULL;

    mp = AllocMem(sizeof(*mp), MEMF_PUBLIC);
    if (mp == NULL) {
        FreeSignal(sig_bit);
        return NULL;
    }

    mp->mp_SigBit = sig_bit;
    mp->mp_SigTask = FindTask(0);

    return mp;
}

/* Initialise a previously-allocated but currently unused port. */
static void InitPort(struct MsgPort *mp)
{
    UBYTE sig_bit = mp->mp_SigBit;
    void *sig_task = mp->mp_SigTask;

    memset(mp, 0, sizeof(*mp));
    
    mp->mp_Node.ln_Type = NT_MSGPORT;
    mp->mp_Flags = PA_SIGNAL;
    mp->mp_SigBit = sig_bit;
    mp->mp_SigTask = sig_task;

    NewList(&mp->mp_MsgList);
}

/* Free a previously-allocated but currently unused port. */
static void FreePort(struct MsgPort *mp)
{
    FreeSignal(mp->mp_SigBit);
    FreeMem(mp, sizeof(*mp));
}

/* Boot-device IORequest structure. */
extern struct IOExtTD *TDIOReq;

/* Search for the trackdisk boot device. The strategy is to open each 
 * trackdisk unit in turn and test for a match on the io_Unit structure. */
static int trackdisk_get_boot_unit(void)
{
    struct MsgPort *mp;
    struct IOExtTD *td;
    struct Unit *td_unit;
    int unit;
    BYTE rc;

    if ((mp = AllocPort()) == NULL)
        return 0;

    if ((td = AllocMem(sizeof(*td), MEMF_PUBLIC)) == NULL) {
        FreePort(mp);
        return 0;
    }

    for (unit = 0; unit < 4; unit++) {

        InitPort(mp);

        memset(td, 0, sizeof(*td));
        td->iotd_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
        td->iotd_Req.io_Message.mn_ReplyPort = mp;
        td->iotd_Req.io_Message.mn_Length = sizeof(*td);

        rc = OpenDevice((unsigned char *)"trackdisk.device", unit,
                        (struct IORequest *)td, 0);
        if (rc == 0) {
            td_unit = td->iotd_Req.io_Unit;
            CloseDevice((struct IORequest *)td);
            if (td_unit == TDIOReq->iotd_Req.io_Unit) {
                td_boot_unit = unit;
                break;
            }
        }
    }

    FreeMem(td, sizeof(*td));
    FreePort(mp);

    return unit & 3;
}

/* Do the system-friendly bit while AmigaOS is still alive. */
static void _get_start_unit(void)
{
    if (!DOSBase) {
        start_unit = trackdisk_get_boot_unit();
        return;
    }

    /* NB. On dos.library < v36 we can't get at the program dir or path 
     * (libnix's argv[0] is computed from GetProgramName()). */
    if (GetLibraryVersion((struct Library *)DOSBase) >= 36)
        start_unit = GetUnitNumFromLock(GetProgramDir());
    if (start_unit < 0)
        start_unit = 0;
    start_unit &= 3;
}

int get_start_unit(char *path)
{
    int i;

    if (start_unit < 0)
        return -ERR_DRIVE_NOT_FOUND;

    delay_ms(100); /* give all motors time to spin down */

    for (i = 0; i < 4; i++) {
        if (test_drive(start_unit)) {
            dbg_printf("get_start_unit : drive %d\n", start_unit);
            return start_unit;
        }
        start_unit = (start_unit + 1) & 3;
    }

    dbg_printf("get_start_unit : drive not found !\n");
    return -ERR_DRIVE_NOT_FOUND;
}

int jumptotrack(uint8_t t)
{
    unsigned int steps = 0;

    dbg_printf("jumptotrack %d - seek track 0...\n", t);

    ciab->prb |= CIABPRB_DIR; /* outward */
    delay_ms(18);
    while (!(~ciaa->pra & CIAAPRA_TK0)) {
        ciab->prb &= ~CIABPRB_STEP;
        ciab->prb |= CIABPRB_STEP;
        delay_ms(3);
        if (steps++ >= 1024) {
            dbg_printf("jumptotrack %d - track 0 not found!!\n", t);
            return -ERR_TRACK0_SEEK;
        }
    }
    delay_ms(15);

    dbg_printf("jumptotrack %d - track 0 found in %u steps\n", t, steps);

    ciab->prb &= ~CIABPRB_DIR; /* inward */
    for (steps = 0; steps < t; steps++) {
        ciab->prb &= ~CIABPRB_STEP;
        ciab->prb |= CIABPRB_STEP;
        delay_ms(3);
    }
    delay_ms(15);

    dbg_printf("jumptotrack %d - jump done\n", t);

    return ERR_NO_ERROR;
};

static int disk_wait_dma(void)
{
    unsigned int i;
    for (i = 0; i < 1000; i++) {
        if (cust->intreqr & INT_DSKBLK) /* dsk-blk-done? */
            break;
        delay_ms(1);
    }
    cust->dsklen = 0x4000; /* no more dma */
    return (i < 1000) ? ERR_NO_ERROR : -ERR_TIMEOUT;
}

static int readtrack(uint16_t *track, uint16_t size)
{
    cust->dskpt.p = track;
    cust->adkcon = 0x7f00; /* clear disk flags */
    cust->intreq = INT_DSKBLK; /* clear dsk-blk-done */
    cust->adkcon = 0x9500; /* MFM, wordsync */
    cust->dsksync = 0x4489; /* sync 4489 */

    cust->dsklen = size | 0x8000;
    cust->dsklen = size | 0x8000;

    return disk_wait_dma();
}

static int writetrack(uint16_t *track, uint16_t size)
{
    cust->dskpt.p = track;
    cust->adkcon = 0x7f00; /* clear disk flags */
    cust->intreq = INT_DSKBLK; /* clear dsk-blk-done */
    cust->adkcon = 0xb100; /* MFM, no wordsync, precomp */

    cust->dsklen = size | 0xc000;
    cust->dsklen = size | 0xc000;

    return disk_wait_dma();
}

/* Fast Bin to MFM converter */
static void BuildCylinder(
    void *mfm_buffer, void *track_data, int size, uint16_t *p_lastbit)
{
    uint8_t byte;
    uint16_t mfm_code;
    uint8_t *in = track_data;
    uint8_t *out = mfm_buffer;
    uint16_t lastbit = *p_lastbit;

    /* MFM Encoding */
    while (size--) {
        byte = *in++;

        mfm_code = MFM_tab[byte] & lastbit;

        *out++ = (uint8_t)(mfm_code >> 8);
        *out++ = (uint8_t)mfm_code;

        lastbit = ~(MFM_tab[byte] << 15);
    }

    *p_lastbit = lastbit;
}

int writesector(uint8_t sectornum, uint8_t *data)
{
    const uint8_t header[4] = { 0xa1, 0xa1, 0xa1, 0x01 };
    uint16_t i, j, lastbit, crc;
    uint8_t byte;

    dbg_printf("writesector : %d\n",sectornum);

    /* Calculate the data CRC. */
    crc = crc16_ccitt(header, 4, 0xffff);
    crc = crc16_ccitt(&sectornum, 1, crc);
    crc = crc16_ccitt(data, 512, crc);

    /* MFM: pre-gap */
    for (i = 0; i < 12; i++)
        track_buffer_wr[i]=0xAAAA;
    /* MFM: sync */
    track_buffer_wr[i++]=0x4489;
    track_buffer_wr[i++]=0x4489;
    track_buffer_wr[i++]=0x4489;
    lastbit = 0x7FFF;
    /* MFM: Data Address Mark (0x01, secnum). */
    byte = header[3];
    BuildCylinder(&track_buffer_wr[i++], &byte, 1, &lastbit);
    BuildCylinder(&track_buffer_wr[i++], &sectornum, 1, &lastbit);
    /* MFM: 512 bytes data. */
    BuildCylinder(&track_buffer_wr[i], data, 512, &lastbit);
    i += 512;
    /* MFM: CRC. */
    BuildCylinder(&track_buffer_wr[i], &crc, 2, &lastbit);
    i += 2;
    /* MFM: post-gap. */
    byte = 0x4E;
    for (j = 0; j < 4; j++)
        BuildCylinder(&track_buffer_wr[i++], &byte, 1, &lastbit);

    validcache = 0;
    return writetrack(track_buffer_wr, i);
}

int readsector(uint8_t sectornum, uint8_t *data, uint8_t invalidate_cache)
{
    const uint8_t header[4] = { 0xa1, 0xa1, 0xa1, 0xfb };
    uint16_t i, j, crc;
    uint8_t sectorfound, tc;
    uint8_t retry, retry2;
    uint8_t sector_header[8];
    uint8_t sect_num;
    int ret;

    dbg_printf("readsector : %d - %d\n", sectornum, invalidate_cache);

    if (sectornum >= MAX_CACHE_SECTOR)
        return -ERR_INVALID_PARAMETER;

    sector_header[0] = 0xFE; /* IDAM */
    sector_header[1] = 0xFF; /* Track */
    sector_header[2] = 0x00; /* Side */
    sector_header[3] = sectornum; /* Sector */
    sector_header[4] = 0x02; /* Size */

    /* Compute the CRC for the IDAM area. */
    crc = crc16_ccitt(header, 3, 0xffff); /* sync */
    crc = crc16_ccitt(sector_header, 5, crc);

    sector_header[5] = crc >> 8; /* CRC High */
    sector_header[6] = crc; /* CRC Low */

    sectorfound = 0;

    for (retry2 = 0; !sectorfound && (retry2 < 2); retry2++) {

        for (retry = 0; !sectorfound && (retry < 5); retry++) {

            if (invalidate_cache || retry || retry2)
                validcache = 0;

            if (!validcache) {

                /* Reload the track buffer. */
                ret = readtrack(track_buffer_rd, RD_TRACK_BUFFER_SIZE);
                if (ret != ERR_NO_ERROR)
                    return ret;
                validcache = 1;

                /* Invalidate the sector-position table. */
                memset(sector_pos, 0xff, sizeof(sector_pos));

                for (i = 0; i < (RD_TRACK_BUFFER_SIZE - 1088);) {
                    /* Find sync mark (4489). */
                    while ((i < RD_TRACK_BUFFER_SIZE)
                           && (track_buffer_rd[i] != 0x4489))
                        i++;
                    if (i == RD_TRACK_BUFFER_SIZE)
                        break;

                    /* Skip past sync marks (4489). */
                    while ((i < RD_TRACK_BUFFER_SIZE)
                           && (track_buffer_rd[i] == 0x4489))
                        i++;
                    if (i == RD_TRACK_BUFFER_SIZE)
                        break;

                    /* IDAM (0xFE)? */
                    if (MFMTOBIN(track_buffer_rd[i]) != 0xFE)
                        continue;

                    sect_num = MFMTOBIN(track_buffer_rd[i+3]);
                    dbg_printf("pre-cache sector: IDAM at %d, sector %d\n",
                               i, sect_num);

                    if ((sect_num < MAX_CACHE_SECTOR)
                        && (sector_pos[sect_num] == 0xffff)
                        && (i < (RD_TRACK_BUFFER_SIZE - 1088))) {
                        sector_pos[sect_num] = i;
                        dbg_printf("pre-cache sector: %d - %d\n", sect_num, i);
                    }

                    i += 512 + 2;
                }
            }

            /* Check if we have this sector cached. Retry if not. */
            i = sector_pos[sectornum];
            if (i >= (RD_TRACK_BUFFER_SIZE - 1088))
                continue;

            dbg_printf("sector %d offset %d\n", sectornum, i);

            /* Check if we have a valid sector header. */
            for (j = 0; j < 7; j++)
                if (MFMTOBIN(track_buffer_rd[i+j]) != sector_header[j])
                    break;
            if (j != 7)
                continue;
            dbg_printf("Valid header found\n");

            /* Search for the DAM. */
            for (j = 0, i += 35; j < 30; j++, i++)
                if (MFMTOBIN(track_buffer_rd[i]) == 0xFB)
                    break;
            if (j == 30)
                continue;
            dbg_printf("Data mark found (%d)\n", j);

            /* CRC: Sync words + DAM. */
            crc = crc16_ccitt(header, 4, 0xffff);

            /* Data (copy and CRC). */
            for (j = 0, i++; j < 512; j++, i++) {
                tc = MFMTOBIN(track_buffer_rd[i]);
                data[j] = tc;
            }
            crc = crc16_ccitt(data, 512, crc);

            /* CRC */
            for (j = 0; j < 2; j++, i++) {
                tc = MFMTOBIN(track_buffer_rd[i]);
                crc = crc16_ccitt(&tc, 1, crc);
            }

            sectorfound = !crc;
        }

        if (!sectorfound) {
            ret = jumptotrack(255);
            if (ret != ERR_NO_ERROR)
                return ret;
        }
    }

    if (!sectorfound)
        validcache = 0;

    return sectorfound ? ERR_NO_ERROR : -ERR_MEDIA_READ_SECTOR_NOT_FOUND;
}

int init_fdc(int drive)
{
    uint16_t i;
    int ret;

    dbg_printf("init_fdc\n");

    if (!test_drive(drive))
        return -ERR_DRIVE_NOT_FOUND;

    /* Select drive, side 0. Don't bother enabling drive motor for DF0.
     * External drives we keep motor enabled as some external drive enclosures
     * gate the TRK0 signal when the motor is off.  */
    drive_select(drive, drive != 0);
    ciab->prb |= CIABPRB_SIDE; /* side 0 */

    validcache = 0;

    for (i = 0; i < 256; i++) {
        mfmtobinLUT_L[i] = (((i&0x40)>>3) | ((i&0x10)>>2) |
                            ((i&0x04)>>1) | (i&0x01));
        mfmtobinLUT_H[i] = mfmtobinLUT_L[i] << 4;
    }

    ret = jumptotrack(255);
    if (ret != ERR_NO_ERROR)
        return ret;

    delay_ms(200);
    cust->intreq = 2;

    return ERR_NO_ERROR;
}

void deinit_fdc(void)
{
    jumptotrack(40);
}


/****************************************************************************
 *                          Joystick / Keyboard I/O
 ****************************************************************************/

static uint8_t Joystick(void)
{
    uint16_t code = cust->joy1dat;
    uint8_t bcode = ciaa->pra;
    uint8_t ret = 0;

    if ( (code&0x100) ^ ((code&0x200)>>1) ) /* Up */
        ret |= 1;
    if ( ((code&0x200)) )  /* Left */
        ret |= 8;
    if ( (code&0x1) ^ ((code&0x2)>>1) ) /* Down */
        ret |= 2;
    if ( ((code&0x002)) )  /* Right */
        ret |= 4;
    if (!(bcode&0x80)) /* Fire */
        ret |= 0x10;

    return ret;
}

static volatile uint8_t key_buffer = 0x80;
static void keyboard_IRQ(void)
{
    uint16_t t_s;
    uint8_t keycode;

    /* Grab the keycode and begin handshake. */
    keycode = ~ciaa->sdr;
    ciaa->cra |= CIACRA_SPMODE; /* start the handshake */
    t_s = get_ciaatb();

    /* Decode the keycode. */
    key_buffer = (keycode >> 1) | (keycode << 7); /* ROR 1 */

    /* Wait to finish handshake over the serial line. We wait for 65 CIA ticks,
     * which is approx 90us: Longer than the 85us minimum dictated by the
     * HRM. */
    while ((uint16_t)(t_s - get_ciaatb()) < 65)
        continue;
    ciaa->cra &= ~CIACRA_SPMODE; /* finish the handshake */
}

IRQ(CIAA_IRQ);
static void c_CIAA_IRQ(void)
{
    uint8_t icr = ciaa->icr;

    if (icr & CIAICR_SERIAL)
        keyboard_IRQ();

    /* NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we get a 
     * spurious extra interrupt, since intreq.ciaa latches the level of CIAA 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciaa second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    IRQ_RESET(INT_CIAA);
}

void flush_char(void)
{
    key_buffer = 0x80;
}

uint8_t get_char(void)
{
    uint8_t i, key;
    uint8_t function_code, key_code;

    function_code = FCT_NO_FUNCTION;

    while (function_code == FCT_NO_FUNCTION) {
        while ((key = key_buffer) & 0x80)
            continue;
        key_buffer = 0x80;

        i = 0;
        do {
            function_code = char_keysmap[i].function_code;
            key_code = char_keysmap[i].keyboard_code;
            i++;
        } while ((key_code!=key) && (function_code!=FCT_NO_FUNCTION));
    }

    return function_code;
}


uint8_t wait_function_key(void)
{
    static uint8_t keyup;
    uint8_t joy, i, key;
    uint8_t function_code, key_code;

    function_code = FCT_NO_FUNCTION;

    key = key_buffer;
    joy = Joystick();
    if ((key & 0x80) && !joy)
        keyup = 2;

    /* If we are starting a key/joy repeat, delay a little longer before 
     * starting the repeat. */
    if (keyup == 1) {
        i = 0;
        /* Abort the delay if key-state changes. */
        while ((key == key_buffer) && (joy == Joystick()) && (i++ < 50))
            delay_ms(5);
    }

    while (function_code == FCT_NO_FUNCTION) {
        while (((key = key_buffer) & 0x80) && !(joy = Joystick()))
            keyup = 2;
        delay_ms(55);

        if (keyup)
            keyup--;

        if (joy) {
            if (joy & 0x10) {
                while (Joystick() & 0x10)
                    continue;
                return FCT_SELECT_FILE_DRIVEA;
            }
            if (joy & 2)
                return FCT_DOWN_KEY;
            if (joy & 1)
                return FCT_UP_KEY;
            if (joy & 4)
                return FCT_RIGHT_KEY;
            if (joy & 8)
                return FCT_LEFT_KEY;
        }

        i = 0;
        do {
            function_code = keysmap[i].function_code;
            key_code = keysmap[i].keyboard_code;
            i++;
        } while ((key_code!=key) && (function_code!=FCT_NO_FUNCTION) );
    }

    return function_code;
}

/****************************************************************************
 *                              Display Output
 ****************************************************************************/

/* Regardless of intrinsic PAL/NTSC-ness, display may be 50 or 60Hz. */
static uint8_t vbl_hz;

/* Display size and depth. */
#define bplsz   (640*256/8)
#define planes  2

static uint16_t *copper;

/* Wait for end of bitplane DMA. */
static void wait_bos(void)
{
    while (*(volatile uint8_t *)&cust->vhposr != 0xf0)
        continue;
}

static uint8_t detect_vbl_hz(void)
{
    uint32_t ticks;

    /* Synchronise to Vblank. */
    vblank_count = 0;
    while (!vblank_count)
        continue;
    ticks = get_time();

    /* Wait 10 blanks. */
    while (vblank_count < 11)
        continue;
    ticks = get_time() - ticks;

    /* Expected tick values: 
     *  NTSC: 10 * (715909 / 60) = 119318
     *  PAL:  10 * (709379 / 50) = 141875 
     * Use 130,000 as mid-point to discriminate.. */
    return (ticks > 130000) ? 50 : 60;
}

static void take_over_system(void)
{
    /* Disable all DMA and interrupts. */
    cust->intena = 0x7fff;
    cust->intreq = 0x7fff;
    while (!(cust->intreqr & INT_VBLANK))
        continue; /* wait for vbl before disabling sprite dma */
    cust->dmacon = 0x7fff;
    cust->adkcon = 0x7fff;
    cust->dsklen = 0x4000;

    /* Master DMA/IRQ enable. */
    cust->dmacon = 0x8200;
    cust->intena = 0xc000;

    /* Blank screen. */
    cust->color[0] = colortable[0];

    /* Floppy motors off. */
    ciab->prb = 0xff;
    ciab->prb = 0x87;
    ciab->prb = 0xff;

    /* Set keyboard serial line to input mode. */
    ciaa->cra &= ~CIACRA_SPMODE;

    /* Set up CIAA ICR. We only care about keyboard. */
    ciaa->icr = (uint8_t)~CIAICR_SETCLR;
    ciaa->icr = CIAICR_SETCLR | CIAICR_SERIAL;

    /* Set up CIAB ICR. We only care about FLAG line (disk index). */
    ciab->icr = (uint8_t)~CIAICR_SETCLR;
    ciab->icr = CIAICR_SETCLR | CIAICR_FLAG;

    /* Start all CIA timers in continuous mode. */
    ciaa->talo = ciaa->tahi = ciab->talo = ciab->tahi = 0xff;
    ciaa->tblo = ciaa->tbhi = ciab->tblo = ciab->tbhi = 0xff;
    ciaa->cra = ciab->cra = CIACRA_LOAD | CIACRA_START;
    ciaa->crb = ciab->crb = CIACRB_LOAD | CIACRB_START;
}

IRQ(VBLANK_IRQ);
static void c_VBLANK_IRQ(void)
{
    uint16_t cur16 = get_ciaatb();

    vblank_count++;

    stamp32 -= (uint16_t)(stamp16 - cur16);
    stamp16 = cur16;

    IRQ_RESET(INT_VBLANK);
}

int init_display(ui_context *ctx)
{
    static const uint16_t static_copper[] = {
        0x008e, 0x2c81, /* diwstrt */
        0x0090, 0x2cc1, /* diwstop */
        0x0092, 0x003c, /* ddfstrt */
        0x0094, 0x00d4, /* ddfstop */
        0x0100, 0xa200, /* bplcon0 */
        0x0102, 0x0000, /* bplcon1 */
        0x0104, 0x0000, /* bplcon2 */
        0x0108, 0x0000, 0x010a, 0x0000, /* bplxmod */
        0xffff, 0xfffe
    };

    uint16_t *p;
    unsigned int i;

    /* Done with AmigaOS. Take over. */
    take_over_system();

    /* Init copper. */
    p = copper;
    for (i = 0; i < 4; i++) {
        *p++ = 0x180 + i*2; /* color00-color03 */
        *p++ = colortable[i];
    }
    for (i = 0; i < 2; i++) {
        uint32_t bpl = (uint32_t)screen_buffer + i*bplsz;
        *p++ = 0xe0 + i*4; /* bplxpth */
        *p++ = (uint16_t)(bpl >> 16);
        *p++ = 0xe2 + i*4; /* bplxptl */
        *p++ = (uint16_t)bpl;
    }
    memcpy(p, (uint16_t *)static_copper, sizeof(static_copper));
    cust->cop1lc.p = copper;

    m68k_vec->level3_autovector.p = VBLANK_IRQ;
    m68k_vec->level2_autovector.p = CIAA_IRQ;
    m68k_vec->level6_autovector.p = CIAB_IRQ;

    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_COPEN | DMA_DSKEN;
    cust->intena = INT_SETCLR | INT_CIAA | INT_CIAB | INT_VBLANK;

    /* Detect our hardware environment. */
    vbl_hz = detect_vbl_hz();
    is_pal = detect_pal_chipset();
    cpu_hz = is_pal ? PAL_HZ : NTSC_HZ;

    /* 640x256 or 640x200 */
    ctx->SCREEN_XRESOL = 640;
    ctx->SCREEN_YRESOL = (vbl_hz == 50) ? 256 : 200;
    if (vbl_hz == 60) {
        /* Modify copper with correct DIWSTOP for NTSC. */
        for (p = copper; *p != 0x90; p += 2)
            continue;
        p[1] = 0xf4c1;
    }

    ctx->screen_txt_xsize = ctx->SCREEN_XRESOL / FONT_SIZE_X;
    ctx->screen_txt_ysize = ctx->SCREEN_YRESOL / FONT_SIZE_Y;

    /* Make sure the copper has run once through, then enable bitplane DMA. */
    delay_ms(1);
    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_BPLEN;

    return ERR_NO_ERROR;
}

uint8_t set_color_scheme(uint8_t color)
{
    UWORD *c = &colortable[(color&0x1F)*4];
    uint16_t *p;
    unsigned int i;

    /* Find colour section of copperlist. */
    for (p = copper; *p != 0x180; p += 2)
        continue;

    /* Load 4 colours into the copperlist. */
    for (i = 0; i < 4; i++) {
        p[1] = *c++;
        p += 2;
    }

    return color;
}

void print_char8x8(ui_context *ctx, int col, int line,
                   unsigned char c, int mode)
{
    int j;
    uint8_t *ptr_src = font_data;
    uint8_t *ptr_dst = screen_buffer;

    if ((col >= ctx->screen_txt_xsize) || (line >= ctx->screen_txt_ysize))
        return;

    ptr_dst += (line * 80 * 8) + col;
    ptr_src += c * ((FONT_SIZE_X*FONT_SIZE_Y)/8);
    for (j = 0; j < 8; j++) {
        *ptr_dst = *ptr_src ^ ((mode & INVERTED) ? 0xff : 0x00);
        ptr_src += 1;
        ptr_dst += 80;
    }
}

void clear_line(ui_context *ctx, int line, int mode)
{
    int i, j;
    uint16_t *ptr_dst = (uint16_t *)screen_buffer;
    int ptroffset;

    if (line >= ctx->screen_txt_ysize)
        return;

    for (j = 0; j < 8; j++) {
        ptroffset = 40 * (line*8 + j);
        for (i = 0; i < 40; i++)
            ptr_dst[ptroffset+i] = (mode & INVERTED) ? 0xFFFF : 0x0000;
    }
}

void invert_line(ui_context *ctx, int line)
{
    int i, j;
    uint16_t *ptr_dst = (uint16_t *)screen_buffer;
    int ptroffset;

    if (line >= ctx->screen_txt_ysize)
        return;

    for (j = 0; j < 8; j++) {
        ptroffset = 40 * (line*8 + j);
        for (i = 0; i < 40; i++)
            ptr_dst[ptroffset+i] ^= 0xFFFF;
    }
}

void reboot(void)
{
    _reboot();
    lockup();
}

void getvbr(void);
asm (
    "    .text ; .align 2;    \n"
    "_getvbr:                 \n"
    "    dc.l    0x4e7a0801   \n"     /* movec.l vbr,d0 */
    "    move.l  d0,_m68k_vec \n"
    "    rte                  \n"
    );

/* We abuse this hook to do all our system-friendly stuff before we knock 
 * the system on the head. */
int process_command_line(int argc, char *argv[])
{
    /* Allocate disk buffers. */
    mfmtobinLUT_L = AllocMem(256, MEMF_ANY);
    mfmtobinLUT_H = AllocMem(256, MEMF_ANY);
    track_buffer_rd = AllocMem(2 * RD_TRACK_BUFFER_SIZE, MEMF_CHIP);
    track_buffer_wr = AllocMem(2 * WR_TRACK_BUFFER_SIZE, MEMF_CHIP);

    /* Allocate display buffers. */
    screen_buffer = AllocMem(bplsz*planes, MEMF_CHIP|MEMF_CLEAR);
    copper = AllocMem(256, MEMF_CHIP);

    /* Fail on any allocation error. */
    if (!mfmtobinLUT_L || !mfmtobinLUT_H
        || !track_buffer_rd || !track_buffer_wr
        || !screen_buffer || !copper)
        return -1;

    /* Find FlashFloppy/HxC drive unit. */
    _get_start_unit();

    /* If running on 68010+ VBR may be non-zero. */
    if (SysBase->AttnFlags & AFF_68010)
        Supervisor((void *)getvbr);

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
