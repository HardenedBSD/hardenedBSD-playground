/*-
 * Copyright (c) 2006 Elad Efrat <elad@NetBSD.org>
 * Copyright (c) 2013-2014, by Oliver Pinter <oliver.pntr at gmail.com>
 * Copyright (c) 2014, by Shawn Webb <lattera at gmail.com>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_compat.h"
#include "opt_pax.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysent.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/elf_common.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/libkern.h>
#include <sys/jail.h>

#include <sys/mman.h>
#include <sys/libkern.h>
#include <sys/exec.h>
#include <sys/kthread.h>

#include <sys/syslimits.h>
#include <sys/param.h>

#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <machine/elf.h>

#include <sys/pax.h>

#include <security/mac_bsdextended/mac_bsdextended.h>

SYSCTL_NODE(_hardening, OID_AUTO, pax, CTLFLAG_RD, 0,
    "PaX (exploit mitigation) features.");

const char *pax_status_str[] = {
	[PAX_FEATURE_DISABLED] = "disabled",
	[PAX_FEATURE_OPTIN] = "opt-in",
	[PAX_FEATURE_OPTOUT] = "opt-out",
	[PAX_FEATURE_FORCE_ENABLED] = "force enabled",
	[PAX_FEATURE_UNKNOWN_STATUS] = "UNKNOWN -> changed to \"force enabled\""
};

struct prison *
pax_get_prison(struct proc *proc)
{
	if ((proc == NULL) || (proc->p_ucred == NULL))
		return (NULL);

	return (proc->p_ucred->cr_prison);
}

int
pax_get_flags(struct proc *proc, uint32_t *flags)
{
	*flags = 0;

	if (proc != NULL)
		*flags = proc->p_pax;
	else
		return (1);

	return (0);
}

int
pax_elf(struct image_params *imgp, uint32_t mode)
{
	struct prison *pr;
	u_int flags = 0;
	u_int status = 0;

	if ((mode & MBI_ALLPAX) != MBI_ALLPAX) {
		if (mode & MBI_ASLR_ENABLED)
			flags |= PAX_NOTE_ASLR;
		if (mode & MBI_ASLR_DISABLED)
			flags |= PAX_NOTE_NOASLR;
		if (mode & MBI_SEGVGUARD_ENABLED)
			flags |= PAX_NOTE_GUARD;
		if (mode & MBI_SEGVGUARD_DISABLED)
			flags |= PAX_NOTE_NOGUARD;
	}

	if ((flags & ~PAX_NOTE_ALL) != 0) {
		pax_log_aslr(imgp->proc, __func__, "unknown paxflags: %x\n", flags);
		pax_ulog_aslr(NULL, "unknown paxflags: %x\n", flags);

		return (1);
	}

	if (((flags & PAX_NOTE_ALL_ENABLED) & ((flags & PAX_NOTE_ALL_DISABLED) >> 1)) != 0) {
		/*
		 * indicate flags inconsistencies in dmesg and in user terminal
		 */
		pax_log_aslr(imgp->proc, __func__, "inconsistent paxflags: %x\n", flags);
		pax_ulog_aslr(NULL, "inconsistent paxflags: %x\n", flags);

		return (1);
	}

#ifdef PAX_ASLR
		pr = pax_get_prison(imgp->proc);
		if (pr != NULL)
			status = pr->pr_pax_aslr_status;
		else
			status = pax_aslr_status;

		if (status == PAX_FEATURE_DISABLED) {
			flags &= ~PAX_NOTE_ASLR;
			flags |= PAX_NOTE_NOASLR;
		}

		if (status == PAX_FEATURE_FORCE_ENABLED) {
			flags |= PAX_NOTE_ASLR;
			flags &= ~PAX_NOTE_NOASLR;
		}

		if (status == PAX_FEATURE_OPTIN) {
			if (mode & MBI_FORCE_ASLR_ENABLED) {
				flags |= PAX_NOTE_ASLR;
				flags &= ~PAX_NOTE_NOASLR;
			} else {
				flags &= ~PAX_NOTE_ASLR;
				flags |= PAX_NOTE_NOASLR;
				pax_log_aslr(proc, __func__,
		"ASLR is opt-in, and executable don't have enabled ASLR!\n");
				pax_ulog_aslr(NULL,
		"ASLR is opt-in, and executable don't have enabled ASLR!\n");
			}
		}

		if (status == PAX_FEATURE_OPTOUT) {
			if (mode & MBI_FORCE_ASLR_DISABLED) {
				flags &= ~PAX_NOTE_ASLR;
				flags |= PAX_NOTE_NOASLR;
				pax_log_aslr(proc, __func__,
		 "ASLR is opt-out, and executable explicitly disabled ASLR!\n");
				pax_ulog_aslr(NULL,
		 "ASLR is opt-out, and executable explicitly disabled ASLR!\n");
			} else {
				flags |= PAX_NOTE_ASLR;
				flags &= ~PAX_NOTE_NOASLR;
			}
		}
#endif

#ifdef PAX_SEGVGUARD
	pax_segvguard_parse_flags(imgp, mode);
#endif

	if (imgp != NULL) {
		imgp->pax_flags = flags;
		if (imgp->proc != NULL) {
			PROC_LOCK(imgp->proc);
			imgp->proc->p_pax = flags;
			PROC_UNLOCK(imgp->proc);
		}
	}

	return (0);
}


/*
 * print out PaX settings on boot time, and validate some of them
 */
static void
pax_sysinit(void)
{

	printf("PAX: initialize and check PaX and HardeneBSD features.\n");
}
SYSINIT(pax, SI_SUB_PAX, SI_ORDER_FIRST, pax_sysinit, NULL);

void
pax_init_prison(struct prison *pr)
{

	if (pr == NULL)
		return;

	if (pr->pr_pax_set)
		return;

	mtx_lock(&(pr->pr_mtx));

	if (pax_aslr_debug)
		uprintf("[PaX ASLR] %s: Setting prison %s ASLR variables\n",
		    __func__, pr->pr_name);

#ifdef PAX_ASLR
	pr->pr_pax_aslr_status = pax_aslr_status;
	pr->pr_pax_aslr_debug = pax_aslr_debug;
	pr->pr_pax_aslr_mmap_len = pax_aslr_mmap_len;
	pr->pr_pax_aslr_stack_len = pax_aslr_stack_len;
	pr->pr_pax_aslr_exec_len = pax_aslr_exec_len;

#ifdef COMPAT_FREEBSD32
	pr->pr_pax_aslr_compat_status = pax_aslr_compat_status;
	pr->pr_pax_aslr_compat_mmap_len = pax_aslr_compat_mmap_len;
	pr->pr_pax_aslr_compat_stack_len = pax_aslr_compat_stack_len;
	pr->pr_pax_aslr_compat_exec_len = pax_aslr_compat_exec_len;
#endif /* COMPAT_FREEBSD32 */
#endif /* PAX_ASLR */

#ifdef PAX_SEGVGUARD
	pr->pr_pax_segvguard_status = pax_segvguard_status;
	pr->pr_pax_segvguard_debug = pax_segvguard_debug;
	pr->pr_pax_segvguard_expiry = pax_segvguard_expiry;
	pr->pr_pax_segvguard_suspension = pax_segvguard_suspension;
	pr->pr_pax_segvguard_maxcrashes = pax_segvguard_maxcrashes;
#endif

#ifdef PAX_HARDENING
	pr->pr_pax_map32_enabled = pax_map32_enabled_global;
#endif

	pr->pr_pax_set = 1;

	mtx_unlock(&(pr->pr_mtx));
}
