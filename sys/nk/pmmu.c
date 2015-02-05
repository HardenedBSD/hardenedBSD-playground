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
 *       Filename:  pmmu.c
 *
 *    Description:  File that holds the low level implmentation of writes to
 *                  the physical mmu.
 *
 *===-----------------------------------------------------------------------===
 */

#include "common.h"
#include "debug.h"
#include "cpufunc.h"
#include "stack.h"
#include "pmmu.h"
#include "wr_prot.h"

#include <sys/libkern.h>
#include <sys/systm.h>

/* These are to obtain the macros and defs for the pmap init system */
//#include <machine/pmap.h>
//#include <machine/param.h>

/* Define whether or not the mmu_init code assumes virtual addresses */
#define USE_VIRT            0

//----------------------------------------------------------------------------//
//===-- pMMU Forward Declarattions ----------------------------------------===//
//----------------------------------------------------------------------------//
void declare_ptp_and_walk_pt_entries(page_entry_t *pageEntry, unsigned long
        numPgEntries, enum page_type_t pageLevel );

//----------------------------------------------------------------------------//
//===-- Local Data Definitions --------------------------------------------===//
//----------------------------------------------------------------------------//
/* Size of the physical memory and page size in bytes */
#define MEMSIZE             0x0000000800000000u
#define PAGESIZE            4096
#define NUMPGDESCENTRIES    MEMSIZE / PAGESIZE

#if REVIEW_OBSOLETE
/* Start and end addresses of the secure memory */
#define SECMEMSTART 0xffffff0000000000u
#define SECMEMEND   0xffffff8000000000u

/*
 * Offset into the PML4E at which the mapping for the secure memory region can
 * be found.
 */
static const uintptr_t secmemOffset = ((SECMEMSTART >> 39) << 3) & vmask;
#endif

/*
 * ===========================================================================
 * BEGIN FreeBSD CODE BLOCK
 *
 * $FreeBSD: release/9.0.0/sys/amd64/include/pmap.h 222813 2011-06-07 08:46:13Z attilio $
 * ===========================================================================
 */

/* MMU Flags ---- Intel Nomenclature ---- */
#define PG_V        0x001   /* P    Valid               */
#define PG_RW       0x002   /* R/W  Read/Write          */
#define PG_U        0x004   /* U/S  User/Supervisor     */
#define PG_NC_PWT   0x008   /* PWT  Write through       */
#define PG_NC_PCD   0x010   /* PCD  Cache disable       */
#define PG_A        0x020   /* A    Accessed            */
#define PG_M        0x040   /* D    Dirty               */
#define PG_PS       0x080   /* PS   Page size (0=4k,1=2M)   */
#define PG_PTE_PAT  0x080   /* PAT  PAT index           */
#define PG_G        0x100   /* G    Global              */
#define PG_AVAIL1   0x200   /*    / Available for system    */
#define PG_AVAIL2   0x400   /*   <  programmers use     */
#define PG_AVAIL3   0x800   /*    \                     */
#define PG_PDE_PAT  0x1000  /* PAT  PAT index           */
#define PG_NX       (1ul<<63) /* No-execute             */

/* Various interpretations of the above */
#define PG_W        PG_AVAIL1   /* "Wired" pseudoflag */
#define PG_MANAGED  PG_AVAIL2
#define PG_FRAME    (0x000ffffffffff000ul)
#define PG_PS_FRAME (0x000fffffffe00000ul)
#define PG_PROT     (PG_RW|PG_U)    /* all protection bits . */
#define PG_N        (PG_NC_PWT|PG_NC_PCD)   /* Non-cacheable */

