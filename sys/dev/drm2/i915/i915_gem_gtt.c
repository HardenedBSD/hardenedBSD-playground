/*
 * Copyright © 2010 Daniel Vetter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>
#include <dev/drm2/i915/i915_drm.h>
#include <dev/drm2/i915/i915_drv.h>
#include <dev/drm2/i915/intel_drv.h>
#include <sys/sched.h>
#include <sys/sf_buf.h>
#include <vm/vm_pageout.h>

static void i915_ggtt_clear_range(struct drm_device *dev,
				 unsigned first_entry,
				  unsigned num_entries);

struct agp_bridge_data;
static int intel_gmch_probe(struct pci_dev *bridge_pdev, struct pci_dev *gpu_pdev,
		     struct agp_bridge_data *bridge);

static void drm2_intel_gtt_get(size_t *gtt_total, size_t *stolen_size,
		   phys_addr_t *mappable_base, unsigned long *mappable_end)
{
#ifdef notyet
	*gtt_total = intel_private.gtt_total_entries << PAGE_SHIFT;
	*stolen_size = intel_private.stolen_size;
	*mappable_base = intel_private.gma_bus_addr;
	*mappable_end = intel_private.gtt_mappable_entries << PAGE_SHIFT;
#endif	
}



typedef uint32_t gtt_pte_t;

/* PPGTT stuff */
#define GEN6_GTT_ADDR_ENCODE(addr)	((addr) | (((addr) >> 28) & 0xff0))

#define GEN6_PDE_VALID			(1 << 0)
/* gen6+ has bit 11-4 for physical addr bit 39-32 */
#define GEN6_PDE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)

#define GEN6_PTE_VALID			(1 << 0)
#define GEN6_PTE_UNCACHED		(1 << 1)
#define HSW_PTE_UNCACHED		(0)
#define GEN6_PTE_CACHE_LLC		(2 << 1)
#define GEN6_PTE_CACHE_LLC_MLC		(3 << 1)
#define GEN6_PTE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)

static inline gtt_pte_t gen6_pte_encode(struct drm_device *dev,
					dma_addr_t addr,
					enum i915_cache_level level)
{
	gtt_pte_t pte = GEN6_PTE_VALID;
	pte |= GEN6_PTE_ADDR_ENCODE(addr);

	switch (level) {
	case I915_CACHE_LLC_MLC:
		/* Haswell doesn't set L3 this way */
		if (IS_HASWELL(dev))
			pte |= GEN6_PTE_CACHE_LLC;
		else
			pte |= GEN6_PTE_CACHE_LLC_MLC;
		break;
	case I915_CACHE_LLC:
		pte |= GEN6_PTE_CACHE_LLC;
		break;
	case I915_CACHE_NONE:
		if (IS_HASWELL(dev))
			pte |= HSW_PTE_UNCACHED;
		else
			pte |= GEN6_PTE_UNCACHED;
		break;
	default:
		BUG();
	}


	return pte;
}

/* PPGTT support for Sandybdrige/Gen6 and later */
static void i915_ppgtt_clear_range(struct i915_hw_ppgtt *ppgtt,
				   unsigned first_entry,
				   unsigned num_entries)
{
	gtt_pte_t *pt_vaddr;
	gtt_pte_t scratch_pte;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned last_pte, i;
	struct sf_buf *sf;

	scratch_pte = gen6_pte_encode(ppgtt->dev, ppgtt->scratch_page_dma_addr,
				 I915_CACHE_LLC);

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		sched_pin();
		sf = sf_buf_alloc(ppgtt->pt_pages[act_pd], SFB_CPUPRIVATE);
		pt_vaddr = (uint32_t *)(uintptr_t)sf_buf_kva(sf);

		for (i = first_pte; i < last_pte; i++)
			pt_vaddr[i] = scratch_pte;

		sf_buf_free(sf);
		sched_unpin();

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

int i915_gem_init_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt;
	unsigned first_pd_entry_in_global_pt;
	int i;
	int ret = -ENOMEM;

	/* ppgtt PDEs reside in the global gtt pagetable, which has 512*1024
	 * entries. For aliasing ppgtt support we just steal them at the end for
	 * now. */
       first_pd_entry_in_global_pt = gtt_total_entries(dev_priv->gtt);

	ppgtt = malloc(sizeof(*ppgtt), DRM_I915_GEM, M_WAITOK | M_ZERO);
	if (!ppgtt)
		return ret;

	ppgtt->dev = dev;
	ppgtt->num_pd_entries = I915_PPGTT_PD_ENTRIES;
	ppgtt->pt_pages = kzalloc(sizeof(struct page *)*ppgtt->num_pd_entries,
				  GFP_KERNEL);
	if (!ppgtt->pt_pages)
		goto err_ppgtt;

	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		ppgtt->pt_pages[i] = alloc_page(GFP_KERNEL);
		if (!ppgtt->pt_pages[i])
			goto err_pt_alloc;
	}

	if (dev_priv->mm.needs_dmar) {
		ppgtt->pt_dma_addr = malloc(sizeof(dma_addr_t)
						*ppgtt->num_pd_entries,
					     DRM_I915_GEM, M_WAITOK | M_ZERO);
		if (!ppgtt->pt_dma_addr)
			goto err_pt_alloc;

#ifdef CONFIG_INTEL_IOMMU /* <- Added as a marker on FreeBSD. */
		for (i = 0; i < ppgtt->num_pd_entries; i++) {
			dma_addr_t pt_addr;

			pt_addr = pci_map_page(dev->pdev, ppgtt->pt_pages[i],
					       0, 4096,
					       PCI_DMA_BIDIRECTIONAL);

			if (pci_dma_mapping_error(dev->pdev,
						  pt_addr)) {
				ret = -EIO;
				goto err_pd_pin;

			}
			ppgtt->pt_dma_addr[i] = pt_addr;
		}
#endif
	}

	ppgtt->scratch_page_dma_addr = dev_priv->gtt.scratch_page_dma;

	i915_ppgtt_clear_range(ppgtt, 0,
			   ppgtt->num_pd_entries*I915_PPGTT_PT_ENTRIES);

	ppgtt->pd_offset = (first_pd_entry_in_global_pt)*sizeof(gtt_pte_t);

	dev_priv->mm.aliasing_ppgtt = ppgtt;

	return 0;

