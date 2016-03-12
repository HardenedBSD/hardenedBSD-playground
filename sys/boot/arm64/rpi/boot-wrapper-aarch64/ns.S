/*
 * ns.S - code to initialise everything required when first booting non-secure.
 *
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#define PL011_UARTIBRD	0x24
#define PL011_UARTFBRD	0x28
#define PL011_UART_LCR_H	0x2c
#define PL011_UARTCR	0x30

#define V2M_SYS_CFGDATA	0xa0
#define V2M_SYS_CFGCTRL	0xa4

	.text
	.globl ns_init_system

ns_init_system:
#ifdef UART_BASE
	/*
	 * UART initialisation (38400 8N1)
	 */
	ldr	x4, =UART_BASE
	mov	w5, #0x10
	str	w5, [x4, #PL011_UARTIBRD]
	str	wzr, [x4, #PL011_UARTFBRD]
	/* set parameters to 8N1 and enable the FIFOs */
	mov	w5, #0x70
	str	w5, [x4, #PL011_UART_LCR_H]
	/* enable the UART, TXen and RXen */
	mov	w5, #0x301
	str	w5, [x4, #PL011_UARTCR]
#endif

#ifdef SYSREGS_BASE
	/*
	 * CLCD output site MB
	 */
	ldr	x4, =SYSREGS_BASE
	ldr	w5, =(1 << 31) | (1 << 30) | (7 << 20) | (0 << 16)	// START|WRITE|MUXFPGA|SITE_MB
	str	wzr, [x4, #V2M_SYS_CFGDATA]
	str	w5, [x4, #V2M_SYS_CFGCTRL]
#endif

	ret

	.ltorg
	.org 0x80