/* Size of the level 1 page table units */
#define PAGE_SHIFT  12      /* LOG2(PAGE_SIZE) */
#define PAGE_SIZE   (1<<PAGE_SHIFT) /* bytes/page */
#define NPTEPG      (PAGE_SIZE/(sizeof (pte_t)))
#define NPTEPGSHIFT 9       /* LOG2(NPTEPG) */
#define PAGE_MASK   (PAGE_SIZE-1)
/* Size of the level 2 page directory units */
#define NPDEPG      (PAGE_SIZE/(sizeof (pde_t)))
#define NPDEPGSHIFT 9       /* LOG2(NPDEPG) */
#define PDRSHIFT    21              /* LOG2(NBPDR) */
#define NBPDR       (1<<PDRSHIFT)   /* bytes/page dir */
#define PDRMASK     (NBPDR-1)
/* Size of the level 3 page directory pointer table units */
#define NPDPEPG     (PAGE_SIZE/(sizeof (pdpte_t)))
#define NPDPEPGSHIFT    9       /* LOG2(NPDPEPG) */
#define PDPSHIFT    30      /* LOG2(NBPDP) */
#define NBPDP       (1<<PDPSHIFT)   /* bytes/page dir ptr table */
#define PDPMASK     (NBPDP-1)
/* Size of the level 4 page-map level-4 table units */
#define NPML4EPG    (PAGE_SIZE/(sizeof (pml4e_t)))
#define NPML4EPGSHIFT   9       /* LOG2(NPML4EPG) */
#define PML4SHIFT   39      /* LOG2(NBPML4) */
#define NBPML4      (1UL<<PML4SHIFT)/* bytes/page map lev4 table */
#define PML4MASK    (NBPML4-1)

/*
 * ===========================================================================
 * END FreeBSD CODE BLOCK
 * ===========================================================================
 */

/* Array describing the physical pages */
DECLARE_NK_DATA_ARR(static page_desc_t, page_desc, NUMPGDESCENTRIES);
 
/* 
 * Description: 
 *   This is a pointer to the PerspicuOS SuperSpace stack, which is used on
 *   calls to SuperSpace or SuperSpace calls.
 */
DECLARE_NK_DATA_ARR(char, SecureStack, 1<<12);
DECLARE_NK_DATA(uintptr_t, SecureStackBase) = (uintptr_t) SecureStack + sizeof(SecureStack);

/* Start and end addresses of user memory */
DECLARE_NK_DATA(static const uintptr_t, USERSTART) = 0x0000000000000000u;
DECLARE_NK_DATA(static const uintptr_t, USEREND) = 0x00007fffffffffffu;

/* Mask to get the proper number of bits from the virtual address */
DECLARE_NK_DATA(static const uintptr_t, vmask) = 0x0000000000000ff8u;

/* The number of references allowed per page table page */
DECLARE_NK_DATA(static const int, maxPTPVARefs) = 1;

/* The count must be at least this value to remove a mapping to a page */
DECLARE_NK_DATA(static const int, minRefCountToRemoveMapping) = 1;

/* Size of the smallest page frame in bytes */
DECLARE_NK_DATA(static const uintptr_t, X86_PAGE_SIZE) = 4096u;

/* Number of bits to shift to get the page number out of a PTE entry */
DECLARE_NK_DATA(static const unsigned, PAGESHIFT) = 12;

/* Zero mapping is the mapping that eliminates the previous entry */
DECLARE_NK_DATA(static const uintptr_t, ZERO_MAPPING) = 0;

/* Mask to get the address bits out of a PTE, PDE, etc. */
DECLARE_NK_DATA(static const uintptr_t, addrmask) = 0x000ffffffffff000u;

//----------------------------------------------------------------------------//
//===-- pMMU Utility Functions --------------------------------------------===//
//----------------------------------------------------------------------------//

/*
 * Function: getVirtual()
 *
 * Description:
 *  This function takes a physical address and converts it into a virtual
 *  address that the SVA VM can access.
 *
 *  In a real system, this is done by having the SVA VM create its own
 *  virtual-to-physical mapping of all of physical memory within its own
 *  reserved portion of the virtual address space.  However, for now, we'll
 *  take advantage of FreeBSD's direct map of physical memory so that we don't
 *  have to set one up.
 */
