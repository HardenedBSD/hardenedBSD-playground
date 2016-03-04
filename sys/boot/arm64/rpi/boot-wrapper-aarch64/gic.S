/*
 * gic.S - Secure gic initialisation for stand-alone Linux booting
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include "common.S"

	.text

	.global gic_secure_init

gic_secure_init:
	/*
	 * Check for the primary CPU to avoid a race on the distributor
	 * registers.
	 */
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	tst	x0, x1
	b.ne	1f				// secondary CPU

	ldr	x1, =GIC_DIST_BASE		// GICD_CTLR
	mov	w0, #3				// EnableGrp0 | EnableGrp1
	str	w0, [x1]

1:	ldr	x1, =GIC_DIST_BASE + 0x80	// GICD_IGROUPR
	mov	w0, #~0				// Grp1 interrupts
	str	w0, [x1]
	b.ne	2f				// Only local interrupts for secondary CPUs
	ldr     x2, =GIC_DIST_BASE + 0x04       // GICD_TYPER
	ldr     w3, [x2]
	ands    w3, w3, #0x1f                   // ITLinesNumber
	b.eq    2f
1:	str     w0, [x1, #4]!
	subs    w3, w3, #1
	b.ne    1b

2:	ldr	x1, =GIC_CPU_BASE		// GICC_CTLR
	mov	w0, #3				// EnableGrp0 | EnableGrp1
	str	w0, [x1]

	mov	w0, #1 << 7			// allow NS access to GICC_PMR
	str	w0, [x1, #4]			// GICC_PMR

	ret
