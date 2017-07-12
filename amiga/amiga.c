/*
 * Copyright (C) 2009-2017 Jean-François DEL NERO
 *
 * This file is part of the HxCFloppyEmulator file selector.
 *
 * HxCFloppyEmulator file selector may be used and distributed without
 * restriction provided that this copyright statement is not removed from the
 * file and that any derivative work contains the original copyright notice and
 * the associated disclaimer.
 *
 * HxCFloppyEmulator file selector is free software; you can redistribute it
 * and/or modify  it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the License, 
 * or (at your option) any later version.
 *
 * HxCFloppyEmulator file selector is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *   See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along 
 * with HxCFloppyEmulator file selector; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <exec/execbase.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdarg.h>
#include <stddef.h>

#include "conf.h"
#include "types.h"

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

#define DEPTH    2 /* 1 BitPlanes should be used, gives eight colours. */
#define COLOURS  2 /* 2^1 = 2                                          */

static volatile unsigned short io_floppy_timeout;

unsigned char * screen_buffer;
unsigned char * screen_buffer_backup;
unsigned short SCREEN_XRESOL;
unsigned short SCREEN_YRESOL;

static unsigned char CIABPRB_DSKSEL;

static unsigned char * mfmtobinLUT_L;
static unsigned char * mfmtobinLUT_H;

#define MFMTOBIN(W) ( mfmtobinLUT_H[W>>8] | mfmtobinLUT_L[W&0xFF] )

#define RD_TRACK_BUFFER_SIZE 10*1024
#define WR_TRACK_BUFFER_SIZE 600

static unsigned short * track_buffer_rd;
static unsigned short * track_buffer_wr;

static unsigned char validcache;

#define MAX_CACHE_SECTOR 16
unsigned short sector_pos[MAX_CACHE_SECTOR];

unsigned char keyup;

extern struct DosLibrary *DOSBase;

#if __GNUC__ < 3
#define attribute_used __attribute__((unused))
#define likely(x) x
#define unlikely(x) x
#else
#define attribute_used __attribute__((used))
#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#endif

#define barrier() asm volatile("" ::: "memory")

#ifndef offsetof
#define offsetof(a,b) __builtin_offsetof(a,b)
#endif
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Write to INTREQ twice at end of ISR to prevent spurious re-entry on 
 * A4000 with faster processors (040/060). */
#define IRQ_RESET(x) do {                       \
    uint16_t __x = (x);                         \
    cust->intreq = __x;                         \
    cust->intreq = __x;                         \
} while (0)
/* Similarly for disabling an IRQ, write INTENA twice to be sure that an 
 * interrupt won't creep in after the IRQ_DISABLE(). */
#define IRQ_DISABLE(x) do {                     \
    uint16_t __x = (x);                         \
    cust->intena = __x;                         \
    cust->intena = __x;                         \
} while (0)
#define IRQ_ENABLE(x) do {                      \
    uint16_t __x = INT_SETCLR | (x);            \
    cust->intena = __x;                         \
} while (0)

#define IRQ(name)                               \
static void c_##name(void) attribute_used;      \
void name(void);                                \
asm (                                           \
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

void push_serial_char(unsigned char byte)
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

    i = 0;
    while (txt_buffer[i])
    {
        if (txt_buffer[i] == '\n')
        {
            push_serial_char('\r');
            push_serial_char('\n');
        }
        else
            push_serial_char(txt_buffer[i]);

        i++;
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

void sleep(int secs)
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

/*
 * Returns the unit number of the underlying device of a filesystem lock.
 * Returns -1 on failure.
 */
LONG GetUnitNumFromLock(BPTR lock)
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

UWORD GetLibraryVersion(struct Library *library)
{
    dbg_printf("GetLibraryVersion : %d\n",library->lib_Version);

    return library->lib_Version;
}

int test_drive(int drive)
{
    int t,j,c;

    ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)));

    delay_ms(100);

    // Jump to Track 0 ("Slow")
    t = 0;
    while ((ciaa->pra & CIAAPRA_TK0) && (t<260))
    {
        ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)) | CIABPRB_STEP);
        delay_us(10);
        ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)));
        delay_us(80);

        t++;
    }

    if (t >= 260)
        goto fail;

    c = 0;
    do {
        // Jump to Track 40 (Fast)
        for (j = 0; j < 40; j++) {
            ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)) | CIABPRB_DIR |CIABPRB_STEP);
            delay_us(8);
            ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)) | CIABPRB_DIR);
            delay_us(8);
        }

        delay_us(200);

        // And go back to Track 0 (Slow)
        t = 0;
        while ((ciaa->pra & CIAAPRA_TK0) && (t < 40)) {
            ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3))  | CIABPRB_STEP);
            delay_us(10);
            ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)));
            delay_us(80);

            t++;
        }

        c++;
    } while ( (t != 40) && c < 2 );

    if (t == 40) {
        ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)));
        return 1;
    }

    ciab->prb = ~(CIABPRB_MTR | (CIABPRB_SEL0<<(drive&3)));

