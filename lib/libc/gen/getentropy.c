/*
 * Copyright (c) 2014 Brent Cook <bcook@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Emulation of getentropy(2) as documented at:
 * http://www.openbsd.org/cgi-bin/man.cgi/OpenBSD-current/man2/getentropy.2
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <stddef.h>
#include <errno.h>

int
getentropy(void *buf, size_t len)
{
	int mib[2];
	size_t todo, done;
	unsigned char *p;
	int err;

	if (len > 256) {
		err = -1;
		errno = EIO;
		goto fail;
	}


	mib[0] = CTL_KERN;
	mib[1] = KERN_ARND;

	p = (unsigned char *)buf;
	done = 0;
	err = 0;

	while (done < len) {
			todo = len - done;
			if (sysctl(mib, 2, p, &todo, NULL, 0) == -1)
					goto fail;

			done += todo;
			p += done;
	}

fail:
	return (err);
}
