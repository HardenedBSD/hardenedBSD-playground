/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_vma_manager.h>
#include <linux/mm.h>
#include <linux/pfn_t.h>
#include <linux/rbtree.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define TTM_BO_VM_NUM_PREFAULT 16

static int ttm_bo_vm_fault_idle(struct ttm_buffer_object *bo,
				struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	int ret = 0;

	if (likely(!bo->moving))
		goto out_unlock;

	/*
	 * Quick non-stalling check for idle.
	 */
	if (fence_is_signaled(bo->moving))
		goto out_clear;

	/*
	 * If possible, avoid waiting for GPU with mmap_sem
	 * held.
	 */
	if (vmf->flags & FAULT_FLAG_ALLOW_RETRY) {
		ret = VM_FAULT_RETRY;
		if (vmf->flags & FAULT_FLAG_RETRY_NOWAIT)
			goto out_unlock;

		up_read(&vma->vm_mm->mmap_sem);
		(void) fence_wait(bo->moving, true);
		goto out_unlock;
	}

	/*
	 * Ordinary wait.
	 */
	ret = fence_wait(bo->moving, true);
	if (unlikely(ret != 0)) {
		printf("fence_wait returned with error %d\n", ret);
		ret = (ret != -ERESTARTSYS) ? VM_FAULT_SIGBUS :
			VM_FAULT_NOPAGE;
		goto out_unlock;
	}

out_clear:
	fence_put(bo->moving);
	bo->moving = NULL;

out_unlock:
	return ret;
}

static int ttm_bo_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct ttm_buffer_object *bo = (struct ttm_buffer_object *)
	    vma->vm_private_data;
	struct ttm_bo_device *bdev = bo->bdev;
	unsigned long page_offset;
	unsigned long page_last;
	unsigned long pfn;
	struct ttm_tt *ttm = NULL;
	struct page *page;
	int ret;
	int i;
	unsigned long address = (unsigned long)vmf->virtual_address;
	int retval = VM_FAULT_NOPAGE;
	struct ttm_mem_type_manager *man =
		&bdev->man[bo->mem.mem_type];
	struct vm_area_struct cvma;

	/*
	 * Work around locking order reversal in fault / nopfn
	 * between mmap_sem and bo_reserve: Perform a trylock operation
	 * for reserve, and if it fails, retry the fault after waiting
	 * for the buffer to become unreserved.
	 */
	ret = ttm_bo_reserve(bo, true, true, NULL);
	if (unlikely(ret != 0)) {
		if (ret != -EBUSY)
			return VM_FAULT_NOPAGE;

		if (vmf->flags & FAULT_FLAG_ALLOW_RETRY) {
			if (!(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
				up_read(&vma->vm_mm->mmap_sem);
				(void) ttm_bo_wait_unreserved(bo);
			}

			return VM_FAULT_RETRY;
		}

		/*
		 * If we'd want to change locking order to
		 * mmap_sem -> bo::reserve, we'd use a blocking reserve here
		 * instead of retrying the fault...
		 */
		return VM_FAULT_NOPAGE;
	}

	/*
	 * Refuse to fault imported pages. This should be handled
	 * (if at all) by redirecting mmap to the exporter.
	 */
	if (bo->ttm && (bo->ttm->page_flags & TTM_PAGE_FLAG_SG)) {
		printf("refused to fault all pages\n");

		retval = VM_FAULT_SIGBUS;
		goto out_unlock;
	}

	if (bdev->driver->fault_reserve_notify) {
		ret = bdev->driver->fault_reserve_notify(bo);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
		case -ERESTARTSYS:
			retval = VM_FAULT_NOPAGE;
			goto out_unlock;
		default:
			printf("reverse_notify failed\n");
			retval = VM_FAULT_SIGBUS;
			goto out_unlock;
		}
	}

	/*
	 * Wait for buffer data in transit, due to a pipelined
	 * move.
	 */
	ret = ttm_bo_vm_fault_idle(bo, vma, vmf);
	if (unlikely(ret != 0)) {
		retval = ret;
		goto out_unlock;
	}

	ret = ttm_mem_io_lock(man, true);
	if (unlikely(ret != 0)) {
		retval = VM_FAULT_NOPAGE;
		goto out_unlock;
	}
	ret = ttm_mem_io_reserve_vm(bo);
	if (unlikely(ret != 0)) {
		printf("mem_io_reserve failed\n");
		retval = VM_FAULT_SIGBUS;
		goto out_io_unlock;
	}

	page_offset = ((address - vma->vm_start) >> PAGE_SHIFT) +
		vma->vm_pgoff - drm_vma_node_start(&bo->vma_node);
	page_last = vma_pages(vma) + vma->vm_pgoff -
		drm_vma_node_start(&bo->vma_node);

	if (unlikely(page_offset >= bo->num_pages)) {
		printf("page_offset past num_pages\n");
		retval = VM_FAULT_SIGBUS;
		goto out_io_unlock;
	}

	/*
	 * Make a local vma copy to modify the page_prot member
	 * and vm_flags if necessary. The vma parameter is protected
	 * by mmap_sem in write mode.
	 */
	cvma = *vma;
	cvma.vm_page_prot = vm_get_page_prot(cvma.vm_flags);

	if (bo->mem.bus.is_iomem) {
		cvma.vm_page_prot = ttm_io_prot(bo->mem.placement,
						cvma.vm_page_prot);
	} else {
		ttm = bo->ttm;
		cvma.vm_page_prot = ttm_io_prot(bo->mem.placement,
						cvma.vm_page_prot);

		/* Allocate all page at once, most common usage */
		if (ttm->bdev->driver->ttm_tt_populate(ttm)) {
			retval = VM_FAULT_OOM;
			goto out_io_unlock;
		}
	}

	/*
	 * Speculatively prefault a number of pages. Only error on
	 * first page.
	 */