fail:
    return 0;
}

/* Do the system-friendly bit while AmigaOS is still alive. */
static int start_unit = -1;
static void _get_start_unit(void)
{
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
        return -1;

    for (i = 0; i < 4; i++) {
        if (test_drive(start_unit)) {
            dbg_printf("get_start_unit : drive %d\n", start_unit);
            return start_unit;
        }
        start_unit = (start_unit + 1) & 3;
    }

    dbg_printf("get_start_unit : drive not found !\n");
    return -1;
}

int jumptotrack(unsigned char t)
{
    unsigned short j,k;

    ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL);

    dbg_printf("jumptotrack : %d\n",t);

    delay_ms(100);

    dbg_printf("jumptotrack %d - seek track 0...\n",t);

    k = 0;
    while ((ciaa->pra & CIAAPRA_TK0) && k<1024) {
        ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL  | CIABPRB_STEP);
        delay_ms(1);
        ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL);
        delay_ms(1);
        k++;
    }

    if (k < 1024) {
        dbg_printf("jumptotrack %d - track 0 found\n",t);

        for (j = 0; j < t; j++) {
            ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL | CIABPRB_DIR |CIABPRB_STEP);
            delay_ms(1);
            ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL | CIABPRB_DIR);
            delay_ms(1);
        }

        ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL);

        dbg_printf("jumptotrack %d - jump done\n",t);

        return 0;
    }

    dbg_printf("jumptotrack %d - track 0 not found!!\n",t);
    return 1;
};

int waitindex(void)
{
    io_floppy_timeout = 0;

    do{
        asm("nop");
    } while ( (!(ciab->icr&0x10)) && ( io_floppy_timeout < 0x200 ) );

    do
    {
        asm("nop");
    } while ( (ciab->icr&0x10) && ( io_floppy_timeout < 0x200 ) );

    do{
        asm("nop");
    } while ((!(ciab->icr&0x10)) && ( io_floppy_timeout < 0x200 ) );

    return (io_floppy_timeout >= 0x200);
}

int readtrack(unsigned short * track,unsigned short size,unsigned char waiti)
{
    ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL);
    cust->dmacon = 0x8210;

    cust->intreq = 2;

    cust->dsklen = 0x4000;

    cust->dskpt.p = track;

    cust->adkcon = 0x7f00;
    cust->adkcon = 0x9500;
    cust->dmacon = 0x8210;
    cust->dsksync = 0x4489;
    cust->intreq = 2;

    if (waiti) {
        if (waitindex()) {
            hxc_printf_box("ERROR: READ - No Index Timeout ! (state %d)",(ciab->icr&0x10)>>4);
            lockup();
        }
    }

    cust->dsklen = size | 0x8000;
    cust->dsklen = size | 0x8000;

    while (!(cust->intreqr & 2))
        continue;
    cust->dsklen = 0x4000;
    cust->intreq = 2;

    validcache = 1;

    return 1;

}

