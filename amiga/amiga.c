/*
 *
 * Copyright (C) 2009-2017 Jean-François DEL NERO
 *
 * This file is part of the HxCFloppyEmulator file selector.
 *
 * HxCFloppyEmulator file selector may be used and distributed without restriction
 * provided that this copyright statement is not removed from the file and that any
 * derivative work contains the original copyright notice and the associated
 * disclaimer.
 *
 * HxCFloppyEmulator file selector is free software; you can redistribute it
 * and/or modify  it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * HxCFloppyEmulator file selector is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *   See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HxCFloppyEmulator file selector; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
*/

#include <intuition/intuitionbase.h>

#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <graphics/gfxbase.h>
#include <graphics/videocontrol.h>

#include <devices/trackdisk.h>

#include <exec/interrupts.h>

#include <clib/exec_protos.h>
#include <clib/dos_protos.h>

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdarg.h>

#include "conf.h"

#include "keysfunc_defs.h"
#include "keys_defs.h"
#include "keymap.h"

#include "hardware.h"
#include "amiga_regs.h"

#include "gui_utils.h"

#include "reboot.h"

#include "crc.h"

#include "color_table.h"
#include "mfm_table.h"


#define DEPTH    2 /* 1 BitPlanes should be used, gives eight colours. */
#define COLOURS  2 /* 2^1 = 2                                          */

volatile unsigned short io_floppy_timeout;

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

unsigned long timercnt;

struct Interrupt *rbfint, *priorint;

struct Library * libptr;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBaseptr;
struct View * my_old_view;
struct View view;
struct ViewPort viewPort = { 0 };
struct RasInfo rasInfo;
struct BitMap my_bit_map;
struct RastPort my_rast_port;
struct Screen *screen;
extern struct DosLibrary *DOSBase;
UWORD  *pointer;
struct ColorMap *cm=NULL;

struct TextAttr MyFont =
{
    (STRPTR)"topaz.font", // Font Name
    TOPAZ_SIXTY, // Font Height
    FS_NORMAL, // Style
    FPF_ROMFONT, // Preferences
};

struct NewScreen screen_cfg =
{
    (WORD)0,   /* the LeftEdge should be equal to zero */
    (WORD)0,   /* TopEdge */
    (WORD)640, /* Width (low-resolution) */
    (WORD)256, /* Height (non-interlace) */
    (WORD)1,   /* Depth (4 colors will be available) */
    (UBYTE)0, (UBYTE)1, /* the DetailPen and BlockPen specifications */
    (UWORD)0,  /* no special display modes */
    CUSTOMSCREEN, /* the screen type */
    &MyFont, /* use my own font */
    (UBYTE *)"HxC Floppy Emulator file selector", /* this declaration is compiled as a text pointer */
    (struct Gadget *)NULL, /* no special screen gadgets */
    (struct BitMap *)NULL  /* no special CustomBitMap */
};

#ifdef DEBUG

void push_serial_char(unsigned char byte)
{
    WRITEREG_W(0xDFF032,0x1E);              //SERPER - 115200 baud/s

    while (!(READREG_W(0xDFF018) & 0x2000)); //SERDATR

    WRITEREG_W(0xDFF030,byte | 0x100);      //SERDAT
}

void dbg_printf(char * chaine, ...)
{
    unsigned char txt_buffer[1024];
    int i;

    va_list marker;
    va_start( marker, chaine );

    vsnprintf(txt_buffer,sizeof(txt_buffer),chaine,marker);

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

    va_end( marker );
}

#else

#define dbg_printf(_f, ...) ((void)0)

#endif

void waitus(int centus)
{
    unsigned short time;

    WRITEREG_B(CIAB_CRA, (READREG_B(CIAB_CRA)&0xC0) | 0x08 );
    WRITEREG_B(CIAB_ICR, 0x7F );

    time = 0x48 * centus;
    WRITEREG_B(CIAB_TALO, time&0xFF );
    WRITEREG_B(CIAB_TAHI, time>>8 );

    WRITEREG_B(CIAB_CRA, READREG_B(CIAB_CRA) | 0x01 );

    do
    {
    } while (!(READREG_B(CIAB_ICR)&1));
}

