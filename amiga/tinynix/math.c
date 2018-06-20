/*
 * tinynix/math.c
 * 
 * Math functions copied directly from libnix.
 * They are Public Domain.
 */

asm(
"		.globl	_div;"
"		.globl	_ldiv;"
"		.globl	___modsi3;"
"		.globl	___divsi3;"
"		.text ; .align 2;"

/* D1.L = D0.L % D1.L signed */

"___modsi3:	moveml	sp@(4:W),d0/d1;"
"		jbsr	___divsi4;"
"		movel	d1,d0;"
"		rts;"

/* D0.L = D0.L / D1.L signed */

"_div:;"
"_ldiv:;"
"___divsi3:	moveml	sp@(4:W),d0/d1;"
"___divsi4:	movel	d3,sp@-;"
"		movel	d2,sp@-;"
"		moveq	#0,d2;"
"		tstl	d0;"
"		bpls	LC5;"
"		negl	d0;"
"		addql	#1,d2;"
"LC5:		movel	d2,d3;"
"		tstl	d1;"
"		bpls	LC4;"
"		negl	d1;"
"		eoriw	#1,d3;"
"LC4:		jbsr	___udivsi4;"
"LC3:		tstw	d2;"
"		beqs	LC2;"
"		negl	d0;"
"LC2:		tstw	d3;"
"		beqs	LC1;"
"		negl	d1;"
"LC1:		movel	sp@+,d2;"
"		movel	sp@+,d3;"
"		rts;"
);

asm(
"		.globl	___mulsi3;"
"		.text ; .align 2;"

/* D0 = D0 * D1 */

"___mulsi3:	moveml	sp@(4),d0/d1;"
"		movel	d3,sp@-;"
"		movel	d2,sp@-;"
"		movew	d1,d2;"
"		mulu	d0,d2;"
"		movel	d1,d3;"
"		swap	d3;"
"		mulu	d0,d3;"
"		swap	d3;"
"		clrw	d3;"
"		addl	d3,d2;"
"		swap	d0;"
"		mulu	d1,d0;"
"		swap	d0;"
"		clrw	d0;"
"		addl	d2,d0;"
"		movel	sp@+,d2;"
"		movel	sp@+,d3;"
"		rts;"
);

asm(
"		.globl	___umodsi3;"
"		.globl	___udivsi3;"
"		.globl	___udivsi4;"
"		.text ; .align 2;"

/* D1.L = D0.L % D1.L unsigned */

"___umodsi3:	moveml	sp@(4:W),d0/d1;"
"		jbsr	___udivsi4;"
"		movel	d1,d0;"
"		rts;"

/* D0.L = D0.L / D1.L unsigned */

"___udivsi3:	moveml	sp@(4:W),d0/d1;"
"___udivsi4:	movel	d3,sp@-;"
"		movel	d2,sp@-;"
"		movel	d1,d3;"
"		swap	d1;"
"		tstw	d1;"
"		bnes	LC14;"
"		movew	d0,d2;"
"		clrw	d0;"
"		swap	d0;"
"		divu	d3,d0;"
"		movel	d0,d1;"
"		swap	d0;"
"		movew	d2,d1;"
"		divu	d3,d1;"
"		movew	d1,d0;"
"		clrw	d1;"
"		swap	d1;"
"		jra	LC11;"
"LC14:		movel	d0,d1;"
"		swap	d0;"
"		clrw	d0;"
"		clrw	d1;"
"		swap	d1;"
"		moveq	#16-1,d2;"
"LC13:		addl	d0,d0;"
"		addxl	d1,d1;"
"		cmpl	d1,d3;"
"		bhis	LC12;"
"		subl	d3,d1;"
"		addqw	#1,d0;"
"LC12:		dbra	d2,LC13;"
"LC11:		movel	sp@+,d2;"
"		movel	sp@+,d3;"
"		rts;"
);