int writetrack(unsigned short * track,unsigned short size,unsigned char waiti)
{
    ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL);
    cust->dmacon = 0x8210;

    cust->intreq = 2;

    cust->dsklen = 0x4000;

    cust->dskpt.p = track;

    cust->adkcon = 0x7f00;
    cust->adkcon = 0xb100;
    cust->dmacon = 0x8210;
    cust->dsksync = 0x4489;
    cust->intreq = 2;

    if (waiti) {
        io_floppy_timeout = 0;
        while ( ciab->icr&0x10 && ( io_floppy_timeout < 0x200 ) );
        while ( !(ciab->icr&0x10) && ( io_floppy_timeout < 0x200 ) );
        if (!( io_floppy_timeout < 0x200 )) {
            hxc_printf_box("ERROR: WRITE - No Index Timeout ! (state %d)",(ciab->icr&0x10)>>4);
            lockup();
        }
    }

    cust->dsklen = size | 0xc000;
    cust->dsklen = size | 0xc000;

    while (!(cust->intreqr & 2))
        continue;
    cust->dsklen = 0x4000;
    cust->intreq = 2;

    validcache = 0;

    return 1;
}

// Fast Bin to MFM converter
int BuildCylinder(unsigned char * mfm_buffer,int mfm_size,unsigned char * track_data,int track_size,unsigned short lastbit,unsigned short * retlastbit)
{
    int i,l;
    unsigned char byte;
    unsigned short mfm_code;

    if (track_size*2>mfm_size)
        track_size = mfm_size/2;

    // MFM Encoding
    i=0;
    for (l = 0; l < track_size; l++) {
        byte =track_data[l];

        mfm_code = MFM_tab[byte] & lastbit;

        mfm_buffer[i++]=mfm_code>>8;
        mfm_buffer[i++]=mfm_code&0xFF;

        lastbit=~(MFM_tab[byte]<<15);
    }

    if (retlastbit)
        *retlastbit = lastbit;

    return track_size;
}

unsigned char writesector(unsigned char sectornum,unsigned char * data)
{
    unsigned short i, j, len, retry, retry2, lastbit;
    unsigned char sectorfound;
    unsigned char c;
    unsigned char CRC16_High, CRC16_Low, byte;
    unsigned char sector_header[4];

    dbg_printf("writesector : %d\n",sectornum);

    retry2 = 2;

    i = 0;
    validcache = 0;

    // Preparing the buffer...
    CRC16_Init(&CRC16_High, &CRC16_Low);
    for (j = 0; j < 3; j++)
        CRC16_Update(&CRC16_High,&CRC16_Low,0xA1);

    CRC16_Update(&CRC16_High,&CRC16_Low,0xFB);

    for (j = 0; j < 512; j++)
        CRC16_Update(&CRC16_High,&CRC16_Low,data[j]);

    for (j = 0; j < 12; j++)
        track_buffer_wr[i++]=0xAAAA;

    track_buffer_wr[i++]=0x4489;
    track_buffer_wr[i++]=0x4489;
    track_buffer_wr[i++]=0x4489;
    lastbit = 0x7FFF;
    byte = 0xFB;
    BuildCylinder((unsigned char*)&track_buffer_wr[i++],1*2,&byte,1,lastbit,&lastbit);
    BuildCylinder((unsigned char*)&track_buffer_wr[i],512*2,data,512,lastbit,&lastbit);
    i += 512;
    BuildCylinder((unsigned char*)&track_buffer_wr[i++],1*2,&CRC16_High,1,lastbit,&lastbit);
    BuildCylinder((unsigned char*)&track_buffer_wr[i++],1*2,&CRC16_Low,1,lastbit,&lastbit);
    byte = 0x4E;
    for (j = 0; j < 4; j++)
        BuildCylinder((unsigned char*)&track_buffer_wr[i++],1*2,&byte,1,lastbit,&lastbit);

    len = i;


    // Looking for/waiting the sector to write...

    sector_header[0]=0xFF;
    sector_header[1]=0x00;
    sector_header[2]=sectornum;

    sectorfound = 0;
    retry = 30;

    if (sectornum) {

        do {

            do {

                i = 0;

                retry--;

                if (!readtrack(track_buffer_rd,16,0))
                    return 0;

                while (track_buffer_rd[i] == 0x4489 && (i<16))
                    i++;

                if (MFMTOBIN(track_buffer_rd[i])==0xFE && (i<(16-3))) {

                    CRC16_Init(&CRC16_High, &CRC16_Low);

                    for (j = 0; j < 3; j++)
                        CRC16_Update(&CRC16_High,&CRC16_Low,0xA1);

                    for (j = 0; j < (1+4+2); j++) {
                        c = MFMTOBIN(track_buffer_rd[i+j]);
                        CRC16_Update(&CRC16_High, &CRC16_Low,c);
                    }

                    if (!CRC16_High && !CRC16_Low) {
                        i++;

                        j = 0;
                        while (j<3 && ( MFMTOBIN(track_buffer_rd[i]) == sector_header[j])) { // track,side,sector
                            j++;
                            i++;
                        }

                        if (j == 3) {
                            sectorfound=1;
                            if (!writetrack(track_buffer_wr,len,0))
                                return 0;
                        }
                    }
                }
            } while (!sectorfound  && retry);

            if (!sectorfound) {
                if (jumptotrack(255)) {
                    hxc_printf_box("ERROR: writesector -> failure while seeking the track 00!");
                }
                retry=30;
            }
            retry2--;

        } while (!sectorfound && retry2);

    } else {
        sectorfound = 1;

        if (!writetrack(track_buffer_wr,len,1))
            return 0;
    }

    return sectorfound;
}