static inline unsigned char *
getVirtual (uintptr_t physical) 
{
    return (unsigned char *)(physical | 0xfffffe0000000000u);
}

/*
 * Description:
 *  This function takes a page table mapping and set's the flag to read only. 
 * 
 * Inputs:
 *  - mapping: the mapping to add read only flag to
 *
 * Return:
 *  - A new mapping set to read only
 *
 *  Note that setting the read only flag does not necessarily mean that the
 *  read only protection is enabled in the system. It just indicates that if
 *  the system has the write protection enabled then the value of this bit is
 *  considered.
 */
static inline page_entry_t
setMappingReadOnly (page_entry_t mapping) 
{ 
  return (mapping & ~((uintptr_t)(PG_RW))); 
}

/*
 * Description:
 *  This function takes a page table mapping and set's the flag to read/write. 
 * 
 * Inputs:
 *  - mapping: the mapping to which to add read/write permission
 *
 * Return:
 *  - A new mapping set with read/write permission
 */
static inline page_entry_t
setMappingReadWrite (page_entry_t mapping) 
{ 
  return (mapping | PG_RW); 
}


/*
 * Description:
 *  Given a page table entry value, return the page description associate with
 *  the frame being addressed in the mapping.
 *
 * Inputs:
 *  mapping: the mapping with the physical address of the referenced frame
 *
 * Return:
 *  Pointer to the page_desc for this frame
 */
static page_desc_t * 
getPageDescPtr(unsigned long mapping) 
{
    unsigned long frameIndex = (mapping & PG_FRAME) / PAGESIZE;
    if (frameIndex >= NUMPGDESCENTRIES)
        panic ("Nested Kernel: getPageDescPtr: %lx %lx\n", frameIndex, NUMPGDESCENTRIES);
    return page_desc + frameIndex;
}

/*
 * Function: get_pagetable()
 *
 * Description:
 *  Return a physical address that can be used to access the current page table.
 */
static inline unsigned char *
get_pagetable (void) {
  /* Value of the CR3 register */
  uintptr_t cr3;

  /* Get the page table value out of CR3 */
  __asm__ __volatile__ ("movq %%cr3, %0\n" : "=r" (cr3));

  /*
   * Shift the value over 12 bits.  The lower-order 12 bits of the page table
   * pointer are assumed to be zero, and so they are reserved or used by the
   * hardware.
   */
  return (unsigned char *)((((uintptr_t)cr3) & 0x000ffffffffff000u));
}

//===-- Functions for finding the virtual address of page table components ===//
static inline
pml4e_t *
get_pml4eVaddr (uintptr_t cr3, uintptr_t vaddr) {
  /* Offset into the page table */
  uintptr_t offset = (vaddr >> (39 - 3)) & vmask;
  return (pml4e_t *) getVirtual (cr3 | offset);
}

static inline
pdpte_t *
get_pdpteVaddr (pml4e_t * pml4e, uintptr_t vaddr) {
  uintptr_t base   = (*pml4e) & 0x000ffffffffff000u;
  uintptr_t offset = (vaddr >> (30 - 3)) & vmask;
  return (pdpte_t *) getVirtual (base | offset);
}

static inline
pde_t *
get_pdeVaddr (pdpte_t * pdpte, uintptr_t vaddr) {
  uintptr_t base   = (*pdpte) & 0x000ffffffffff000u;
  uintptr_t offset = (vaddr >> (21 - 3)) & vmask;
  return (pde_t *) getVirtual (base | offset);
}

static inline
pte_t *
get_pteVaddr (pde_t * pde, uintptr_t vaddr) {
  uintptr_t base   = (*pde) & 0x000ffffffffff000u;
  uintptr_t offset = (vaddr >> (12 - 3)) & vmask;
  return (pte_t *) getVirtual (base | offset);
}

/*
 * Functions for returing the physical address of page table pages.
 */