void waitms(int ms)
{
    int cnt;

    WRITEREG_B(CIAB_CRA, (READREG_B(CIAB_CRA)&0xC0) | 0x08 );
    WRITEREG_B(CIAB_ICR, 0x7F );

    WRITEREG_B(CIAB_TALO, 0xCC );
    WRITEREG_B(CIAB_TAHI, 0x02 );

    WRITEREG_B(CIAB_CRA, READREG_B(CIAB_CRA) | 0x01 );
    for (cnt=0;cnt<ms;cnt++)
    {
        do
        {
        } while (!(READREG_B(CIAB_ICR)&1));

        WRITEREG_B(CIAB_CRA, READREG_B(CIAB_CRA) | 0x01 );
    }
}

void alloc_error()
{
    hxc_printf_box("ERROR: Memory Allocation Error -> No more free mem ?");
    lockup();
}

void lockup()
{
    dbg_printf("lockup : Sofware halted...\n");

    for (;;)
        sleep(100);
}
/********************************************************************************
 *                              FDC I/O
 *********************************************************************************/

/*
 * Returns the unit number of the underlying device of a filesystem lock.
 * Returns -1 on failure.
 */
LONG GetUnitNumFromLock(BPTR lock) {
    LONG unitNum = -1;
    if (lock != 0) {
        struct InfoData *infoData = AllocMem(sizeof(struct InfoData), MEMF_ANY);
        if (NULL != infoData) {
            if (Info(lock, infoData)) {
                unitNum = infoData->id_UnitNumber;
            }
            FreeMem(infoData, sizeof(struct InfoData));
        }
    }

    dbg_printf("GetUnitNumFromLock : %d\n", unitNum);
    return unitNum;
}

/*
 * Returns the unit number of the underlying device of a filesystem path.
 * Returns -1 on failure.
 */
LONG GetUnitNumFromPath(char *path) {
    LONG unitNum = -1;

    if (path)
    {
        dbg_printf("GetUnitNumFromPath : %s\n",path);

        BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
        if (lock != 0) {
            unitNum = GetUnitNumFromLock(lock);
            UnLock(lock);
        }
        else
        {
            dbg_printf("GetUnitNumFromPath : Lock failed !\n");
        }
    }
    else
    {
        dbg_printf("GetUnitNumFromPath : NULL path !\n");
    }
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
    Forbid();
    WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) ));

    waitms(100);

    // Jump to Track 0 ("Slow")
    t = 0;
    while ((READREG_B(CIAAPRA) & CIAAPRA_DSKTRACK0) && (t<260))
    {
        WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3))  | CIABPRB_DSKSTEP));
        waitus(10);
        WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) ) );
        waitus(80);

        t++;
    }

    if (t >= 260)
        goto fail;

    c = 0;
    do {
        // Jump to Track 40 (Fast)
        for (j = 0; j < 40; j++) {
            WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) | CIABPRB_DSKDIREC |CIABPRB_DSKSTEP) );
            waitus(8);
            WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) | CIABPRB_DSKDIREC ) );
            waitus(8);
        }

        waitus(200);

        // And go back to Track 0 (Slow)
        t = 0;
        while ((READREG_B(CIAAPRA) & CIAAPRA_DSKTRACK0) && (t < 40)) {
            WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3))  | CIABPRB_DSKSTEP));
            waitus(10);
            WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) ) );
            waitus(80);

            t++;
        }

        c++;
    } while ( (t != 40) && c < 2 );

    if (t == 40) {
        WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) ) );

        Permit();
        return 1;
    }

    WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | (CIABPRB_DSKSEL0<<(drive&3)) ) );

fail:
    Permit();
    return 0;
}

int get_start_unit(char * path)
{
    int i;
    LONG startedFromUnitNum;

    dbg_printf("get_start_unit\n");

    if (GetLibraryVersion((struct Library *) DOSBase) >= 36)
        startedFromUnitNum = GetUnitNumFromLock( GetProgramDir() );
    else
        startedFromUnitNum = GetUnitNumFromPath( path );

    if ( startedFromUnitNum < 0 )
        startedFromUnitNum = 0;

    for ( i = 0; i < 4; i++ ) {
        if (test_drive((startedFromUnitNum + i) & 0x3)) {
            dbg_printf("get_start_unit : drive %d\n",(startedFromUnitNum + i) & 0x3);
            return (startedFromUnitNum + i) & 0x3;
        }
    }

    dbg_printf("get_start_unit : drive not found !\n");

    return -1;
}