#ifdef CONFIG_INTEL_IOMMU /* <- Added as a marker on FreeBSD. */
err_pd_pin:
	if (ppgtt->pt_dma_addr) {
		for (i--; i >= 0; i--)
			pci_unmap_page(dev->pdev, ppgtt->pt_dma_addr[i],
				       4096, PCI_DMA_BIDIRECTIONAL);
	}
#endif
err_pt_alloc:
	kfree(ppgtt->pt_dma_addr);
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		if (ppgtt->pt_pages[i]) {
			vm_page_unwire(ppgtt->pt_pages[i], PQ_INACTIVE);
			vm_page_free(ppgtt->pt_pages[i]);
		}
	}
	kfree(ppgtt->pt_pages);
err_ppgtt:
	kfree(ppgtt);

	return ret;
}

void i915_gem_cleanup_aliasing_ppgtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	int i;

	if (!ppgtt)
		return;

#ifdef CONFIG_INTEL_IOMMU /* <- Added as a marker on FreeBSD. */
	if (ppgtt->pt_dma_addr) {
		for (i = 0; i < ppgtt->num_pd_entries; i++)
			pci_unmap_page(dev->pdev, ppgtt->pt_dma_addr[i],
				       4096, PCI_DMA_BIDIRECTIONAL);
	}
#endif

	kfree(ppgtt->pt_dma_addr);
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		vm_page_unwire(ppgtt->pt_pages[i], PQ_INACTIVE);
		vm_page_free(ppgtt->pt_pages[i]);
	}
	kfree(ppgtt->pt_pages);
	kfree(ppgtt);
}

static void i915_ppgtt_insert_pages(struct i915_hw_ppgtt *ppgtt,
					 vm_page_t *pages,
					 unsigned first_entry,
					 unsigned num_entries,
					 enum i915_cache_level cache_level)
{
	uint32_t *pt_vaddr;
	unsigned act_pd = first_entry / I915_PPGTT_PT_ENTRIES;
	unsigned first_pte = first_entry % I915_PPGTT_PT_ENTRIES;
	unsigned j, last_pte;
	vm_paddr_t page_addr;
	struct sf_buf *sf;

	while (num_entries) {
		last_pte = first_pte + num_entries;
		if (last_pte > I915_PPGTT_PT_ENTRIES)
			last_pte = I915_PPGTT_PT_ENTRIES;

		sched_pin();
		sf = sf_buf_alloc(ppgtt->pt_pages[act_pd], SFB_CPUPRIVATE);
		pt_vaddr = (uint32_t *)(uintptr_t)sf_buf_kva(sf);

		for (j = first_pte; j < last_pte; j++) {
			page_addr = page_to_phys(*pages);
			pt_vaddr[j] = gen6_pte_encode(ppgtt->dev, page_addr,
						 cache_level);

			pages++;
		}

		sf_buf_free(sf);
		sched_unpin();

		num_entries -= last_pte - first_pte;
		first_pte = 0;
		act_pd++;
	}
}

