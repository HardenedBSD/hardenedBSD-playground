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
 *       Filename:  cpufunc.h
 *
 *    Description:  These are the real CPU state modifying functions
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CPUFUNC_H_
#define __CPUFUNC_H_

#include <sys/types.h>

/*
 *===-----------------------------------------------------------------------===
 * Flags that are protected state of the nested kernel
 *===-----------------------------------------------------------------------===
 */

/* CR0 Flags */
#define     CR0_WP          0x00010000      /* Write protect enable */

/* CR4 Flags */
#define     CR4_SMEP        0x00100000      /* SMEP enable */

/* EFER Flags */
#define     EFER_NXE        0x000000800     /* NXE enable */

/* MSRs */
#define     MSR_REG_EFER    0xC0000080      /* MSR for EFER register */

/*
 *===-----------------------------------------------------------------------===
 * Low level register local read/write functions
 *===-----------------------------------------------------------------------===
 */

/* 
 * Load cr0 with the given value
 */
static inline void 
_load_cr0(register_t val) 
{
    __asm __volatile("movq %0,%%cr0" : : "r" (val));
}

/*
 * Function: _load_cr3
 *
 * Description: 
 *  Load the cr3 with the given value passed in.
 */
static inline void 
_load_cr3(register_t data)
{ 
    __asm __volatile("movq %0,%%cr3" : : "r" (data) : "memory"); 
}

/* 
 * Load cr4 with the given value
 */
static inline void 
_load_cr4(register_t val) 
{
    __asm __volatile("movq %0,%%cr4" : : "r" (val));
}


/* 
 * Load an MSR with the given value
 */
static inline void 
_wrmsr(register_t msr, register_t newval)
{
	uint32_t low, high;

	low = newval;
	high = newval >> 32;
	__asm __volatile("wrmsr" : : "a" (low), "d" (high), "c" (msr));
}

static inline register_t 
_rdmsr(register_t msr)
{
    uint32_t low, high;

    __asm __volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return (low | ((register_t)high << 32));
}

static inline register_t 
_rcr0(void) 
{
    register_t  data;
    __asm __volatile("movq %%cr0,%0" : "=r" (data));
    return (data);
}

static inline register_t 
_rcr3(void) 
{
    register_t  data;
    __asm __volatile("movq %%cr3,%0" : "=r" (data));
    return (data);
}

static inline register_t 
_rcr4(void) 
{
    register_t  data;
    __asm __volatile("movq %%cr4,%0" : "=r" (data));
    return (data);
}

static inline register_t 
_efer(void) 
{
    return _rdmsr(MSR_REG_EFER);
}

#endif /* __CPUFUNC_H_ */