static inline uintptr_t
get_pml4ePaddr (unsigned char * cr3, uintptr_t vaddr) {
  /* Offset into the page table */
  uintptr_t offset = ((vaddr >> 39) << 3) & vmask;
  return (((uintptr_t)cr3) | offset);
}

static inline uintptr_t
get_pdptePaddr (pml4e_t * pml4e, uintptr_t vaddr) {
  uintptr_t offset = ((vaddr  >> 30) << 3) & vmask;
  return ((*pml4e & 0x000ffffffffff000u) | offset);
}

static inline uintptr_t
get_pdePaddr (pdpte_t * pdpte, uintptr_t vaddr) {
  uintptr_t offset = ((vaddr  >> 21) << 3) & vmask;
  return ((*pdpte & 0x000ffffffffff000u) | offset);
}

static inline uintptr_t
get_ptePaddr (pde_t * pde, uintptr_t vaddr) {
  uintptr_t offset = ((vaddr >> 12) << 3) & vmask;
  return ((*pde & 0x000ffffffffff000u) | offset);
}


/* 
 * Function: get_pgeVaddr
 *
 * Description:
 *  This function does page walk to find the entry controlling access to the
 *  specified address. The function takes into consideration the potential use
 *  of larger page sizes.
 * 
 * Inputs:
 *  vaddr - Virtual Address to find entry for
 *
 * Return value:
 *  0 - There is no mapping for this virtual address.
 *  Otherwise, a pointer to the PTE that controls the mapping of this virtual
 *  address is returned.
 */
static inline
page_entry_t * 
get_pgeVaddr (uintptr_t vaddr) {
    /* Pointer to the page table entry for the virtual address */
    page_entry_t *pge = 0;

    /* Get the base of the pml4 to traverse */
    uintptr_t cr3 = get_pagetable();
    if ((cr3 & 0xfffffffffffff000u) == 0)
        return 0;

    /* Get the VA of the pml4e for this vaddr */
    pml4e_t *pml4e = get_pml4eVaddr (cr3, vaddr);

    if (*pml4e & PG_V) {
        /* Get the VA of the pdpte for this vaddr */
        pdpte_t *pdpte = get_pdpteVaddr (pml4e, vaddr);
        if (*pdpte & PG_V) {
            /* 
             * The PDPE can be configurd in large page mode. If it is then we have the
             * entry corresponding to the given vaddr If not then we go deeper in the
             * page walk.
             */
            if (*pdpte & PG_PS) {
                pge = pdpte;
            } else {
                /* Get the pde associated with this vaddr */
                pde_t *pde = get_pdeVaddr (pdpte, vaddr);
                if (*pde & PG_V) {
                    /* 
                     * As is the case with the pdpte, if the pde is configured for large
                     * page size then we have the corresponding entry. Otherwise we need
                     * to traverse one more level, which is the last. 
                     */
                    if (*pde & PG_PS) {
                        pge = pde;
                    } else {
                        pge = get_pteVaddr (pde, vaddr);
                    }
                }
            }
        }
    }

    /* Return the entry corresponding to this vaddr */
    return pge;
}

/*
 * Function: getPhysicalAddr()
 *
 * Description:
 *  Find the physical page number of the specified virtual address.
 */
