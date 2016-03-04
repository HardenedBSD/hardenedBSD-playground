/*
 * cache.S - simple cache clean+invalidate code for stand-alone Linux booting
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */
	.text

	.globl flush_caches

flush_caches:
	mrs	x0, clidr_el1

	/* find what out the max cache level to flush */
	lsr	x1, x0, #24
	and	x1, x1, #(0x7)
	cbz	x1, dcaches_done

	mov	x2, #0			/* level 1 (represented 1-off) */

1:	cmp	x2, x1			/* gone over all levels */
	b.eq	dcaches_done

	/* find out if we have a cache at this level */
	add	x3, x2, x2, lsl 1	/* amount to shift for CtypeN */
	lsr	x4, x0, x3
	and	x4, x4, #0x7

	cmp	x4, #1
	b.eq	5f			/* no dcache at this level */

	lsl	x3, x2, #1
	msr	csselr_el1, x3
	isb
	mrs	x3, ccsidr_el1
	and	x4, x3, #0x7
	add	x4, x4, #4		/* log2 line size, corrected for offset */
	ubfx	x6, x3, #3, #10		/* max way index */
	clz	w5, w6			/* 32 - log2 ways */
	ubfx	x7, x3, #13, #15	/* sets */

	/* loop over ways */
2:	mov	x8, x7			/* temporary (sets) */

	/* loop over sets */
	/* build the set/way command */
3:	lsl	x9, x2, #1		/* cache level (-1) */
	lsl	x10, x6, x5		/* way << shift */
	orr	x9, x9, x10
	lsl	x10, x8, x4		/* set << line size */
	orr	x9, x9, x10

	dc	cisw, x9
	dsb	sy

	cbz	x8, 4f
	sub	x8, x8, #1
	b	3b

4:	/* completed all sets for this way */
	cbz	x6, 5f
	sub	x6, x6, #1
	b	2b

5:	/* finished this level, try the next */
	dsb	sy
	add	x2, x2, #1
	b	1b

dcaches_done:

	dsb	sy
	ic	iallu
	dsb	sy
	isb
	ret

	.ltorg
	.org 0x100
