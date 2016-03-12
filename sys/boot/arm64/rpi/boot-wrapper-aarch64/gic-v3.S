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
	 * If GICv3 is not available, skip initialisation. The OS will probably
	 * fail with a warning, but this should be easier to debug than a
	 * failure within the boot wrapper.
	 */
	mrs	x0, id_aa64pfr0_el1
	ubfx	x0, x0, #24, #4
	cmp	x0, #1
	b.ne	skip_gicv3

	/*
	 * Only the primary CPU setups the (re)distributors.
	 */
	mrs	x0, mpidr_el1
	ldr	x1, =MPIDR_ID_BITS
	tst	x0, x1
	b.ne	setup_cpu_if			// secondary CPU

	ldr	x1, =GIC_DIST_BASE		// GICD_CTLR
	mov	w0, #7				// EnableGrp0 | EnableGrp1ns | EnableGrp1s
	orr	w0, w0, #(3 << 4)		// ARE_S | ARE_NS
	str	w0, [x1]

	ldr	x2, =GIC_RDIST_BASE

	mvn	w5, wzr

next_rdist:
	movn	w6, #(1 << 1)			// ProcessorSleep
	ldr	w4, [x2, #0x014]		// GICR_WAKER
	and	w4, w4, w6			// Clear ProcessorSleep
	str	w4, [x2, #0x014]		// GICR_WAKER
	dsb	st
	isb

1:	ldr	w4, [x2, #0x014]		// GICR_WAKER
	ands	wzr, w4, #(1 << 2)		// Test ChildrenAsleep
	b.ne	1b

	add	x3, x2, #(1 << 16)		// SGI_base

	str	w5, [x3, #0x80]			// GICR_IGROUP0
	str	wzr, [x3, #0xD00]		// GICR_IGRPMOD0

	ldr	w4, [x2, #8]			// GICR_TYPER
	add	x3, x3, #(1 << 16)		// Next redist
	tbz	w4, #1, 2f			// if VLPIS is set,
	add	x3, x3, #(2 << 16)		// it is two page further away
2:	mov	x2, x3
	tbz	w4, #4, next_rdist

	ldr	w2, [x1, #4]			// GICD_TYPER
	and	w2, w2, #0x1f                   // ITLinesNumber
	cbz	w2, setup_cpu_if

	add	x3, x1, #0x84			// GICD_IGROUP1
	add	x4, x1, #0xD04			// GICD_IGRPMOD1

1:	str     w5, [x3], #4
	str	wzr, [x4], #4
	sub	w2, w2, #1
	cbnz	w2, 1b

setup_cpu_if:

#define ICC_SRE_EL2	S3_4_C12_C9_5
#define ICC_SRE_EL3	S3_6_C12_C12_5
#define ICC_CTLR_EL1	S3_0_C12_C12_4
#define ICC_CTLR_EL3	S3_6_C12_C12_4
#define ICC_PMR_EL1	S3_0_C4_C6_0

	// Enable SRE at EL3 and ICC_SRE_EL2 access
	mov	x0, #((1 << 3) | (1 << 0))	// Enable | SRE
	mrs	x1, ICC_SRE_EL3
	orr	x1, x1, x0
	msr	ICC_SRE_EL3, x1
	isb

	// Configure CPU interface
	msr	ICC_CTLR_EL3, xzr
	isb

skip_gicv3:
	ret