#ifdef __linux__
	for (i = 0; i < TTM_BO_VM_NUM_PREFAULT; ++i) {
		if (bo->mem.bus.is_iomem)
			pfn = ((bo->mem.bus.base + bo->mem.bus.offset) >> PAGE_SHIFT) + page_offset;
		else {
			page = ttm->pages[page_offset];
			if (unlikely(!page && i == 0)) {
				retval = VM_FAULT_OOM;
				goto out_io_unlock;
			} else if (unlikely(!page)) {
				break;
			}
			page->mapping = vma->vm_file->f_mapping;
			page->index = drm_vma_node_start(&bo->vma_node) +
				page_offset;
			pfn = page_to_pfn(page);
		}

		if (vma->vm_flags & VM_MIXEDMAP)
			ret = vm_insert_mixed(&cvma, address,
					__pfn_to_pfn_t(pfn, PFN_DEV));
		else
			ret = vm_insert_pfn(&cvma, address, pfn);

		/*
		 * Somebody beat us to this PTE or prefaulting to
		 * an already populated PTE, or prefaulting error.
		 */

		if (unlikely((ret == -EBUSY) || (ret != 0 && i > 0)))
			break;
		else if (unlikely(ret != 0)) {
			retval =
			    (ret == -ENOMEM) ? VM_FAULT_OOM : VM_FAULT_SIGBUS;
			goto out_io_unlock;
		}

		address += PAGE_SIZE;
		if (unlikely(++page_offset >= page_last))
			break;
	}
#else
	vm_object_t obj;
	vm_pindex_t pidx;

	obj = vma->vm_obj;
	pidx = OFF_TO_IDX(address);
	vma->vm_pfn_first = pidx;

	VM_OBJECT_WLOCK(obj);
	for (i = 0; i < TTM_BO_VM_NUM_PREFAULT && page_offset < page_last;
	    i++, page_offset++, pidx++) {
retry:
		page = vm_page_lookup(obj, pidx);
		if (page != NULL) {
			if (vm_page_sleep_if_busy(page, "ttmflt"))
				goto retry;
		} else {
			if (bo->mem.bus.is_iomem) {
				pfn = OFF_TO_IDX(bo->mem.bus.base +
				    bo->mem.bus.offset) + page_offset;
				page = PHYS_TO_VM_PAGE(IDX_TO_OFF(pfn));
			} else {
				page = ttm->pages[page_offset];
				if (page == NULL)
					goto fail;
			}
			if (vm_page_busied(page))
				goto fail;
			if (vm_page_insert(page, obj, pidx))
				goto fail;
			page->valid = VM_PAGE_BITS_ALL;
		}
		pmap_page_set_memattr(page,
		    pgprot2cachemode(cvma.vm_page_prot));
		vm_page_xbusy(page);
		vma->vm_pfn_count++;
		continue;
fail:
		if (i == 0)
			retval = VM_FAULT_OOM;
		break;
	}
	VM_OBJECT_WUNLOCK(obj);