void i915_ppgtt_bind_object(struct i915_hw_ppgtt *ppgtt,
			    struct drm_i915_gem_object *obj,
			    enum i915_cache_level cache_level)
{
	i915_ppgtt_insert_pages(ppgtt,
				     obj->pages,
				     obj->gtt_space->start >> PAGE_SHIFT,
				     obj->base.size >> PAGE_SHIFT,
				     cache_level);
}

void i915_ppgtt_unbind_object(struct i915_hw_ppgtt *ppgtt,
			      struct drm_i915_gem_object *obj)
{
	i915_ppgtt_clear_range(ppgtt,
			   obj->gtt_space->start >> PAGE_SHIFT,
			   obj->base.size >> PAGE_SHIFT);
}

void i915_gem_init_ppgtt(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t pd_offset;
	struct intel_ring_buffer *ring;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	uint32_t __iomem *pd_addr;
	uint32_t pd_entry;
	int i;

	if (!dev_priv->mm.aliasing_ppgtt)
		return;


	pd_addr =  (gtt_pte_t __iomem*)dev_priv->gtt.gsm + ppgtt->pd_offset/sizeof(gtt_pte_t);
	for (i = 0; i < ppgtt->num_pd_entries; i++) {
		vm_paddr_t pt_addr;

		if (dev_priv->mm.needs_dmar)
			pt_addr = ppgtt->pt_dma_addr[i];
		else
			pt_addr = page_to_phys(ppgtt->pt_pages[i]);

		pd_entry = GEN6_PDE_ADDR_ENCODE(pt_addr);
		pd_entry |= GEN6_PDE_VALID;

		writel(pd_entry, pd_addr + i);
	}
	readl(pd_addr);

	pd_offset = ppgtt->pd_offset;
	pd_offset /= 64; /* in cachelines, */
	pd_offset <<= 16;

	if (INTEL_INFO(dev)->gen == 6) {
		uint32_t ecochk, gab_ctl, ecobits;

		ecobits = I915_READ(GAC_ECO_BITS);
		I915_WRITE(GAC_ECO_BITS, ecobits | ECOBITS_PPGTT_CACHE64B);

		gab_ctl = I915_READ(GAB_CTL);
		I915_WRITE(GAB_CTL, gab_ctl | GAB_CTL_CONT_AFTER_PAGEFAULT);

		ecochk = I915_READ(GAM_ECOCHK);
		I915_WRITE(GAM_ECOCHK, ecochk | ECOCHK_SNB_BIT |
				       ECOCHK_PPGTT_CACHE64B);
		I915_WRITE(GFX_MODE, _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));
	} else if (INTEL_INFO(dev)->gen >= 7) {
		I915_WRITE(GAM_ECOCHK, ECOCHK_PPGTT_CACHE64B);
		/* GFX_MODE is per-ring on gen7+ */
	}

	for_each_ring(ring, dev_priv, i) {
		if (INTEL_INFO(dev)->gen >= 7)
			I915_WRITE(RING_MODE_GEN7(ring),
				   _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE));

		I915_WRITE(RING_PP_DIR_DCLV(ring), PP_DIR_DCLV_2G);
		I915_WRITE(RING_PP_DIR_BASE(ring), pd_offset);
	}
}

extern int intel_iommu_gfx_mapped;
/* Certain Gen5 chipsets require require idling the GPU before
 * unmapping anything from the GTT when VT-d is enabled.
 */
static inline bool needs_idle_maps(struct drm_device *dev)
{
#ifdef CONFIG_INTEL_IOMMU
	/* Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	if (IS_GEN5(dev) && IS_MOBILE(dev) && intel_iommu_gfx_mapped)
		return true;
#endif
	return false;
}

static bool do_idling(struct drm_i915_private *dev_priv)
{
	bool ret = dev_priv->mm.interruptible;

	if (unlikely(dev_priv->gtt.do_idle_maps)) {
		dev_priv->mm.interruptible = false;
		if (i915_gpu_idle(dev_priv->dev)) {
			DRM_ERROR("Couldn't idle GPU\n");
			/* Wait a bit, in hopes it avoids the hang */
			udelay(10);
		}
	}

	return ret;
}

static void undo_idling(struct drm_i915_private *dev_priv, bool interruptible)
{
	if (unlikely(dev_priv->gtt.do_idle_maps))
		dev_priv->mm.interruptible = interruptible;
}

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* First fill our portion of the GTT with scratch pages */
	dev_priv->gtt.gtt_clear_range(dev, dev_priv->gtt.start / PAGE_SIZE,
				      dev_priv->gtt.total / PAGE_SIZE);

	list_for_each_entry(obj, &dev_priv->mm.bound_list, gtt_list) {
		i915_gem_clflush_object(obj);
		i915_gem_gtt_bind_object(obj, obj->cache_level);
	}

	i915_gem_chipset_flush(dev);
}

