#ifndef __LINUX_CAPABILITY_H_
#define __LINUX_CAPABILITY_H_
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/priv.h>

#define file_ns_capable(a, b, c) (1)

enum __lkpi_capabilities {
	CAP_SYS_ADMIN
};

static inline bool
capable(enum __lkpi_capabilities cap)
{

	switch (cap) {
	case CAP_SYS_ADMIN:
		return (priv_check(curthread, PRIV_DRIVER) == 0);
	default:
		panic("%s: unhandled capability: %0x", __func__, cap);
		return (false);
	}
}

#endif
