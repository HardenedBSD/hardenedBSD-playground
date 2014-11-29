/*-
 * Copyright (c) 2006 Elad Efrat <elad@NetBSD.org>
 * Copyright (c) 2013-2014, by Oliver Pinter <oliver.pinter@hardenedbsd.org>
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

#include "opt_pax.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/sysent.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/elf_common.h>
#include <sys/mount.h>
#include <sys/pax.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/queue.h>
#include <sys/libkern.h>
#include <sys/jail.h>

#include <sys/mman.h>
#include <sys/libkern.h>
#include <sys/exec.h>
#include <sys/kthread.h>

#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <machine/elf.h>

FEATURE(pax_pageexec, "PAX PAGEEXEC hardening");
#ifdef PAX_MPROTECT
FEATURE(pax_mprotect, "PAX MPROTECT hardening");
#endif

static int pax_pageexec_status = PAX_FEATURE_OPTOUT;
#ifdef PAX_MPROTECT
static int pax_mprotect_status = PAX_FEATURE_OPTOUT;
#endif

TUNABLE_INT("hardening.pax.pageexec.status", &pax_pageexec_status);
#ifdef PAX_MPROTECT
TUNABLE_INT("hardening.pax.mprotect.status", &pax_mprotect_status);
#endif

#ifdef PAX_SYSCTLS
SYSCTL_DECL(_hardening_pax);

/*
 * sysctls
 */
static int sysctl_pax_pageexec_status(SYSCTL_HANDLER_ARGS);
#ifdef PAX_MPROTECT
static int sysctl_pax_mprotect_status(SYSCTL_HANDLER_ARGS);
#endif

SYSCTL_NODE(_hardening_pax, OID_AUTO, pageexec, CTLFLAG_RD, 0,
    "Remove WX pages from user-space.");

SYSCTL_PROC(_hardening_pax_pageexec, OID_AUTO, status,
    CTLTYPE_INT|CTLFLAG_RWTUN|CTLFLAG_PRISON|CTLFLAG_SECURE,
    NULL, 0, sysctl_pax_pageexec_status, "I",
    "Restrictions status. "
    "0 - disabled, "
    "1 - opt-in,  "
    "2 - opt-out, "
    "3 - force enabled");

