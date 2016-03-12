/*
 * boot.S - simple register setup code for stand-alone Linux booting
 *
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include "common.S"

	.text

	.globl	_start
_start:
	/*
	 * EL3 initialisation
	 */
	mrs	x0, CurrentEL
	cmp	x0, #CURRENTEL_EL3
	b.ne	start_no_el3			// skip EL3 initialisation

	mov	x0, #0x30			// RES1
	orr	x0, x0, #(1 << 0)		// Non-secure EL1
	orr	x0, x0, #(1 << 8)		// HVC enable
	orr	x0, x0, #(1 << 10)		// 64-bit EL2
	msr	scr_el3, x0

	msr	cptr_el3, xzr			// Disable copro. traps to EL3

	ldr	x0, =CNTFRQ
	msr	cntfrq_el0, x0

#if 0
	bl	gic_secure_init
#endif

	b	start_el3

	.ltorg

	.org	0x100
