/*===-- LICENSE ------------------------------------------------------------===
 * 
 * University of Illinois/NCSA Open Source License 
 *
 * Copyright (C) 2014, The Board of Trustees of the University of Illinois.
 * All rights reserved. 
 *
 * Developed by: 
 *
 *    Research Group of Professor Vikram Adve in the Department of Computer
 *    Science The University of Illinois at Urbana-Champaign
 *    http://web.engr.illinois.edu/~vadve/Home.html
 *
 * Copyright (c) 2014, Nathan Dautenhahn, Theodoros Kasampalis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the Software), to deal
 * with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions: 
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimers. 
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimers in the documentation
 * and/or other materials provided with the distribution.  Neither the names of
 * Sam King or the University of Illinois, nor the names of its contributors
 * may be used to endorse or promote products derived from this Software
 * without specific prior written permission. 
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE. 
 *
 *===-----------------------------------------------------------------------===
 *
 *       Filename:  nk_cpufunc.c
 *
 *    Description:  This function implements the protected interface to
 *                  protected CPU functionality.
 *
 *===-----------------------------------------------------------------------===
 */

#include "nk/nk_cpufunc.h"
#include "cpufunc.h"
#include "common.h"

#include <sys/types.h>
#include <sys/libkern.h>

#define	CPUID_STDEXT_SMEP	0x00000080
    
extern u_int cpu_stdext_feature;

/*
 * Function: nk_load_cr0
 *
 * Description:
 *  Nested Kernel Call to load a value in cr0. We need to make sure write
 *  protection is enabled. 
 */
void nk_load_cr0 (register_t val)
{
    val |= CR0_WP;
    _load_cr0(val);
    if (!(val & CR0_WP))
        panic("Nested Kernel: attempt to clear the CR0.WP bit: %p.", (void *) val);
}

/*
 * Function: nk_load_cr4
 *
 * Description:
 *  Nested Kernel Call to load a value in cr4. We need to make sure that the
 *  SMEP bit is enabled. 
 */
void 
nk_load_cr4(register_t val)
{
    /* 
     * XXX Test for SMEP support, if not the Nested Kernel Cannot guarantee full
     * protections.
     */
    if (cpu_stdext_feature & CPUID_STDEXT_SMEP)
    {
        val |= CR4_SMEP;
    }
    else 
        printf("Nested Kernel Property Violation: SMEP Feature not supported on this chip!");

    _load_cr4(val);

    /* 
     * XXX: If there is no SMEP then the nested kernel isolation must be
     * supported with another mechanisn, such as with marking all usermode
     * pages as NX.
     */
    if (cpu_stdext_feature & CPUID_STDEXT_SMEP) { if (!(val & CR4_SMEP))
        panic("Nested Kernel: attempt to clear the CR4.SMEP bit: %p.", (void *) val); }
}

/*
 * Function: nk_load_msr
 *
 * Description:
 *  Nested Kernel Call to load a value in an MSR. If the MSR is EFER, we need
 *  to make sure that the NXE bit is enabled. 
 *
 * Assumption: 
 *  The system wide enforcement of the NX bit is initialized by the pmmu_init
 *  function.
 */
void nk_load_msr(register_t msr, register_t val) 
{
    _wrmsr(msr, val);
    if ((msr == MSR_REG_EFER) && !(val & EFER_NXE))
        panic("Nested Kernel: attempt to clear the EFER.NXE bit: %p.", (void *) val);
}

/*
 * Function: nk_wrmsr
 *
 * Description:
 *  Nested Kernel call to load a value in an MSR. The given value should be
 *  given in edx:eax and the MSR should be given in ecx. If the MSR is
 *  EFER, we need to make sure that the NXE bit is enabled. 
 */

#if 0
void nk_wrmsr(void) 
{
    register_t val;
    register_t msr;
    __asm__ __volatile__ (
        "wrmsr\n"
        : "=c" (msr), "=a" (val)
        :
        : "rax", "rcx", "rdx"
    );
    if ((msr == MSR_REG_EFER) && !(val & EFER_NXE))
        panic("Nested Kernel: attempt to clear the EFER.NXE bit: %p.", (void *) val);
}
#endif
