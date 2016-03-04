/*
 * mmu.S - EL3 MMU identity map code to enable the use of exclusives.
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#define ATTR_UPPER_XN		(1 << 54)		/* Non-Executable */
#define ATTR_UPPER_PXN		(1 << 53)		/* Non-Executable */

#define MEM_ATTR_UPPER		0
#define DEV_ATTR_UPPER		(ATTR_UPPER_XN | ATTR_UPPER_PXN)

#define ATTR_LOWER_AF		(1 << 10)		/* Don't trap accesses */
#define ATTR_LOWER_SH_IS	(3 << 8)		/* Inner shareable */
#define ATTR_LOWER_SH_NS	(0 << 8)		/* Inner shareable */

#define ATTR_LOWER_AP_RW_ANY	(1 << 6)		/* Writeable at any privilege level */

#define ATTR_LOWER_NS		(1 << 5)		/* non-secure PA */
#define ATTR_LOWER_ATTRINDEX(n)	((n) << 2)		/* MAIR_EL3 attrN */

#define MEM_ATTR_LOWER		(ATTR_LOWER_AF | ATTR_LOWER_SH_IS | \
				 ATTR_LOWER_NS | ATTR_LOWER_ATTRINDEX(0))

#define DEV_ATTR_LOWER		(ATTR_LOWER_AF | ATTR_LOWER_SH_NS | \
				 ATTR_LOWER_NS | ATTR_LOWER_ATTRINDEX(1))

#define BLOCK_VALID		(1 << 0)		/* Valid block entry */

/*
 * the top 10 bits of PA [39:30]
 */
#define BLOCK_1GB_PA(_pa)	((_pa) & (0x3ff << 30))

#define BLOCK_MEM_1GB(_pa)	(MEM_ATTR_UPPER | BLOCK_1GB_PA(_pa) | \
				 MEM_ATTR_LOWER | BLOCK_VALID)

#define BLOCK_DEV_1GB(_pa)	(DEV_ATTR_UPPER | BLOCK_1GB_PA(_pa) | \
				 DEV_ATTR_LOWER | BLOCK_VALID)

	.data

#define BLOCK_INVALID	(0 << 0)

	/*
	 * 1st level page table.
	 * 512 entries, each covering 1GB.
	 */
	.align 12
pgtable_l1:
	.quad BLOCK_DEV_1GB(0x00000000)
	.quad BLOCK_INVALID
	.quad BLOCK_MEM_1GB(0x80000000)
	.quad BLOCK_MEM_1GB(0xC0000000)
	.rept 30
	.quad BLOCK_INVALID
	.endr
	.quad BLOCK_MEM_1GB(0x880000000)
	.quad BLOCK_MEM_1GB(0x8C0000000)
	.rept (512-36)
	.quad BLOCK_INVALID
	.endr

/*
 * attr0: Normal memory, outer non-cacheable, inner write-through non-transient
 * attrN: device-nGnRnE
 */
#define MAIR_ATTR	0x48

#define	TCR_RES1	((1 << 31) | (1 << 23))
#define	TCR_PS		(2 << 16)	/* 40 bits */
#define TCR_TG0		(0 << 14)	/* 4KB */
#define TCR_SH0		(3 << 12)	/* inner shareable */
#define TCR_ORGN0	(0 << 10)	/* normal outer non-cacheable */
#define TCR_IRGN0	(2 << 8)	/* normal inner write-through */
#define TCR_T0SZ	(25 << 0)	/* 2^39 bits (2^(64-25)) */

#define TCR_VAL		(TCR_RES1 | TCR_PS | TCR_TG0 | TCR_SH0 | TCR_ORGN0 | TCR_IRGN0 | TCR_T0SZ)

#define SCTLR_RES1	((3 << 28) | (3 << 22) | (1 << 18) | (1 << 16) | (1 << 11) | (3 << 4))
#define SCTLR_EE	(0 << 25)	/* little endian */
#define SCTLR_WXN	(0 << 19)	/* regions with write permission not forced to XN */
#define SCTLR_I		(0 << 12)	/* Disable I cache */
#define SCTLR_SA	(0 << 3)	/* No stack alignment checking */
#define SCTLR_C		(0 << 2)	/* Disable caches */
#define SCTLR_A		(0 << 1)	/* No alignment checking */
#define SCTLR_M		(1 << 0)	/* enable MMU */

#define SCTLR_VAL	(SCTLR_RES1 | SCTLR_EE | SCTLR_WXN | SCTLR_I | \
			 SCTLR_SA | SCTLR_C | SCTLR_A | SCTLR_M)

	.text

	.globl switch_to_idmap
	.globl switch_to_physmap

switch_to_idmap:

	mov	x28, x30

	/*
	 * We assume that the d-caches are invalid at power-on, and hence do
	 * not need to be invalidated. However the icache(s) and TLBs may still
	 * be filled with garbage.
	 */
	ic	iallu
	tlbi	alle3
	dsb	sy
	isb

	adr	x0, pgtable_l1
	msr	ttbr0_el3, x0

	ldr	x0, =MAIR_ATTR
	msr	mair_el3, x0

	ldr	x0, =TCR_VAL
	msr	tcr_el3, x0

	isb

	ldr	x0, =SCTLR_VAL
	msr	sctlr_el3, x0

	isb

	/* Identity map now active, branch back to phys/virt address */
	ret	x28

switch_to_physmap:
	mov	x28, x30

	mrs	x0, sctlr_el3
	mov	x1, #(SCTLR_M | SCTLR_C)
	bic	x0, x0, x1
	msr	sctlr_el3, x0

	isb

	bl	flush_caches

	ret	x28