#endif
out_io_unlock:
	ttm_mem_io_unlock(man);
out_unlock:
	ttm_bo_unreserve(bo);
	return retval;
}

static void ttm_bo_vm_open(struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo =
	    (struct ttm_buffer_object *)vma->vm_private_data;

	WARN_ON(bo->bdev->dev_mapping != vma->vm_file->f_mapping);

	(void)ttm_bo_reference(bo);
}

static void ttm_bo_vm_close(struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo = (struct ttm_buffer_object *)vma->vm_private_data;

	ttm_bo_unref(&bo);
	vma->vm_private_data = NULL;
}

static const struct vm_operations_struct ttm_bo_vm_ops = {
	.fault = ttm_bo_vm_fault,
	.open = ttm_bo_vm_open,
	.close = ttm_bo_vm_close
};

static struct ttm_buffer_object *ttm_bo_vm_lookup(struct ttm_bo_device *bdev,
						  unsigned long offset,
						  unsigned long pages)
{
	struct drm_vma_offset_node *node;
	struct ttm_buffer_object *bo = NULL;

	drm_vma_offset_lock_lookup(&bdev->vma_manager);

	node = drm_vma_offset_lookup_locked(&bdev->vma_manager, offset, pages);
	if (likely(node)) {
		bo = container_of(node, struct ttm_buffer_object, vma_node);
		if (!kref_get_unless_zero(&bo->kref))
			bo = NULL;
	}

	drm_vma_offset_unlock_lookup(&bdev->vma_manager);

	if (!bo)
		pr_err("Could not find buffer object to map\n");

	return bo;
}

int ttm_bo_mmap(struct file *filp, struct vm_area_struct *vma,
		struct ttm_bo_device *bdev)
{
	struct ttm_bo_driver *driver;
	struct ttm_buffer_object *bo;
	int ret;

	bo = ttm_bo_vm_lookup(bdev, vma->vm_pgoff, vma_pages(vma));
	if (unlikely(!bo))
		return -EINVAL;

	driver = bo->bdev->driver;
	if (unlikely(!driver->verify_access)) {
		ret = -EPERM;
		goto out_unref;
	}
	ret = driver->verify_access(bo, filp);
	if (unlikely(ret != 0))
		goto out_unref;

	vma->vm_ops = &ttm_bo_vm_ops;

	/*
	 * Note: We're transferring the bo reference to
	 * vma->vm_private_data here.
	 */

	vma->vm_private_data = bo;

	/*
	 * We'd like to use VM_PFNMAP on shared mappings, where
	 * (vma->vm_flags & VM_SHARED) != 0, for performance reasons,
	 * but for some reason VM_PFNMAP + x86 PAT + write-combine is very
	 * bad for performance. Until that has been sorted out, use
	 * VM_MIXEDMAP on all mappings. See freedesktop.org bug #75719
	 */
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
out_unref:
	ttm_bo_unref(&bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mmap);

int ttm_fbdev_mmap(struct vm_area_struct *vma, struct ttm_buffer_object *bo)
{
	if (vma->vm_pgoff != 0)
		return -EACCES;

	vma->vm_ops = &ttm_bo_vm_ops;
	vma->vm_private_data = ttm_bo_reference(bo);
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_flags |= VM_IO | VM_DONTEXPAND;
	return 0;
}
EXPORT_SYMBOL(ttm_fbdev_mmap);
