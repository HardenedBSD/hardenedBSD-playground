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
 * Copyright (c) 2014, Nathan Dautenhahn
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
 *       Filename:  nk_cpufunc.h
 *
 *    Description:  This file contains protected x86 CPU functions that are
 *                  required to maintain specific properties to maintain the
 *                  nested kernel isolation.
 *
 *        Version:  1.0
 *        Created:  08/22/14 16:46:18
 *       Revision:  none
 *       Compiler:  llvm
 *
 *         Author:  Nathan Dautenhahn (nathandautenhahn.com), dautenh1@illinois.edu
 *        Company:  University of Illinois at Urbana-Champaign
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __NK_CPUFUNC_H_
#define __NK_CPUFUNC_H_

#include <sys/types.h>

/*
 *===-----------------------------------------------------------------------===
 * External NK interface to protected CPU operations
 *===-----------------------------------------------------------------------===
 */
void nk_load_cr0(register_t val);
void nk_load_cr4(register_t val);
void nk_load_msr(register_t msr, register_t val);
void nk_wrmsr(void);

#endif /* __NK_CPUFUNC_H_ */