int i915_gem_gtt_prepare_object(struct drm_i915_gem_object *obj)
{
	if (obj->has_dma_mapping)
		return 0;

#ifdef FREEBSD_WIP
	if (!dma_map_sg(&obj->base.dev->pdev->dev,
			obj->pages->sgl, obj->pages->nents,
			PCI_DMA_BIDIRECTIONAL))
		return -ENOSPC;
#endif /* FREEBSD_WIP */

	return 0;
}

/*
 * Binds an object into the global gtt with the specified cache level. The object
 * will be accessible to the GPU via commands whose operands reference offsets
 * within the global GTT as well as accessible by the GPU through the GMADR
 * mapped BAR (dev_priv->mm.gtt->gtt).
 */
static void gen6_ggtt_insert_entries(struct drm_device *dev,
				     vm_page_t *st,
				     unsigned int first_entry,
				     enum i915_cache_level level)
{
#ifdef notyet
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct scatterlist *sg = st->sgl;
	gtt_pte_t __iomem *gtt_entries =
		(gtt_pte_t __iomem *)dev_priv->gtt.gsm + first_entry;
	int unused, i = 0;
	unsigned int len, m = 0;
	dma_addr_t addr;

	for_each_sg(st->sgl, sg, st->nents, unused) {
		len = sg_dma_len(sg) >> PAGE_SHIFT;
		for (m = 0; m < len; m++) {
			addr = sg_dma_address(sg) + (m << PAGE_SHIFT);
			iowrite32(gen6_pte_encode(dev, addr, level),
				  &gtt_entries[i]);
			i++;
		}
	}

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readl(&gtt_entries[i-1])
			!= gen6_pte_encode(dev, addr, level));

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
#endif	
}

static void gen6_ggtt_clear_range(struct drm_device *dev,
				  unsigned int first_entry,
				  unsigned int num_entries)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	gtt_pte_t scratch_pte;
	gtt_pte_t __iomem *gtt_base = (gtt_pte_t __iomem *) dev_priv->gtt.gsm + first_entry;
	const int max_entries = gtt_total_entries(dev_priv->gtt) - first_entry;
	int i;

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = gen6_pte_encode(dev, dev_priv->gtt.scratch_page_dma,
				      I915_CACHE_LLC);
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
	readl(gtt_base);
}


static void i915_ggtt_insert_entries(struct drm_device *dev,
				     vm_page_t *st,
				     unsigned int pg_start,
				     enum i915_cache_level cache_level)
{
#ifdef notyet
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gtt_insert_sg_entries(st, pg_start, flags);
#endif	

}

static void i915_ggtt_clear_range(struct drm_device *dev,
				  unsigned int first_entry,
				  unsigned int num_entries)
{
	intel_gtt_clear_range(first_entry, num_entries);
}


void i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj,
			      enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->gtt.gtt_insert_entries(dev, obj->pages,
					 obj->gtt_space->start >> PAGE_SHIFT,
					 cache_level);

	obj->has_global_gtt_mapping = 1;
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->gtt.gtt_clear_range(obj->base.dev,
				      obj->gtt_space->start >> PAGE_SHIFT,
				      obj->base.size >> PAGE_SHIFT);

	obj->has_global_gtt_mapping = 0;
}

void i915_gem_gtt_finish_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

#ifdef FREEBSD_WIP
	if (!obj->has_dma_mapping)
		dma_unmap_sg(&dev->pdev->dev,
			     obj->pages->sgl, obj->pages->nents,
			     PCI_DMA_BIDIRECTIONAL);
#endif /* FREEBSD_WIP */

	undo_idling(dev_priv, interruptible);
}

static void i915_gtt_color_adjust(struct drm_mm_node *node,
				  unsigned long color,
				  unsigned long *start,
				  unsigned long *end)
{
	if (node->color != color)
		*start += 4096;

	if (!list_empty(&node->node_list)) {
		node = list_entry(node->node_list.next,
				  struct drm_mm_node,
				  node_list);
		if (node->allocated && node->color != color)
			*end -= 4096;
	}
}