unsigned char readsector(unsigned char sectornum,unsigned char * data,unsigned char invalidate_cache)
{
    unsigned short i,j;
    unsigned char sectorfound,tc;
    unsigned char retry,retry2;
    unsigned char CRC16_High,CRC16_Low;
    unsigned char sector_header[8];
    unsigned char sect_num;

    dbg_printf("readsector : %d - %d\n",sectornum,invalidate_cache);

    if (!(sectornum<MAX_CACHE_SECTOR))
        return 0;

    retry2 = 2;
    retry = 5;

    sector_header[0] = 0xFE; // IDAM
    sector_header[1] = 0xFF; // Track
    sector_header[2] = 0x00; // Side
    sector_header[3] = sectornum; // Sector
    sector_header[4] = 0x02;      // Size

    CRC16_Init(&CRC16_High, &CRC16_Low);
    for (j = 0; j < 3; j++)
        CRC16_Update(&CRC16_High,&CRC16_Low,0xA1);

    for (j = 0; j < 5; j++)
        CRC16_Update(&CRC16_High, &CRC16_Low,sector_header[j]);

    sector_header[5] = CRC16_High;// CRC H
    sector_header[6] = CRC16_Low; // CRC L

    do {
        do {
            sectorfound = 0;
            i = 0;
            if (!validcache || invalidate_cache) {
                if (!readtrack(track_buffer_rd,RD_TRACK_BUFFER_SIZE,0))
                    return 0;

                i = 1;
                for (j = 0; j < MAX_CACHE_SECTOR; j++)
                    sector_pos[j]=0xFFFF;

                for (j = 0; j < 9; j++) {
                    while (i < RD_TRACK_BUFFER_SIZE && ( track_buffer_rd[i]!=0x4489 ))
                        i++;

                    if (i == RD_TRACK_BUFFER_SIZE)
                        break;

                    while (i < RD_TRACK_BUFFER_SIZE && (track_buffer_rd[i]==0x4489 ))
                        i++;

                    if (i == RD_TRACK_BUFFER_SIZE)
                        break;

                    if (MFMTOBIN(track_buffer_rd[i]) == 0xFE) {
                        dbg_printf("pre-cache sector : index mark at %d sector %d\n",i,MFMTOBIN(track_buffer_rd[i+3]));

                        sect_num = MFMTOBIN(track_buffer_rd[i+3]);
                        if (sect_num < MAX_CACHE_SECTOR) {
                            if (sector_pos[sect_num] == 0xFFFF) {
                                if (i < (RD_TRACK_BUFFER_SIZE - 1088)) {
                                    sector_pos[sect_num] = i;
                                    dbg_printf("pre-cache sector : %d - %d\n",sect_num,i);
                                }

                            } else {
                                dbg_printf("pre-cache sector : sector already found : %d sector %d\n",i,sector_pos[sect_num]);
                            }
                        }

                        i += ( 512 + 2 );
                    } else {
                        i++;
                    }
                }
            }

            i = sector_pos[sectornum];

            dbg_printf("sector %d offset %d\n",sectornum,i);

            if (i < (RD_TRACK_BUFFER_SIZE - 1088)) {
                // Check if we have a valid sector header
                j = 0;
                while (j<7 && ( MFMTOBIN(track_buffer_rd[i+j]) == sector_header[j] ) ) { // track,side,sector
                    j++;
                }

                if (j == 7) { // yes
                    dbg_printf("Valid header found\n");

                    i += 35;

                    j = 0;
                    while (j<30 && (MFMTOBIN(track_buffer_rd[i]) != 0xFB)) { // Data mark
                        i++;
                        j++;
                    }

                    if (j != 30) {
                        dbg_printf("Data mark found (%d)\n",j);

                        // 0xA1 * 3
                        CRC16_Init(&CRC16_High, &CRC16_Low);
                        for (j=0;j<3;j++)
                            CRC16_Update(&CRC16_High,&CRC16_Low,0xA1);

                        // Data Mark
                        CRC16_Update(&CRC16_High,&CRC16_Low,MFMTOBIN(track_buffer_rd[i]));
                        i++;

                        // Data
                        for (j = 0; j < 512; j++) {
                            tc = MFMTOBIN(track_buffer_rd[i]);
                            i++;
                            data[j] = tc;
                        }

                        for (j = 0; j < 2; j++) {
                            tc = MFMTOBIN(track_buffer_rd[i]);
                            i++;
                            CRC16_Update(&CRC16_High, &CRC16_Low,tc);
                        }

                        if (1)//!CRC16_High && !CRC16_Low)
                            sectorfound = 1;
                    }
                }
            }

            retry--;
            if (!sectorfound && retry)
                validcache=0;

        } while (!sectorfound && retry);


        if (!sectorfound) {
            if (jumptotrack(255)) {
                hxc_printf_box("ERROR: readsector -> failure while seeking the track 00!");
            }

            retry2--;
            retry=5;
        }

    } while (!sectorfound && retry2);

    if (!sectorfound)
        validcache=0;

    return sectorfound;
}