static int
sysctl_pax_pageexec_status(SYSCTL_HANDLER_ARGS)
{
	struct prison *pr;
	int err, val;

	pr = pax_get_prison(req->td->td_proc);

	val = pr->pr_hardening.hr_pax_pageexec_status;
	err = sysctl_handle_int(oidp, &val, sizeof(int), req);
	if (err || (req->newptr == NULL))
		return (err);

	switch (val) {
	case PAX_FEATURE_DISABLED:
#ifdef PAX_MPROTECT
		printf("PAX MPROTECT depend on PAGEEXEC!\n");
		if (pr == &prison0)
			pax_mprotect_status = val;

		pr->pr_hardening.hr_pax_mprotect_status = val;
#endif
	case PAX_FEATURE_OPTIN:
	case PAX_FEATURE_OPTOUT:
	case PAX_FEATURE_FORCE_ENABLED:
		if (pr == &prison0)
			pax_pageexec_status = val;

		pr->pr_hardening.hr_pax_pageexec_status = val;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

#ifdef PAX_MPROTECT
SYSCTL_NODE(_hardening_pax, OID_AUTO, mprotect, CTLFLAG_RD, 0,
    "MPROTECT hardening - enforce W^X.");

SYSCTL_PROC(_hardening_pax_mprotect, OID_AUTO, status,
    CTLTYPE_INT|CTLFLAG_RWTUN|CTLFLAG_PRISON|CTLFLAG_SECURE,
    NULL, 0, sysctl_pax_mprotect_status, "I",
    "Restrictions status. "
    "0 - disabled, "
    "1 - opt-in,  "
    "2 - opt-out, "
    "3 - force enabled");

static int
sysctl_pax_mprotect_status(SYSCTL_HANDLER_ARGS)
{
	struct prison *pr;
	int err, val;

	pr = pax_get_prison(req->td->td_proc);

	val = pr->pr_hardening.hr_pax_mprotect_status;
	err = sysctl_handle_int(oidp, &val, sizeof(int), req);
	if (err || (req->newptr == NULL))
		return (err);

	switch (val) {
	case PAX_FEATURE_DISABLED:
	case PAX_FEATURE_OPTIN:
	case PAX_FEATURE_OPTOUT:
	case PAX_FEATURE_FORCE_ENABLED:
		if (pr == &prison0)
			pax_mprotect_status = val;

		pr->pr_hardening.hr_pax_mprotect_status = val;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}
#endif /* PAX_MPROTECT */
#endif /* PAX_SYSCTLS */


/*
 * PAX PAGEEXEC functions
 */

static void
pax_pageexec_sysinit(void)
{

	switch (pax_pageexec_status) {
	case PAX_FEATURE_DISABLED:
	case PAX_FEATURE_OPTIN:
	case PAX_FEATURE_OPTOUT:
	case PAX_FEATURE_FORCE_ENABLED:
		break;
	default:
		printf("[PAX PAGEEXEC] WARNING, invalid PAX settings in loader.conf!"
		    " (hardening.pax.pageexec.status = %d)\n", pax_pageexec_status);
		pax_pageexec_status = PAX_FEATURE_FORCE_ENABLED;
		break;
	}
	printf("[PAX PAGEEXEC] status: %s\n", pax_status_str[pax_pageexec_status]);
}
SYSINIT(pax_pageexec, SI_SUB_PAX, SI_ORDER_SECOND, pax_pageexec_sysinit, NULL);

int
pax_pageexec_active(struct proc *p)
{
	u_int flags;

	pax_get_flags(p, &flags);

	CTR3(KTR_PAX, "%s: pid = %d p_pax = %x",
	    __func__, p->p_pid, flags);

	if ((flags & PAX_NOTE_PAGEEXEC) == PAX_NOTE_PAGEEXEC)
		return (true);

	if ((flags & PAX_NOTE_NOPAGEEXEC) == PAX_NOTE_NOPAGEEXEC)
		return (false);

	return (true);
}

void
pax_pageexec_init_prison(struct prison *pr)
{
	struct prison *pr_p;

	CTR2(KTR_PAX, "%s: Setting prison %s PaX variables\n",
	    __func__, pr->pr_name);

	if (pr == &prison0) {
		/* prison0 has no parent, use globals */
		pr->pr_hardening.hr_pax_pageexec_status = pax_pageexec_status;
	} else {
		KASSERT(pr->pr_parent != NULL,
		   ("%s: pr->pr_parent == NULL", __func__));
		pr_p = pr->pr_parent;

		pr->pr_hardening.hr_pax_pageexec_status =
		    pr_p->pr_hardening.hr_pax_pageexec_status;
	}
}

u_int
pax_pageexec_setup_flags(struct image_params *imgp, u_int mode)
{
	struct prison *pr;
	u_int flags, status;

	flags = 0;
	status = 0;

	pr = pax_get_prison(imgp->proc);
	status = pr->pr_hardening.hr_pax_pageexec_status;

	if (status == PAX_FEATURE_DISABLED) {
		flags &= ~PAX_NOTE_PAGEEXEC;
		flags |= PAX_NOTE_NOPAGEEXEC;

		return (flags);
	}

	if (status == PAX_FEATURE_FORCE_ENABLED) {
		flags |= PAX_NOTE_PAGEEXEC;
		flags &= ~PAX_NOTE_NOPAGEEXEC;

		return (flags);
	}

	if (status == PAX_FEATURE_OPTIN) {
		if (mode & PAX_NOTE_PAGEEXEC) {
			flags |= PAX_NOTE_PAGEEXEC;
			flags &= ~PAX_NOTE_NOPAGEEXEC;
		} else {
			flags &= ~PAX_NOTE_PAGEEXEC;
			flags |= PAX_NOTE_NOPAGEEXEC;
		}

		return (flags);
	}

	if (status == PAX_FEATURE_OPTOUT) {
		if (mode & PAX_NOTE_NOPAGEEXEC) {
			flags &= ~PAX_NOTE_PAGEEXEC;
			flags |= PAX_NOTE_NOPAGEEXEC;
			pax_log_pageexec(imgp->proc, "PAGEEXEC is opt-out, and "
			    "executable explicitly disabled PAGEEXEC!\n");
			pax_ulog_pageexec("PAGEEXEC is opt-out, and executable "
			    "explicitly disabled PAGEEXEC!\n");
		} else {
			flags |= PAX_NOTE_PAGEEXEC;
			flags &= ~PAX_NOTE_NOPAGEEXEC;
		}

		return (flags);
	}

	/*
	 * unknown status, force PAGEEXEC
	 */
	flags |= PAX_NOTE_PAGEEXEC;
	flags &= ~PAX_NOTE_NOPAGEEXEC;

	return (flags);
}


/*
 * PaX MPROTECT functions
 */
#ifdef PAX_MPROTECT
static void
pax_mprotect_sysinit(void)
{

	switch (pax_mprotect_status) {
	case PAX_FEATURE_DISABLED:
	case PAX_FEATURE_OPTIN:
	case PAX_FEATURE_OPTOUT:
	case PAX_FEATURE_FORCE_ENABLED:
		break;
	default:
		printf("[PAX MPROTECT] WARNING, invalid PAX settings in loader.conf!"
		    " (hardening.pax.mprotect.status = %d)\n", pax_mprotect_status);
		pax_mprotect_status = PAX_FEATURE_FORCE_ENABLED;
		break;
	}
	printf("[PAX MPROTECT] status: %s\n", pax_status_str[pax_mprotect_status]);
}
SYSINIT(pax_mprotect, SI_SUB_PAX, SI_ORDER_SECOND, pax_mprotect_sysinit, NULL);

int
pax_mprotect_active(struct proc *p)
{
	u_int flags;

	pax_get_flags(p, &flags);

	CTR3(KTR_PAX, "%s: pid = %d p_pax = %x",
	    __func__, p->p_pid, flags);

	if ((flags & PAX_NOTE_MPROTECT) == PAX_NOTE_MPROTECT)
		return (true);

	if ((flags & PAX_NOTE_NOMPROTECT) == PAX_NOTE_NOMPROTECT)
		return (false);

	return (true);
}

void
pax_mprotect_init_prison(struct prison *pr)
{
	struct prison *pr_p;

	CTR2(KTR_PAX, "%s: Setting prison %s PaX variables\n",
	    __func__, pr->pr_name);

	if (pr == &prison0) {
		/* prison0 has no parent, use globals */
		pr->pr_hardening.hr_pax_mprotect_status = pax_mprotect_status;
	} else {
		KASSERT(pr->pr_parent != NULL,
		   ("%s: pr->pr_parent == NULL", __func__));
		pr_p = pr->pr_parent;

		pr->pr_hardening.hr_pax_mprotect_status =
		    pr_p->pr_hardening.hr_pax_mprotect_status;
	}
}

u_int
pax_mprotect_setup_flags(struct image_params *imgp, u_int mode)
{
	struct prison *pr;
	u_int flags, status;

	flags = 0;
	status = 0;

	pr = pax_get_prison(imgp->proc);
	status = pr->pr_hardening.hr_pax_mprotect_status;

	if (status == PAX_FEATURE_DISABLED) {
		flags &= ~PAX_NOTE_MPROTECT;
		flags |= PAX_NOTE_NOMPROTECT;

		return (flags);
	}

	if (status == PAX_FEATURE_FORCE_ENABLED) {
		flags |= PAX_NOTE_MPROTECT;
		flags &= ~PAX_NOTE_NOMPROTECT;

		return (flags);
	}

	if (status == PAX_FEATURE_OPTIN) {
		if (mode & PAX_NOTE_MPROTECT) {
			flags |= PAX_NOTE_MPROTECT;
			flags &= ~PAX_NOTE_NOMPROTECT;
		} else {
			flags &= ~PAX_NOTE_MPROTECT;
			flags |= PAX_NOTE_NOMPROTECT;
		}

		return (flags);
	}

	if (status == PAX_FEATURE_OPTOUT) {
		if (mode & PAX_NOTE_NOMPROTECT) {
			flags &= ~PAX_NOTE_MPROTECT;
			flags |= PAX_NOTE_NOMPROTECT;
			pax_log_mprotect(imgp->proc, PAX_LOG_DEFAULT,
"MPROTECT is opt-out, and executable explicitly disabled MPROTECT!");
			pax_ulog_mprotect(
"MPROTECT is opt-out, and executable explicitly disabled MPROTECT!\n");
		} else {
			flags |= PAX_NOTE_MPROTECT;
			flags &= ~PAX_NOTE_NOMPROTECT;
		}

		return (flags);
	}

	/*
	 * unknown status, force MPROTECT
	 */
	flags |= PAX_NOTE_MPROTECT;
	flags &= ~PAX_NOTE_NOMPROTECT;

	return (flags);
}
#endif /* PAX_MPROTECT */