void i915_gem_setup_global_gtt(struct drm_device *dev,
			       unsigned long start,
			       unsigned long mappable_end,
			       unsigned long end)
{
	/* Let GEM Manage all of the aperture.
	 *
	 * However, leave one page at the end still bound to the scratch page.
	 * There are a number of places where the hardware apparently prefetches
	 * past the end of the object, and we've seen multiple hangs with the
	 * GPU head pointer stuck in a batchbuffer bound at the last page of the
	 * aperture.  One page should be enough to keep any prefetching inside
	 * of the aperture.
	 */
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_mm_node *entry;
	struct drm_i915_gem_object *obj;
	unsigned long hole_start, hole_end;

	BUG_ON(mappable_end > end);

	/* Subtract the guard page ... */
	drm_mm_init(&dev_priv->mm.gtt_space, start, end - start - PAGE_SIZE);
	if (!HAS_LLC(dev))
		dev_priv->mm.gtt_space.color_adjust = i915_gtt_color_adjust;

	/* Mark any preallocated objects as occupied */
	list_for_each_entry(obj, &dev_priv->mm.bound_list, gtt_list) {
		DRM_DEBUG_KMS("reserving preallocated space: %x + %zx\n",
			      obj->gtt_offset, obj->base.size);

		BUG_ON(obj->gtt_space != I915_GTT_RESERVED);
		obj->gtt_space = drm_mm_create_block(&dev_priv->mm.gtt_space,
						     obj->gtt_offset,
						     obj->base.size,
						     false);
		obj->has_global_gtt_mapping = 1;
	}

	dev_priv->gtt.start = start;
	dev_priv->gtt.total = end - start;

	/* Clear any non-preallocated blocks */
	drm_mm_for_each_hole(entry, &dev_priv->mm.gtt_space,
			     hole_start, hole_end) {
		DRM_DEBUG_KMS("clearing unused GTT space: [%lx, %lx]\n",
			      hole_start, hole_end);
		dev_priv->gtt.gtt_clear_range(dev, hole_start / PAGE_SIZE,
					      (hole_end-hole_start) / PAGE_SIZE);
	}

	/* And finally clear the reserved guard page */
	dev_priv->gtt.gtt_clear_range(dev, end / PAGE_SIZE - 1, 1);
}

static bool
intel_enable_ppgtt(struct drm_device *dev)
{
	if (i915_enable_ppgtt >= 0)
		return i915_enable_ppgtt;

#ifdef CONFIG_INTEL_IOMMU
	/* Disable ppgtt on SNB if VT-d is on. */
	if (INTEL_INFO(dev)->gen == 6 && intel_iommu_gfx_mapped)
		return false;
#endif

	return true;
}

void i915_gem_init_global_gtt(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long gtt_size, mappable_size;

	gtt_size = dev_priv->gtt.total;
	mappable_size = dev_priv->gtt.mappable_end;
#ifdef notyet
	if (intel_enable_ppgtt(dev) && HAS_ALIASING_PPGTT(dev)) {
		int ret;
		/* PPGTT pdes are stolen from global gtt ptes, so shrink the
		 * aperture accordingly when using aliasing ppgtt. */
		gtt_size -= I915_PPGTT_PD_ENTRIES*PAGE_SIZE;

		i915_gem_setup_global_gtt(dev, 0, mappable_size, gtt_size);

		ret = i915_gem_init_aliasing_ppgtt(dev);
		if (!ret)
			return;

		DRM_ERROR("Aliased PPGTT setup failed %d\n", ret);
		drm_mm_takedown(&dev_priv->mm.gtt_space);
		gtt_size += I915_PPGTT_PD_ENTRIES*PAGE_SIZE;
	}
#endif	
	i915_gem_setup_global_gtt(dev, 0, mappable_size, gtt_size);
#ifdef useold	
	/* Substract the guard page ... */
	drm_mm_init(&dev_priv->mm.gtt_space, start, end - start - PAGE_SIZE);
	if (!HAS_LLC(dev))
		dev_priv->mm.gtt_space.color_adjust = i915_gtt_color_adjust;

	dev_priv->mm.gtt_start = start;
	dev_priv->gtt.mappable_end = mappable_end;
	dev_priv->mm.gtt_end = end;
	dev_priv->mm.gtt_total = end - start;
	dev_priv->mm.mappable_gtt_total = min(end, mappable_end) - start;

	/* ... but ensure that we clear the entire range. */
	i915_ggtt_clear_range(dev, start / PAGE_SIZE, (end-start) / PAGE_SIZE);

	device_printf(dev->dev,
	    "taking over the fictitious range 0x%lx-0x%lx\n",
	    dev_priv->mm.gtt_base_addr + start,
	    dev_priv->mm.gtt_base_addr + start + dev_priv->mm.mappable_gtt_total);
	vm_phys_fictitious_reg_range(dev_priv->mm.gtt_base_addr + start,
	    dev_priv->mm.gtt_base_addr + start + dev_priv->mm.mappable_gtt_total,
	    VM_MEMATTR_WRITE_COMBINING);
#endif
}

