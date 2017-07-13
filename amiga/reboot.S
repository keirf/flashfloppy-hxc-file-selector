
#define _LVOSupervisor   -30
#define LIB_VERSION      20         /* Offset of version in Lib struct */
#define MAGIC_ROMEND     0x01000000 /* End of Kickstart ROM */
#define MAGIC_SIZEOFFSET -0x14      /* Offset of Kickstart size */
#define TEMP_ColdReboot  -726       /* Offset of the V36 ColdReboot func */

        .text
        .globl __reboot
__reboot:
        move.l  4,a6
        cmp.w   #36,LIB_VERSION(a6)
        jlt     old_exec

        /* Let Exec do it... */
        jmp     TEMP_ColdReboot(a6)

old_exec:
        lea.l   GoAway(pc),a5
        jsr     _LVOSupervisor(a6)

        .align 4                        /* IMPORTANT! Longword align! */
GoAway:
        lea.l   MAGIC_ROMEND,a0         /* (end of ROM) */
        sub.l   MAGIC_SIZEOFFSET(a0),a0 /* (end of ROM)-(ROM size)=PC */
        move.l  4(a0),a0                /* Get Initial Program Counter */
        subq.l  #2,a0                   /* now points to second RESET */
        /* NOTE: the RESET and JMP instructions must share a longword! */
        reset                           /* first RESET instruction */
        jmp     (a0)                    /* CPU Prefetch executes this */
