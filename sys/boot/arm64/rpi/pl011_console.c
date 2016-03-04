/*-
 * Copyright (c) 2000 Doug Rabson
 * Copyright (c) 2015 Andrew Turner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <bootstrap.h>

/* For RPi 3 */
#define	UART_BASE	0x3f201000

static volatile uint32_t *uart = (volatile uint32_t *)UART_BASE;

static void
pl011_cons_probe(struct console *cp)
{

	cp->c_flags |= C_PRESENTIN | C_PRESENTOUT;
}

static int
pl011_cons_init(int arg)
{

	return 0;
}

void pl011_cons_putchar(int c);
void putch(int);

void
pl011_cons_putchar(int c)
{
	if (c == '\n')
		pl011_cons_putchar('\r');

	while ((uart[6] & (1 << 5)) != 0)
		;

	uart[0] = c;
}

static int
pl011_cons_getchar(void)
{

	return (0);
}

static int
pl011_cons_poll(void)
{

	return (0);
}

struct console pl011_console = {
	"pl011",
	"pl011 console",
	0,
	pl011_cons_probe,
	pl011_cons_init,
	pl011_cons_putchar,
	pl011_cons_getchar,
	pl011_cons_poll
};