static uintptr_t
getPhysicalAddr (void * v) 
{
    /* Mask to get the proper number of bits from the virtual address */
#if NO_PORTED_YET
    static const uintptr_t vmask = 0x0000000000000fffu;
#endif

    /* Virtual address to convert */
    uintptr_t vaddr  = ((uintptr_t) v);

    /* Offset into the page table */
    uintptr_t offset = 0;

    /*
     * Get the currently active page table.
     */
    unsigned char * cr3 = get_pagetable();

    /*
     * Get the address of the PML4e.
     */
    pml4e_t * pml4e = get_pml4eVaddr (cr3, vaddr);

    /*
     * Use the PML4E to get the address of the PDPTE.
     */
    pdpte_t * pdpte = get_pdpteVaddr (pml4e, vaddr);

    /*
     * Determine if the PDPTE has the PS flag set.  If so, then it's pointing to
     * a 1 GB page; return the physical address of that page.
     */
    if ((*pdpte) & PG_PS) {
        return (*pdpte & 0x000fffffffffffffu) >> 30;
    }

    /*
     * Find the page directory entry table from the PDPTE value.
     */
    pde_t * pde = get_pdeVaddr (pdpte, vaddr);

    /*
     * Determine if the PDE has the PS flag set.  If so, then it's pointing to a
     * 2 MB page; return the physical address of that page.
     */
    if ((*pde) & PG_PS) {
        return (*pde & 0x000fffffffe00000u) + (vaddr & 0x1fffffu);
    }

    /*
     * Find the PTE pointed to by this PDE.
     */
    pte_t * pte = get_pteVaddr (pde, vaddr);

    /*
     * Compute the physical address.
     */
    offset = vaddr & vmask;
    uintptr_t paddr = (*pte & 0x000ffffffffff000u) + offset;
    return paddr;
}

/*
 * Function: declare_ptp_and_walk_pt_entries
 *
 * Descriptions:
 *  This function recursively walks a page table and it's entries to initalize
 *  the nested kernel data structures for the given page. This function is
 *  meant to initialize SVA data structures so they mirror the static page
 *  table setup by a kernel. However, it uses the paging structure itself to
 *  walk the pages, which means it should be agnostic to the operating system
 *  being employed upon. The function only walks into page table pages that are
 *  valid or enabled. It also makes sure that if a given page table is already
 *  active in NK then it skips over initializing its entries as that could
 *  cause an infinite loop of recursion. This is an issue in FreeBSD as they
 *  have a recursive mapping in the pml4 top level page table page.
 *  
 *  If a given page entry is marked as having a larger page size, such as may
 *  be the case with a 2MB page size for PD entries, then it doesn't traverse
 *  the page. Therefore, if the kernel page tables are configured correctly
 *  this won't initialize any NK page descriptors that aren't in use.
 *
 *  The primary objective of this code is to for each valid page table page:
 *      [1] Initialize the page_desc for the given page
 *      [2] Set the page permissions as read only
 *
 * Assumptions:
 *  - The number of entries per page assumes a amd64 paging hardware mechanism.
 *    As such the number of entires per a 4KB page table page is 2^9 or 512
 *    entries. 
 *  - This page referenced in pageMapping has already been determined to be
 *    valid and requires SVA metadata to be created.
 *
 * Inputs:
 *   pageMapping: Page mapping associated with the given page being traversed.
 *                This mapping identifies the physical address/frame of the
 *                page table page so that SVA can initialize it's data
 *                structures then recurse on each entry in the page table page. 
 *  numPgEntries: The number of entries for a given level page table. 
 *     pageLevel: The page level of the given mapping {1,2,3,4}.
 *
 *
 * TODO: 
 *  - Modify the page entry number to be dynamic in some way to accomodate
 *    differing numbers of entries. This only impacts how we traverse the
 *    address structures. The key issue is that we don't want to traverse an
 *    entry that randomly has the valid bit set, but not have it point to a
 *    real page. For example, if the kernel did not zero out the entire page
 *    table page and only inserted a subset of entries in the page table, the
 *    non set entries could be identified as holding valid mappings, which
 *    would then cause this function to traverse down truly invalid page table
 *    pages. In FreeBSD this isn't an issue given the way they initialize the
 *    static mapping, but could be a problem given differnet intialization
 *    methods.
 *
 *  - Add code to mark direct map page table pages to prevent the OS from
 *    modifying them.
 *
 */