int jumptotrack(unsigned char t)
{
    unsigned short j,k;

    Forbid();
    WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL ));

    dbg_printf("jumptotrack : %d\n",t);

    waitms(100);

    dbg_printf("jumptotrack %d - seek track 0...\n",t);

    k = 0;
    while ((READREG_B(CIAAPRA) & CIAAPRA_DSKTRACK0) && k<1024) {
        WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL  | CIABPRB_DSKSTEP));
        waitms(1);
        WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL ) );
        waitms(1);

        k++;
    }

    if (k < 1024) {
        dbg_printf("jumptotrack %d - track 0 found\n",t);

        for (j = 0; j < t; j++) {
            WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL | CIABPRB_DSKDIREC |CIABPRB_DSKSTEP) );
            waitms(1);
            WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL | CIABPRB_DSKDIREC ) );
            waitms(1);
        }

        WRITEREG_B(CIABPRB, ~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL ) );

        dbg_printf("jumptotrack %d - jump done\n",t);

        Permit();

        return 0;
    }

    dbg_printf("jumptotrack %d - track 0 not found!!\n",t);

    Permit();
    return 1;
};

int waitindex(void)
{
    io_floppy_timeout = 0;

    do{
        asm("nop");
    } while ( (!(READREG_B(CIAB_ICR)&0x10)) && ( io_floppy_timeout < 0x200 ) );

    do
    {
        asm("nop");
    } while ( (READREG_B(CIAB_ICR)&0x10) && ( io_floppy_timeout < 0x200 ) );

    do{
        asm("nop");
    } while ((!(READREG_B(CIAB_ICR)&0x10)) && ( io_floppy_timeout < 0x200 ) );

    return (io_floppy_timeout >= 0x200);
}

int readtrack(unsigned short * track,unsigned short size,unsigned char waiti)
{
    WRITEREG_B(CIABPRB,~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL));
    WRITEREG_W( DMACON,0x8210);

    WRITEREG_W(INTREQ,0x0002);

    // set dsklen to 0x4000
    WRITEREG_W( DSKLEN ,0x4000);

    WRITEREG_L( DSKPTH ,track);

    WRITEREG_W( ADKCON, 0x7F00);
    WRITEREG_W( ADKCON, 0x9500); //9500
    WRITEREG_W( DMACON, 0x8210);
    WRITEREG_W( DSKSYNC,0x4489);
    WRITEREG_W( INTREQ, 0x0002);

    if (waiti) {
        if (waitindex()) {
            hxc_printf_box("ERROR: READ - No Index Timeout ! (state %d)",(READREG_B(CIAB_ICR)&0x10)>>4);
            lockup();
        }
    }

    //Put the value you want into the DSKLEN register
    WRITEREG_W( DSKLEN ,size | 0x8000);
    //Write this value again into the DSKLEN register. This actually starts the DMA.
    WRITEREG_W( DSKLEN ,size | 0x8000);

    while (!(READREG_W(INTREQR)&0x0002));
    WRITEREG_W( DSKLEN ,0x4000);
    WRITEREG_W(INTREQ,0x0002);

    validcache=1;

    return 1;

}