void init_fdc(int drive)
{
    unsigned short i;

    dbg_printf("init_fdc\n");

    CIABPRB_DSKSEL = CIABPRB_SEL0 << (drive & 3);

    validcache = 0;

    for (i = 0; i < 256; i++) {
        mfmtobinLUT_L[i] = (((i&0x40)>>3) | ((i&0x10)>>2) |
                            ((i&0x04)>>1) | (i&0x01));
        mfmtobinLUT_H[i] = mfmtobinLUT_L[i] << 4;
    }

    ciab->prb = ~(CIABPRB_MTR | CIABPRB_DSKSEL);
    cust->dmacon = DMA_SETCLR | DMA_DSKEN;

    if (jumptotrack(255)) {
        hxc_printf_box("ERROR: init_fdc drive %d -> failure "
                       "while seeking the track 00!", drive);
        lockup();
    }

    delay_ms(200);
    cust->intreq = 2;
}

/****************************************************************************
 *                          Joystick / Keyboard I/O
 ****************************************************************************/

static unsigned char Joystick(void)
{
    unsigned short code;
    unsigned char bcode;
    unsigned char ret;

    code = cust->joy1dat;
    bcode = ciaa->pra;

    ret=0;
    if ( (code&0x100) ^ ((code&0x200)>>1) ) // Forward
    {
        ret=ret| 0x1;
    }
    if ( ((code&0x200)) )  // Left
    {
        ret=ret| 0x8;
    }

    if ( (code&0x1) ^ ((code&0x2)>>1) ) // Back
    {
        ret=ret| 0x2;
    }

    if ( ((code&0x002)) )  // Right
    {
        ret=ret| 0x4;
    }

    if (!(bcode&0x80))
    {
        ret=ret| 0x10;
    }

    return( ret );
}

static unsigned char Keyboard(void)
{
    unsigned char code;

    /* Get a copy of the SDR value and invert it: */
    code = ~ciaa->sdr;

    /* Shift all bits one step to the right, and put the bit that is */
    /* pushed out last: 76543210 -> 07654321                         */
    code = code & 0x01 ? (code>>1)+0x80 : code>>1;

    /* Return the Raw Key Code Value: */
    return( code );
}