static int setup_scratch_page(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	vm_page_t page;
	dma_addr_t dma_addr;

	page = alloc_page(GFP_KERNEL | GFP_DMA32 | __GFP_ZERO);
	if (page == NULL)
		return -ENOMEM;
	get_page(page);
	set_pages_uc(page, 1);

#ifdef CONFIG_INTEL_IOMMU
	dma_addr = pci_map_page(dev->pdev, page, 0, PAGE_SIZE,
				PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->pdev, dma_addr))
		return -EINVAL;
#else
	dma_addr = page_to_phys(page);
#endif
	dev_priv->gtt.scratch_page = page;
	dev_priv->gtt.scratch_page_dma = dma_addr;

	return 0;
}

static void teardown_scratch_page(struct drm_device *dev)
{
#ifdef CONFIG_INTEL_IOMMU /* <- Added as a marker on FreeBSD. */
	struct drm_i915_private *dev_priv = dev->dev_private;
	pci_unmap_page(dev->pdev, dev_priv->gtt.scratch_page_dma,
		       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
#endif
}

static inline unsigned int gen6_get_total_gtt_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GGMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GGMS_MASK;
	return snb_gmch_ctl << 20;
}

static inline size_t gen6_get_stolen_size(u16 snb_gmch_ctl)
{
	snb_gmch_ctl >>= SNB_GMCH_GMS_SHIFT;
	snb_gmch_ctl &= SNB_GMCH_GMS_MASK;
	return snb_gmch_ctl << 25; /* 32 MB units */
}

static inline size_t gen7_get_stolen_size(u16 snb_gmch_ctl)
{
	static const int stolen_decoder[] = {
		0, 0, 0, 0, 0, 32, 48, 64, 128, 256, 96, 160, 224, 352};
	snb_gmch_ctl >>= IVB_GMCH_GMS_SHIFT;
	snb_gmch_ctl &= IVB_GMCH_GMS_MASK;
	return stolen_decoder[snb_gmch_ctl] << 20;
}

static int gen6_gmch_probe(struct drm_device *dev,
			   size_t *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   unsigned long *mappable_end)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	phys_addr_t gtt_bus_addr;
	unsigned int gtt_size;
	u16 snb_gmch_ctl = 0;
	int ret;

#ifdef notyet
	*mappable_base = pci_resource_start(dev->pdev, 2);
	*mappable_end = pci_resource_len(dev->pdev, 2);
#else
	*mappable_base = 0;
	*mappable_end = 0;
#endif	
	/* 64/512MB is the current min/max we actually know of, but this is just
	 * a coarse sanity check.
	 */
	if ((*mappable_end < (64<<20) || (*mappable_end > (512<<20)))) {
		DRM_ERROR("Unknown GMADR size (%lx)\n",
			  dev_priv->gtt.mappable_end);
		return -ENXIO;
	}
#ifdef notyet
	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(40)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(40));
	pci_read_config_word(dev->pdev, SNB_GMCH_CTRL, &snb_gmch_ctl);
#endif	
	gtt_size = gen6_get_total_gtt_size(snb_gmch_ctl);

	if (IS_GEN7(dev))
		*stolen = gen7_get_stolen_size(snb_gmch_ctl);
	else
		*stolen = gen6_get_stolen_size(snb_gmch_ctl);

	*gtt_total = (gtt_size / sizeof(gtt_pte_t)) << PAGE_SHIFT;

	/* For GEN6+ the PTEs for the ggtt live at 2MB + BAR0 */
#ifdef notyet	
	gtt_bus_addr = pci_resource_start(dev->pdev, 0) + (2<<20);
#else
	gtt_bus_addr = 0;
#endif	
	dev_priv->gtt.gsm = ioremap_wc(gtt_bus_addr, gtt_size);
	if (!dev_priv->gtt.gsm) {
		DRM_ERROR("Failed to map the gtt page table\n");
		return -ENOMEM;
	}

	ret = setup_scratch_page(dev);
	if (ret)
		DRM_ERROR("Scratch setup failed\n");

	dev_priv->gtt.gtt_clear_range = gen6_ggtt_clear_range;
	dev_priv->gtt.gtt_insert_entries = gen6_ggtt_insert_entries;

	return ret;
}

static void gen6_gmch_remove(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	iounmap(dev_priv->gtt.gsm);
	teardown_scratch_page(dev_priv->dev);
}

static int i915_gmch_probe(struct drm_device *dev,
			   size_t *gtt_total,
			   size_t *stolen,
			   phys_addr_t *mappable_base,
			   unsigned long *mappable_end)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