#define DEBUG_INIT 0
void 
declare_ptp_and_walk_pt_entries(page_entry_t *pageEntry, unsigned long
        numPgEntries, enum page_type_t pageLevel ) 
{ 
    int i;
    int traversedPTEAlready;
    enum page_type_t subLevelPgType;
    unsigned long numSubLevelPgEntries;
    page_desc_t *thisPg;
    page_entry_t pageMapping; 
    page_entry_t *pagePtr;

    /* Store the pte value for the page being traversed */
    pageMapping = *pageEntry;

    /* Set the page pointer for the given page */
#if USE_VIRT
    uintptr_t pagePhysAddr = pageMapping & PG_FRAME;
    pagePtr = (page_entry_t *) getVirtual(pagePhysAddr);
#else
    pagePtr = (page_entry_t *) (pageMapping & PG_FRAME);
#endif

    /* Get the page_desc for this page */
    thisPg = getPageDescPtr(pageMapping);

    /* Mark if we have seen this traversal already */
    traversedPTEAlready = (thisPg->type != PG_UNUSED);

#if DEBUG_INIT >= 1
    /* Character inputs to make the printing pretty for debugging */
    char * indent = "";
    char * l4s = "L4:";
    char * l3s = "\tL3:";
    char * l2s = "\t\tL2:";
    char * l1s = "\t\t\tL1:";

    switch (pageLevel){
        case PG_L4:
            indent = l4s;
            printf("%sSetting L4 Page: mapping:0x%lx\n", indent, pageMapping);
            break;
        case PG_L3:
            indent = l3s;
            printf("%sSetting L3 Page: mapping:0x%lx\n", indent, pageMapping);
            break;
        case PG_L2:
            indent = l2s;
            printf("%sSetting L2 Page: mapping:0x%lx\n", indent, pageMapping);
            break;
        case PG_L1:
            indent = l1s;
            printf("%sSetting L1 Page: mapping:0x%lx\n", indent, pageMapping);
            break;
        default:
            break;
    }
#endif

    /*
     * For each level of page we do the following:
     *  - Set the page descriptor type for this page table page
     *  - Set the sub level page type and the number of entries for the
     *    recursive call to the function.
     */
    switch(pageLevel){

        case PG_L4:

            thisPg->type = PG_L4;       /* Set the page type to L4 */
            thisPg->user = 0;           /* Set the priv flag to kernel */
            ++(thisPg->count);
            subLevelPgType = PG_L3;
            numSubLevelPgEntries = NPML4EPG;//    numPgEntries;
            break;

        case PG_L3:

            if (thisPg->type != PG_L4)
                thisPg->type = PG_L3;       /* Set the page type to L3 */
            thisPg->user = 0;           /* Set the priv flag to kernel */
            ++(thisPg->count);
            subLevelPgType = PG_L2;
            numSubLevelPgEntries = NPDPEPG; //numPgEntries;
            break;

        case PG_L2:

            /* 
             * If the L2 page mapping signifies that this mapping references a
             * 1GB page frame, then get the frame address using the correct
             * page mask for a L3 page entry and initialize the page_desc for
             * this entry. Then return as we don't need to traverse frame
             * pages.
             */
            if ((pageMapping & PG_PS) != 0) {
#if DEBUG_INIT >= 1
                printf("\tIdentified 1GB page...\n");
#endif
                unsigned long index = (pageMapping & ~PDPMASK) / PAGESIZE;
                page_desc[index].type = PG_TKDATA;
                page_desc[index].user = 0;           /* Set the priv flag to kernel */
                ++(page_desc[index].count);
                return;
            } else {
                thisPg->type = PG_L2;       /* Set the page type to L2 */
                thisPg->user = 0;           /* Set the priv flag to kernel */
                ++(thisPg->count);
                subLevelPgType = PG_L1;
                numSubLevelPgEntries = NPDEPG; // numPgEntries;
            }
            break;

        case PG_L1:
            /* 
             * If my L1 page mapping signifies that this mapping references a 2MB
             * page frame, then get the frame address using the correct page mask
             * for a L2 page entry and initialize the page_desc for this entry. 
             * Then return as we don't need to traverse frame pages.
             */
            if ((pageMapping & PG_PS) != 0){
#if DEBUG_INIT >= 1
                printf("\tIdentified 2MB page...\n");
#endif
                /* TODO: this should use thisPg... */
                /* The frame address referencing the page obtained */
                unsigned long index = (pageMapping & ~PDRMASK) / PAGESIZE;
                page_desc[index].type = PG_TKDATA;
                page_desc[index].user = 0;           /* Set the priv flag to kernel */
                ++(page_desc[index].count);
                return;
            } else {
                thisPg->type = PG_L1;       /* Set the page type to L1 */
                thisPg->user = 0;           /* Set the priv flag to kernel */
                ++(thisPg->count);
                subLevelPgType = PG_TKDATA;
                numSubLevelPgEntries = NPTEPG;//      numPgEntries;
            }
            break;

        default:
            printf("SVA: page type %d. Frame addr: %p\n",thisPg->type, pagePtr); 
            panic("SVA: walked an entry with invalid page type.");
    }

    /* 
     * There is one recursive mapping, which is the last entry in the PML4 page
     * table page. Thus we return before traversing the descriptor again.
     * Notice though that we keep the last assignment to the page as the page
     * type information. 
     */
    if(traversedPTEAlready) {
#if DEBUG_INIT >= 1
        printf("%sRecursed on already initialized page_desc\n", indent);
#endif
        return;
    }

#if DEBUG_INIT >= 1
    u_long nNonValPgs=0;
    u_long nValPgs=0;
#endif
    /* 
     * Iterate through all the entries of this page, recursively calling the
     * walk on all sub entries.
     */
    for (i = 0; i < numSubLevelPgEntries; i++){
#if 0
        /*
         * Do not process any entries that implement the direct map.  This prevents
         * us from marking physical pages in the direct map as kernel data pages.
         */
        if ((pageLevel == PG_L4) && (i == (0xfffffe0000000000 / 0x1000))) {
            continue;
        }
#endif
        page_entry_t * nextEntry = & pagePtr[i];

#if DEBUG_INIT >= 5
        printf("%sPagePtr in loop: %p, val: 0x%lx\n", indent, nextEntry, *nextEntry);
#endif

        /* 
         * If this entry is valid then recurse the page pointed to by this page
         * table entry.
         */
        if (*nextEntry & PG_V) {
#if DEBUG_INIT >= 1
            nValPgs++;
#endif 

            /* 
             * If we hit the level 1 pages we have hit our boundary condition for
             * the recursive page table traversals. Now we just mark the leaf page
             * descriptors.
             */
            if (pageLevel == PG_L1){
#if DEBUG_INIT >= 2
                printf("%sInitializing leaf entry: pteaddr: %p, mapping: 0x%lx\n",
                        indent, nextEntry, *nextEntry);
#endif
            } else {
#if DEBUG_INIT >= 2
                printf("%sProcessing:pte addr: %p, newPgAddr: %p, mapping: 0x%lx\n",
                        indent, nextEntry, (*nextEntry & PG_FRAME), *nextEntry ); 
#endif
                declare_ptp_and_walk_pt_entries(nextEntry,
                        numSubLevelPgEntries, subLevelPgType); 
            }
        } 
#if DEBUG_INIT >= 1
        else {
            nNonValPgs++;
        }
#endif
    }

#if DEBUG_INIT >= 1
    SVA_ASSERT((nNonValPgs + nValPgs) == 512, "Wrong number of entries traversed");

    printf("%sThe number of || non valid pages: %lu || valid pages: %lu\n",
            indent, nNonValPgs, nValPgs);
#endif

}

