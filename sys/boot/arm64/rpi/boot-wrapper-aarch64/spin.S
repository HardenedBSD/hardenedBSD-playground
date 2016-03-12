/*
 * spin.S - spin-table boot protocol implementation
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include "common.S"

	.text

	.globl start_no_el3
	.globl start_el3

start_el3:
	/*
	 * Prepare the switch to the EL2_SP1 mode from EL3
	 */
	ldr	x0, =SCTLR_EL2_RESET
	msr	sctlr_el2, x0
	ldr	x0, =start_no_el3		// Return after mode switch
	mov	x1, #SPSR_KERNEL
	drop_el	x1, x0

start_no_el3:
	/*
	 * Kernel parameters
	 */
	mov	x0, xzr
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr

	mrs	x4, mpidr_el1
	ldr	x5, =MPIDR_ID_BITS
	tst	x4, x5
	b.eq	2f

	/*
	 * Secondary CPUs
	 */
1:	wfe
#if 0 /* No SMP support */
	ldr	x4, mbox
	cbz	x4, 1b
	br	x4				// branch to the given address
#else
	b	1b
#endif

2:
	/*
	 * Primary CPU
	 */
	bl	ns_init_system
	ldr	x0, =DTB_ADDR			// device tree blob
	/* Set the stack, assume there is space before the dtb */
	mov	sp, x0
	b	arm64_main

	.ltorg

	.org	0x80