#ifdef notyet
	int ret;

	ret = intel_gmch_probe(dev_priv->bridge_dev, dev_priv->dev->pdev, NULL);
	if (!ret) {
		DRM_ERROR("failed to set up gmch\n");
		return -EIO;
	}
#endif
	drm2_intel_gtt_get(gtt_total, stolen, mappable_base, mappable_end);

	dev_priv->gtt.do_idle_maps = needs_idle_maps(dev_priv->dev);
	dev_priv->gtt.gtt_clear_range = i915_ggtt_clear_range;
	dev_priv->gtt.gtt_insert_entries = i915_ggtt_insert_entries;

	return 0;
}

static void i915_gmch_remove(struct drm_device *dev)
{
#ifdef notyet
	intel_gmch_remove();
#endif
}

int i915_gem_gtt_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_gtt *gtt = &dev_priv->gtt;
	unsigned long gtt_size;
	int ret;

	
	if (INTEL_INFO(dev)->gen <= 5) {
		dev_priv->gtt.gtt_probe = i915_gmch_probe;
		dev_priv->gtt.gtt_remove = i915_gmch_remove;
	} else {
		dev_priv->gtt.gtt_probe = gen6_gmch_probe;
		dev_priv->gtt.gtt_remove = gen6_gmch_remove;
	}

	ret = dev_priv->gtt.gtt_probe(dev, &dev_priv->gtt.total,
				     &dev_priv->gtt.stolen_size,
				     &gtt->mappable_base,
				     &gtt->mappable_end);
	if (ret)
		return ret;

	gtt_size = (dev_priv->gtt.total >> PAGE_SHIFT) * sizeof(gtt_pte_t);

	/* GMADR is the PCI mmio aperture into the global GTT. */
	DRM_INFO("Memory usable by graphics device = %zdM\n",
		 dev_priv->gtt.total >> 20);
	DRM_DEBUG_DRIVER("GMADR size = %ldM\n",
			 dev_priv->gtt.mappable_end >> 20);
	DRM_DEBUG_DRIVER("GTT stolen size = %zdM\n",
			 dev_priv->gtt.stolen_size >> 20);
	return 0;
#if 0
	
	/* On modern platforms we need not worry ourself with the legacy
	 * hostbridge query stuff. Skip it entirely
	 */
	if (INTEL_INFO(dev)->gen < 6) {
#ifdef FREEBSD_WIP
		ret = intel_gmch_probe(dev_priv->bridge_dev, dev->pdev, NULL);
		if (!ret) {
			DRM_ERROR("failed to set up gmch\n");
			return -EIO;
		}
#endif /* FREEBSD_WIP */
#ifdef notyet
		dev_priv->mm.gtt = intel_gtt_get();
#endif
#ifdef FREEBSD_WIP
			intel_gmch_remove();
#endif /* FREEBSD_WIP */
			return -ENODEV;
	}


#ifdef FREEBSD_WIP
	if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(40)))
		pci_set_consistent_dma_mask(dev->pdev, DMA_BIT_MASK(40));
#endif /* FREEBSD_WIP */

#ifdef CONFIG_INTEL_IOMMU
	dev_priv->mm.needs_dmar = 1;
#endif

	/* For GEN6+ the PTEs for the ggtt live at 2MB + BAR0 */
	gtt_bus_addr = drm_get_resource_start(dev, 0) + (2<<20);
	dev_priv->gtt.gma_bus_addr = drm_get_resource_start(dev, 2);

	/* i9xx_setup */
	pci_read_config_word(dev->dev, SNB_GMCH_CTRL, &snb_gmch_ctl);
	gtt_total_entries(dev_priv->gtt) =
		gen6_get_total_gtt_size(snb_gmch_ctl) / sizeof(gtt_pte_t);
	if (INTEL_INFO(dev)->gen < 7)
		dev_priv->mm.gtt->stolen_size = gen6_get_stolen_size(snb_gmch_ctl);
	else
		dev_priv->mm.gtt->stolen_size = gen7_get_stolen_size(snb_gmch_ctl);

	dev_priv->mm.gtt->gtt_mappable_entries = drm_get_resource_len(dev, 2) >> PAGE_SHIFT;
	/* 64/512MB is the current min/max we actually know of, but this is just a
	 * coarse sanity check.
	 */
	if ((dev_priv->mm.gtt->gtt_mappable_entries >> 8) < 64 ||
	    dev_priv->mm.gtt->gtt_mappable_entries > dev_priv->mm.gtt->gtt_total_entries) {
		DRM_ERROR("Unknown GMADR entries (%d)\n",
			  dev_priv->mm.gtt->gtt_mappable_entries);
		ret = -ENXIO;
		goto err_out;
	}

	ret = setup_scratch_page(dev);
	if (ret) {
		DRM_ERROR("Scratch setup failed\n");
		goto err_out;
	}

	dev_priv->mm.gtt->gtt = pmap_mapdev_attr(gtt_bus_addr,
					   /* The size is used later by pmap_unmapdev. */
					   dev_priv->mm.gtt->gtt_total_entries * sizeof(gtt_pte_t),
					   VM_MEMATTR_WRITE_COMBINING);
	if (!dev_priv->mm.gtt->gtt) {
		DRM_ERROR("Failed to map the gtt page table\n");
		teardown_scratch_page(dev);
		ret = -ENOMEM;
		goto err_out;
	}

	/* GMADR is the PCI aperture used by SW to access tiled GFX surfaces in a linear fashion. */
	DRM_INFO("Memory usable by graphics device = %dM\n", dev_priv->mm.gtt->gtt_total_entries >> 8);
	DRM_DEBUG_DRIVER("GMADR size = %dM\n", dev_priv->mm.gtt->gtt_mappable_entries >> 8);
	DRM_DEBUG_DRIVER("GTT stolen size = %dM\n", dev_priv->mm.gtt->stolen_size >> 20);

	return 0;