/*
 * Function: declare_kernel_code_pages()
 *
 * Description:
 *  Mark all kernel code pages as code pages.
 *
 * Inputs: 
 *  startVA    - The first virtual address of the memory region.
 *  endVA      - The last virtual address of the memory region.
 *  pgType     - The nested kernel page type 
 */
void
init_protected_pages (uintptr_t startVA, uintptr_t endVA, enum page_type_t
        pgType) 
{
    /* Get pointers for the pages */
    uintptr_t page;
    uintptr_t startPA = getPhysicalAddr(startVA) & PG_FRAME;
    uintptr_t endPA = getPhysicalAddr(endVA) & PG_FRAME;

#if NOT_PORTED_YET
    PERSPDEBUG(dec_ker_cod_pgs,"\nDeclaring pages for range: %p -- %p\n",
            startVA, endVA);

    /*
     * Scan through each page in the text segment.  Note that it is a pgType
     * page, and make the page read-only within the page table.
     */
    for (page = startPA; page < endPA; page += PAGESIZE) {
        /* Mark the page as both a code page and kernel level */
        page_desc[page / PAGESIZE].type = PG_CODE;
        page_desc[page / PAGESIZE].user = 0;

        /* Configure the MMU so that the page is read-only */
        page_entry_t * page_entry = get_pgeVaddr (startVA + (page - startPA));
        page_entry_store(page_entry, setMappingReadOnly (*page_entry));
    }

    PERSPDEBUG(dec_ker_cod_pgs,"\nFinished decl pages for range: %p -- %p\n",
            startVA, endVA);
#endif
}