int writetrack(unsigned short * track,unsigned short size,unsigned char waiti)
{

//    while (!(READREG_W(INTREQR)&0x0002));

    WRITEREG_B(CIABPRB,~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL));
    WRITEREG_W( DMACON,0x8210);

    WRITEREG_W(INTREQ,0x0002);

    // set dsklen to 0x4000
    WRITEREG_W( DSKLEN ,0x4000);

    WRITEREG_L( DSKPTH ,track);

    WRITEREG_W( ADKCON, 0x7F00);
    WRITEREG_W( ADKCON, 0xB100); //9500
    WRITEREG_W( DMACON, 0x8210);
    WRITEREG_W( DSKSYNC,0x4489);
    WRITEREG_W( INTREQ, 0x0002);

    if (waiti) {
        io_floppy_timeout = 0;
        while ( READREG_B(CIAB_ICR)&0x10 && ( io_floppy_timeout < 0x200 ) );
        while ( !(READREG_B(CIAB_ICR)&0x10) && ( io_floppy_timeout < 0x200 ) );
        if (!( io_floppy_timeout < 0x200 )) {
            hxc_printf_box("ERROR: WRITE - No Index Timeout ! (state %d)",(READREG_B(CIAB_ICR)&0x10)>>4);
            lockup();
        }
    }

    //Put the value you want into the DSKLEN register
    WRITEREG_W( DSKLEN ,size | 0x8000 | 0x4000 );
    //Write this value again into the DSKLEN register. This actually starts the DMA.
    WRITEREG_W( DSKLEN ,size | 0x8000 | 0x4000 );

    while (!(READREG_W(INTREQR)&0x0002));
    WRITEREG_W( DSKLEN ,0x4000);
    WRITEREG_W(INTREQ,0x0002);

    validcache=0;

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

    Forbid();

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

                if (!readtrack(track_buffer_rd,16,0)) {
                    Permit();
                    return 0;
                }

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
                            if (!writetrack(track_buffer_wr,len,0)) {
                                Permit();
                                return 0;
                            }
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

        if (!writetrack(track_buffer_wr,len,1)) {
            Permit();
            return 0;
        }
    }

    Permit();
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
                Forbid();
                if (!readtrack(track_buffer_rd,RD_TRACK_BUFFER_SIZE,0)) {
                    Permit();
                    return 0;
                }
                Permit();

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

    CIABPRB_DSKSEL = CIABPRB_DSKSEL0 << (drive&3);

    validcache=0;

    mfmtobinLUT_L=(unsigned char*)AllocMem(256,MEMF_CHIP);
    mfmtobinLUT_H=(unsigned char*)AllocMem(256,MEMF_CHIP);
    if (mfmtobinLUT_L && mfmtobinLUT_H) {
        for (i = 0; i < 256; i++) {
            mfmtobinLUT_L[i] =   ((i&0x40)>>3) | ((i&0x10)>>2) | ((i&0x04)>>1) | (i&0x01);
            mfmtobinLUT_H[i] =   mfmtobinLUT_L[i] << 4;
        }
    } else {
        alloc_error();
    }

    track_buffer_rd = (unsigned short*)AllocMem( sizeof(unsigned short) * RD_TRACK_BUFFER_SIZE, MEMF_CHIP);
    if (track_buffer_rd) {
        memset(track_buffer_rd,0,sizeof(unsigned short) * RD_TRACK_BUFFER_SIZE);
    } else {
        alloc_error();
    }

    track_buffer_wr=(unsigned short*)AllocMem( sizeof(unsigned short) * WR_TRACK_BUFFER_SIZE,MEMF_CHIP);
    if (track_buffer_wr) {
        memset(track_buffer_wr,0, sizeof(unsigned short) * WR_TRACK_BUFFER_SIZE);
    } else {
        alloc_error();
    }

    Forbid();

    WRITEREG_B(CIABPRB,~(CIABPRB_DSKMOTOR | CIABPRB_DSKSEL));
    WRITEREG_W( DMACON,0x8210);

    if (jumptotrack(255)) {
        Permit();
        hxc_printf_box("ERROR: init_fdc drive %d -> failure while seeking the track 00!",drive);
        lockup();
    }
    Delay(12);
    WRITEREG_W(INTREQ,0x0002);

    Permit();
}

/********************************************************************************
 *                          Joystick / Keyboard I/O
 *********************************************************************************/