err_out:
	free(dev_priv->mm.gtt, DRM_I915_GEM);
#ifdef FREEBSD_WIP
	if (INTEL_INFO(dev)->gen < 6)
		intel_gmch_remove();
#endif /* FREEBSD_WIP */
	return ret;
#endif
}
#if 0
void i915_gem_gtt_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	pmap_unmapdev((vm_offset_t)dev_priv->mm.gtt->gtt,
	    dev_priv->mm.gtt->gtt_total_entries * sizeof(gtt_pte_t));
	teardown_scratch_page(dev);
#ifdef FREEBSD_WIP
	if (INTEL_INFO(dev)->gen < 6)
		intel_gmch_remove();
#endif /* FREEBSD_WIP */
	if (INTEL_INFO(dev)->gen >= 6)
		free(dev_priv->mm.gtt, DRM_I915_GEM);
}

static void gen6_ggtt_bind_object(struct drm_i915_gem_object *obj,
				  enum i915_cache_level level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	const int first_entry = obj->gtt_space->start >> PAGE_SHIFT;
#if defined(INVARIANTS)
	const int max_entries = dev_priv->mm.gtt->gtt_total_entries - first_entry;
#endif
	gtt_pte_t __iomem *gtt_entries = dev_priv->mm.gtt->gtt + first_entry;
	int i = 0;
	vm_paddr_t addr;

	for (i = 0; i < obj->base.size >> PAGE_SHIFT; ++i) {
		addr = page_to_phys(obj->pages[i]);
		iowrite32(gen6_pte_encode(dev, addr, level), &gtt_entries[i]);
	}

	BUG_ON(i > max_entries);
	BUG_ON(i != obj->base.size / PAGE_SIZE);

	/* XXX: This serves as a posting read to make sure that the PTE has
	 * actually been updated. There is some concern that even though
	 * registers and PTEs are within the same BAR that they are potentially
	 * of NUMA access patterns. Therefore, even with the way we assume
	 * hardware should work, we must keep this posting read for paranoia.
	 */
	if (i != 0)
		WARN_ON(readl(&gtt_entries[i-1]) != gen6_pte_encode(dev, addr, level));

	/* This next bit makes the above posting read even more important. We
	 * want to flush the TLBs only after we're certain all the PTE updates
	 * have finished.
	 */
	I915_WRITE(GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	POSTING_READ(GFX_FLSH_CNTL_GEN6);
}

static void i915_ggtt_clear_range(struct drm_device *dev,
				 unsigned first_entry,
				 unsigned num_entries)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	gtt_pte_t scratch_pte;
	gtt_pte_t __iomem *gtt_base = dev_priv->mm.gtt->gtt + first_entry;
	const int max_entries = dev_priv->mm.gtt->gtt_total_entries - first_entry;
	int i;

	if (INTEL_INFO(dev)->gen < 6) {
		intel_gtt_clear_range(first_entry, num_entries);
		return;
	}

	if (WARN(num_entries > max_entries,
		 "First entry = %d; Num entries = %d (max=%d)\n",
		 first_entry, num_entries, max_entries))
		num_entries = max_entries;

	scratch_pte = gen6_pte_encode(dev, dev_priv->gtt.scratch_page_dma, I915_CACHE_LLC);
	for (i = 0; i < num_entries; i++)
		iowrite32(scratch_pte, &gtt_base[i]);
	readl(gtt_base);
}
#endif