void flush_char(void)
{
}

unsigned char get_char(void)
{
    unsigned char key, i, c;
    unsigned char function_code, key_code;

    function_code = FCT_NO_FUNCTION;
    while (!(Keyboard()&0x80))
        continue;

    do {
        c = 1;
        do {
            do {
                key = Keyboard();
                if (key & 0x80)
                   c=1;
            } while (key & 0x80);
            delay_ms(55);
            c--;
        } while (c);

        i = 0;
        do {
            function_code = char_keysmap[i].function_code;
            key_code = char_keysmap[i].keyboard_code;
            i++;
        } while ((key_code!=key) && (function_code!=FCT_NO_FUNCTION));

    } while (function_code==FCT_NO_FUNCTION);

    return function_code;
}


unsigned char wait_function_key(void)
{
    unsigned char key, joy, i, c;
    unsigned char function_code, key_code;

    function_code = FCT_NO_FUNCTION;

    if (keyup == 1)
        delay_ms(250);

    do {
        c = 1;
        do {
            do {
                key=Keyboard();
                joy=Joystick();
                if ((key & 0x80) && !joy)
                {
                    c = 1;
                    keyup = 2;
                }
            } while ((key & 0x80) && !joy);

            delay_ms(55);

            c--;

        } while (c);

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

        i=0;
        do {
            function_code = keysmap[i].function_code;
            key_code = keysmap[i].keyboard_code;
            i++;
        } while ((key_code!=key) && (function_code!=FCT_NO_FUNCTION) );

    } while (function_code==FCT_NO_FUNCTION);

    return function_code;
}

/****************************************************************************
 *                              Display Output
 ****************************************************************************/

/* Regardless of intrinsic PAL/NTSC-ness, display may be 50 or 60Hz. */
static uint8_t vbl_hz;

/* Display size and depth. */
#define xres    640
#define yres    256
#define bplsz   (yres*xres/8)
#define planes  2

/* Top-left coordinates of the display. */
#define diwstrt_h 0x81
#define diwstrt_v 0x46

static uint16_t *copper;

/* Wait for end of bitplane DMA. */
void wait_bos(void)
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

    /* Master DMA/IRQ enable. */
    cust->dmacon = 0x8200;
    cust->intena = 0xc000;

    /* Blank screen. */
    cust->color[0] = colortable[0];

    /* Floppy motors off. */
    ciab->prb = 0xf8;
    ciab->prb = 0x87;
    ciab->prb = 0x78;

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
    vblank_count++;
    io_floppy_timeout++;

    if ((Keyboard() & 0x80) && !Joystick())
        keyup = 2;

    IRQ_RESET(INT_VBLANK);
}