/*
 * Function: pmmu_init()
 
 * Description:
 *  This function initializes the nk vmmu unit by zeroing out the page
 *  descriptors, capturing the statically allocated initial kernel mmu state,
 *  and identifying all kernel code pages, and setting them in the page
 *  descriptor array.
 *
 *  To initialize the sva page descriptors, this function takes the pml4 base
 *  mapping and walks down each level of the page table tree. 
 *
 *  NOTE: In this function we assume that the page mapping for the kpml4 has
 *  physical addresses in it. We then dereference by obtaining the virtual
 *  address mapping of this page. This works whether or not the processor is in
 *  a virtually addressed or physically addressed mode. 
 *
 * Inputs:
 *  - kpml4Mapping  : Mapping referencing the base kernel pml4 page table page
 *  - nkpml4e       : The number of entries in the pml4
 *  - firstpaddr    : A pointer to the physical address of the first free frame.
 *  - btext         : The first virtual address of the text segment.
 *  - etext         : The last virtual address of the text segment.
 */ 
void 
pmmu_init(pml4e_t * kpml4Mapping, unsigned long nkpml4e, uintptr_t *
        firstpaddr, uintptr_t btext, uintptr_t etext)
{
    NKDEBUG(mmu_init,"Initializing MMU");

    /* Get the virtual address of the pml4e mapping */
#if USE_VIRT
    pml4e_t * kpml4eVA = (pml4e_t *) getVirtual( (uintptr_t) kpml4Mapping);
#else
    pml4e_t * kpml4eVA = kpml4Mapping;
#endif

    /* Zero out the page descriptor array */
    memset (page_desc, 0, NUMPGDESCENTRIES * sizeof(page_desc_t));

    /* Walk the kernel page tables and initialize the sva page_desc */
    declare_ptp_and_walk_pt_entries(kpml4eVA, nkpml4e, PG_L4);

    /* Identify kernel code pages and intialize the descriptors */
    init_protected_pages(btext, etext, PG_CODE);

#if NOT_PORTED_YET
    /* Now load the initial value of the cr3 to complete kernel init */
    _load_cr3(*kpml4Mapping & PG_FRAME);

    /* Make existing page table pages read-only */
    makePTReadOnly();

    /* Make all SuperSpace pages read-only */
    //makeSuperSpaceRO();
    declare_kernel_code_pages(_pspacestart, _pspaceend);

    /* Note that the MMU is now initialized */
    mmuIsInitialized = 1;
#endif

    NKDEBUG(mmu_init,"Completed MMU init");
}