static unsigned char Joystick(void)
{
    unsigned short code;
    unsigned char bcode;
    unsigned char ret;

    /* Get a copy of the SDR value and invert it: */
    code = READREG_W(0xDFF00C);
    bcode = READREG_B(CIAAPRA);

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
    code = READREG_B(0xBFEC01) ^ 0xFF;

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
            waitms(55);
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
        waitms(250);

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

            waitms(55);

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

/********************************************************************************
 *                              Display Output
 *********************************************************************************/

int init_display(void)
{
    unsigned short loop;

    SCREEN_XRESOL = 640;

    memset(&view,0,sizeof(struct View));
    memset(&viewPort,0,sizeof(struct ViewPort));
    memset(&rasInfo,0,sizeof(struct RasInfo));
    memset(&my_bit_map,0,sizeof(struct BitMap));
    memset(&my_rast_port,0,sizeof(struct RastPort));
    screen_buffer_backup=(unsigned char*)malloc(8*1024);

    IntuitionBase= (struct IntuitionBase *) OpenLibrary( (CONST_STRPTR)"intuition.library", 0 );
    screen=(struct Screen *)OpenScreen(&screen_cfg);

    /* Open the Graphics library: */
    GfxBaseptr = (struct GfxBase *) OpenLibrary( (CONST_STRPTR)"graphics.library", 0 );
    if ( !GfxBaseptr )  return -1;

    /* Save the current View, so we can restore it later: */
    my_old_view = GfxBaseptr->ActiView;

    /* 1. Prepare the View structure, and give it a pointer to */
    /*    the first ViewPort:                                  */
    InitView( &view );
    view.Modes |= HIRES;//LACE;

    /* 4. Prepare the BitMap: */
    InitBitMap( &my_bit_map, DEPTH, SCREEN_XRESOL, 256 );

    /* Allocate memory for the Raster: */
    for ( loop = 0; loop < DEPTH; loop++ )
    {
        my_bit_map.Planes[ loop ] = (PLANEPTR) AllocRaster( SCREEN_XRESOL, 256 );
        BltClear( my_bit_map.Planes[ loop ], RASSIZE( SCREEN_XRESOL, 256 ), 0 );
    }

    /* 5. Prepare the RasInfo structure: */
    rasInfo.BitMap = &my_bit_map; /* Pointer to the BitMap structure.  */
    rasInfo.RxOffset = 0;         /* The top left corner of the Raster */
    rasInfo.RyOffset = 0;         /* should be at the top left corner  */
    /* of the display.                   */
    rasInfo.Next = NULL;          /* Single playfield - only one       */
    /* RasInfo structure is necessary.   */

    InitVPort(&viewPort);           /*  Initialize the ViewPort.  */
    view.ViewPort = &viewPort;      /*  Link the ViewPort into the View.  */
    viewPort.RasInfo = &rasInfo;
    viewPort.DWidth = SCREEN_XRESOL;
    viewPort.DHeight = 256;

    /* Set the display mode the old-fashioned way */
    viewPort.Modes=HIRES;// | LACE;

    cm =(struct ColorMap *) GetColorMap(COLOURS);

    /* Attach the ColorMap, old 1.3-style */
    viewPort.ColorMap = cm;

    LoadRGB4(&viewPort, colortable, 4);

    /* 6. Create the display: */
    MakeVPort( &view, &viewPort );
    MrgCop( &view );
    LoadView( &view );
    WaitTOF();
    WaitTOF();

    /* 7. Prepare the RastPort, and give it a pointer to the BitMap. */
    InitRastPort( &my_rast_port );
    my_rast_port.BitMap = &my_bit_map;
    SetAPen( &my_rast_port,   1 );
    screen_buffer = my_bit_map.Planes[ 0 ];

    SCREEN_YRESOL = (get_vid_mode() > 290) ? 256 : 200;

    // Number of free line to display the file list.

    disablemousepointer();
    init_timer();

    return 0;
}

unsigned short get_vid_mode(void)
{
    unsigned short vpos = 0,vpos2 = 0;

    Forbid();
    do {
        vpos = READREG_W(VHPOSR) >> 8;
        while (vpos == (READREG_W(VHPOSR) >> 8))
            continue;

        vpos = ((READREG_W(VPOSR)&1)<<8) | (READREG_W(VHPOSR)>>8);
        if (vpos >= vpos2)
            vpos2 = vpos;
    } while (vpos >= vpos2);
    Permit();
    return vpos2;
}

void disablemousepointer(void)
{
    WRITEREG_W( DMACON ,0x20);
}

unsigned char set_color_scheme(unsigned char color)
{
    LoadRGB4(&viewPort, &colortable[(color&0x1F)*4], 4);
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

static void ithandler(void)
{
    timercnt++;

    io_floppy_timeout++;

    if ((Keyboard() & 0x80)  && !Joystick())
        keyup = 2;
}

void init_timer(void)
{
    rbfint = AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC|MEMF_CLEAR);
    rbfint->is_Node.ln_Type = NT_INTERRUPT;      /* Init interrupt node. */
    rbfint->is_Node.ln_Name = "HxCFESelectorTimerInt";
    rbfint->is_Data = 0;//(APTR)rbfdata;
    rbfint->is_Code = ithandler;

    AddIntServer(5,rbfint);
}

int process_command_line(int argc, char* argv[])
{
    return 0;
}