int init_display(void)
{
    static const uint16_t static_copper[] = {
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
    *p++ = 0x008e; /* diwstrt */
    *p++ = (diwstrt_v << 8) | diwstrt_h;
    *p++ = 0x0090; /* diwstop */
    *p++ = (((diwstrt_v+yres) & 0xFF) << 8) | ((diwstrt_h+xres/2) & 0xFF);
    memcpy(p, (uint16_t *)static_copper, sizeof(static_copper));
    cust->cop1lc.p = copper;

    m68k_vec->level3_autovector.p = VBLANK_IRQ;

    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_COPEN | DMA_DSKEN;
    cust->intena = INT_SETCLR | INT_VBLANK;

    /* Detect our hardware environment. */
    vbl_hz = detect_vbl_hz();
    is_pal = detect_pal_chipset();
    cpu_hz = is_pal ? PAL_HZ : NTSC_HZ;

    /* 640x256 or 640x200 */
    SCREEN_XRESOL = 640;
    SCREEN_YRESOL = (vbl_hz == 50) ? 256 : 200;
    if (vbl_hz == 60) {
        /* Modify copper with correct DIWSTOP for NTSC. */
        for (p = copper; *p != 0x90; p += 2)
            continue;
        p[1] = (((diwstrt_v+SCREEN_YRESOL) & 0xFF) << 8)
            | ((diwstrt_h+xres/2) & 0xFF);
    }

    /* Make sure the copper has run once through, then enable bitplane DMA. */
    delay_ms(1);
    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_BPLEN;

    return 0;
}

unsigned char set_color_scheme(unsigned char color)
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

void print_char8x8(unsigned char *membuffer, bmaptype *font,
                   int x, int y, unsigned char c)
{
    int j;
    unsigned char *ptr_src;
    unsigned char *ptr_dst;

    ptr_dst = (unsigned char*)membuffer;
    ptr_src = (unsigned char*)&font->data[0];

    x = x>>3;
    //x=((x&(~0x1))<<1)+(x&1);//  0 1   2 3
    ptr_dst += ((y*80)+ x);
    ptr_src += (((c>>4)*(8*8*2))+(c&0xF));
    for ( j = 0; j < 8; j++) {
        *ptr_dst = *ptr_src;
        ptr_src += 16;
        ptr_dst += 80;
    }
}

void display_sprite(unsigned char *membuffer, bmaptype * sprite,int x, int y)
{
    int i, j, base_offset;
    unsigned short k, l;
    unsigned short *ptr_src;
    unsigned short *ptr_dst;

    ptr_dst = (unsigned short*)membuffer;
    ptr_src = (unsigned short*)&sprite->data[0];

    k = 0;
    l = 0;
    base_offset = ((y*80)+ ((x>>3)))/2;
    for (j = 0; j < sprite->Ysize; j++) {
        l = base_offset + (40*j);
        for (i = 0;i < (sprite->Xsize/16); i++) {
            ptr_dst[l] = ptr_src[k];
            l++;
            k++;
        }
    }
}

void h_line(int y_pos, unsigned short val)
{
    unsigned short *ptr_dst;
    int i, ptroffset;

    ptr_dst = (unsigned short*)screen_buffer;
    ptroffset = 40* y_pos;

    for (i = 0; i < 40; i++)
        ptr_dst[ptroffset+i] = val;
}

void box(int x_p1, int y_p1, int x_p2, int y_p2,
         unsigned short fillval, unsigned char fill)
{
    unsigned short *ptr_dst;
    int i, j, ptroffset, x_size;

    ptr_dst = (unsigned short*)screen_buffer;

    x_size = ((x_p2-x_p1)/16)*2;

    ptroffset = 80 * y_p1;
    for (j = 0; j < (y_p2 - y_p1); j++) {
        for (i = 0; i < x_size; i++) {
            ptr_dst[ptroffset+i] = fillval;
        }
        ptroffset = 80 * (y_p1+j);
    }
}

void invert_line(int x_pos,int y_pos)
{
    int i,j;
    unsigned short *ptr_dst;
    int ptroffset;

    for (j = 0; j < 8; j++) {
        ptr_dst=(unsigned short*)screen_buffer;
        ptroffset = (40 * (y_pos+j)) + x_pos;

        for (i = 0; i < 40; i++)
            ptr_dst[ptroffset+i] = ptr_dst[ptroffset+i] ^ 0xFFFF;
    }
}

void save_box(void)
{
    memcpy(screen_buffer_backup,&screen_buffer[160*70], 8*1024);
}

void restore_box(void)
{
    memcpy(&screen_buffer[160*70],screen_buffer_backup, 8*1024);
}

void reboot(void)
{
    _reboot();
    lockup();
}

void getvbr(void);
asm (
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
    screen_buffer_backup = AllocMem(8*1024, MEMF_ANY);
    screen_buffer = AllocMem(bplsz*planes, MEMF_CHIP|MEMF_CLEAR);
    copper = AllocMem(256, MEMF_CHIP);

    /* Fail on any allocation error. */
    if (!mfmtobinLUT_L || !mfmtobinLUT_H
        || !track_buffer_rd || !track_buffer_wr
        || !screen_buffer_backup || !screen_buffer || !copper)
        return -1;

    /* Find FlashFloppy/HxC drive unit. */
    _get_start_unit();

    /* If running on 68010+ VBR may be non-zero. */
    if (SysBase->AttnFlags & AFF_68010)
        Supervisor((void *)getvbr);

    return 0;
}
