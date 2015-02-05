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
 * Copyright (c) 2014, Nathan Dautenhahn, John Criswell, Will Dietz, Theodoros
 * Kasampalis
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
 *       Filename:  pmmu.h
 *
 *    Description:  Holds the data structures and function decls for all low
 *                  level NK protected MMU operations. 
 *
 *===-----------------------------------------------------------------------===
 *  TODO:
 *      - Make physical memory size dynamically tracked --> will require to
 *        dynamically extend the page descriptor arrays -- var:
 *        numbPageDescEntries
 *===-----------------------------------------------------------------------===
 */

#ifndef __PMMU_H_
#define __PMMU_H_

#include <sys/types.h>

/* Begin and end location of the perspicuos protected region */
extern char _pspacestart[];
extern char _pspaceend[];

//
//===-- Type Definitions for MMU system -----------------------------------===//
//
typedef uintptr_t cr3_t;
typedef uintptr_t pml4e_t;
typedef uintptr_t pdpte_t;
typedef uintptr_t pde_t;
typedef uintptr_t pte_t;
typedef uintptr_t page_entry_t;

//
//===-- Define structures used in the SVA MMU interface--------------------===//
//

/*
 * Frame usage constants
 */
/* Enum representing the four page types */
enum page_type_t {
    PG_UNUSED = 0,
    PG_L1,          /*  1: Defines a page being used as an L1 PTP */
    PG_L2,          /*  2: Defines a page being used as an L2 PTP */
    PG_L3,          /*  3: Defines a page being used as an L3 PTP */
    PG_L4,          /*  4: Defines a page being used as an L4 PTP */
    PG_LEAF,        /*  5: Generic type representing a valid LEAF page */
    PG_TKDATA,      /*  6: Defines a kernel data page */
    PG_TUDATA,      /*  7: Defines a user data page */
    PG_CODE,        /*  8: Defines a code page */
    PG_SVA,         /*  9: Defines an SVA system page */
    PG_GHOST,       /* 10: Defines a secure page */
    PG_DML1,        /* 11: Defines a L1 PTP  for the direct map */
    PG_DML2,        /* 12: Defines a L2 PTP  for the direct map */
    PG_DML3,        /* 13: Defines a L3 PTP  for the direct map */
    PG_DML4,        /* 14: Defines a L4 PTP  for the direct map */
};

/*
 * Struct: page_desc_t
 *
 * Description:
 *  There is one element of this structure for each physical page of memory
 *  in the system.  It records information about the physical memory (and the
 *  data stored within it) that SVA needs to perform its MMU safety checks.
 */
typedef struct page_desc_t {
    /* Type of frame */
    enum page_type_t type;

    /*
     * If the page is a page table page, mark the virtual addres to which it is
     * mapped.
     */
    uintptr_t pgVaddr;

    /* Flag to denote whether the page is a Ghost page table page */
    unsigned ghostPTP : 1;

    /* Flag denoting whether or not this frame is a stack frame */
    unsigned stack : 1;
    
    /* Flag denoting whether or not this frame is a code frame */
    unsigned code : 1;
    
    /* State of page: value of != 0 is active and 0 is inactive */
    unsigned active : 1;

    /* Number of times a page is mapped */
    unsigned count : 12;

    /* Is this page a user page? */
    unsigned user : 1;
} page_desc_t;


/*
 * Function prototypes
 */
void    pmmu_init(pml4e_t * kpml4Mapping, unsigned long nkpml4e, uintptr_t *
            firstpaddr, uintptr_t btext, uintptr_t etext);

#endif /* __PMMU_H_ */
