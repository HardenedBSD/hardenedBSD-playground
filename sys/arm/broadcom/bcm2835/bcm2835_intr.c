/*-
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
 * All rights reserved.
 *
 * Based on OMAP3 INTC code by Ben Gray
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_soc.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#if defined(SOC_BCM2836) || defined(SOC_BRCM_BCM2837)
#include <arm/broadcom/bcm2835/bcm2836.h>
#endif

#ifdef __aarch64__
#include "pic_if.h"
#endif

#define	INTC_PENDING_BASIC	0x00
#define	INTC_PENDING_BANK1	0x04
#define	INTC_PENDING_BANK2	0x08
#define	INTC_FIQ_CONTROL	0x0C
#define	INTC_ENABLE_BANK1	0x10
#define	INTC_ENABLE_BANK2	0x14
#define	INTC_ENABLE_BASIC	0x18
#define	INTC_DISABLE_BANK1	0x1C
#define	INTC_DISABLE_BANK2	0x20
#define	INTC_DISABLE_BASIC	0x24

#define	BANK1_START	8
#define	BANK1_END	(BANK1_START + 32 - 1)
#define	BANK2_START	(BANK1_START + 32)
#define	BANK2_END	(BANK2_START + 32 - 1)
#define	BANK3_START	(BANK2_START + 32)
#define	BANK3_END	(BANK3_START + 32 - 1)

#define	IS_IRQ_BASIC(n)	(((n) >= 0) && ((n) < BANK1_START))
#define	IS_IRQ_BANK1(n)	(((n) >= BANK1_START) && ((n) <= BANK1_END))
#define	IS_IRQ_BANK2(n)	(((n) >= BANK2_START) && ((n) <= BANK2_END))
#define	ID_IRQ_BCM2836(n) (((n) >= BANK3_START) && ((n) <= BANK3_END))
#define	IRQ_BANK1(n)	((n) - BANK1_START)
#define	IRQ_BANK2(n)	((n) - BANK2_START)

#ifdef  DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...)
#endif

struct bcm_intc_softc {
	device_t		sc_dev;
	struct resource *	intc_res;
	bus_space_tag_t		intc_bst;
	bus_space_handle_t	intc_bsh;
};

static struct bcm_intc_softc *bcm_intc_sc = NULL;

#define	intc_read_4(_sc, reg)		\
    bus_space_read_4((_sc)->intc_bst, (_sc)->intc_bsh, (reg))
#define	intc_write_4(_sc, reg, val)		\
    bus_space_write_4((_sc)->intc_bst, (_sc)->intc_bsh, (reg), (val))

static int bcm_get_next_irq(int);
static void bcm_mask_irq(uintptr_t);
static void bcm_unmask_irq(uintptr_t);

static int
bcm_intc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "brcm,bcm2708-armctrl-ic"))
		return (ENXIO);
	device_set_desc(dev, "BCM2835 Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_intc_attach(device_t dev)
{
	struct		bcm_intc_softc *sc = device_get_softc(dev);
	int		rid = 0;

	sc->sc_dev = dev;

	if (bcm_intc_sc)
		return (ENXIO);

	sc->intc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->intc_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->intc_bst = rman_get_bustag(sc->intc_res);
	sc->intc_bsh = rman_get_bushandle(sc->intc_res);

	bcm_intc_sc = sc;

#ifdef __aarch64__
	arm_register_root_pic(dev, BANK3_END);
#endif

	return (0);
}

#ifdef __aarch64__
static void
bcm_intc_dispatch(device_t dev, struct trapframe *frame)
{
	int irq;

	irq = -1;
	while (1) {
		irq = bcm_get_next_irq(irq);
		if (irq < 0)
			return;

		arm_dispatch_intr(irq, frame);
	}
}

static void
bcm_intc_eoi(device_t dev, u_int irq)
{
}

static void
bcm_intc_mask_irq(device_t dev, u_int irq)
{

	bcm_mask_irq(irq);
}

static void
bcm_intc_unmask_irq(device_t dev, u_int irq)
{

	bcm_unmask_irq(irq);
}
#endif

static device_method_t bcm_intc_methods[] = {
	DEVMETHOD(device_probe,		bcm_intc_probe),
	DEVMETHOD(device_attach,	bcm_intc_attach),

#ifdef __aarch64__
	/* pic_if */
	DEVMETHOD(pic_dispatch,		bcm_intc_dispatch),
	DEVMETHOD(pic_eoi,		bcm_intc_eoi),
	DEVMETHOD(pic_mask,		bcm_intc_mask_irq),
	DEVMETHOD(pic_unmask,		bcm_intc_unmask_irq),
#endif

	{ 0, 0 }
};

static driver_t bcm_intc_driver = {
	"intc",
	bcm_intc_methods,
	sizeof(struct bcm_intc_softc),
};

static devclass_t bcm_intc_devclass;

EARLY_DRIVER_MODULE(intc, simplebus, bcm_intc_driver, bcm_intc_devclass, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);

static int
bcm_get_next_irq(int last_irq)
{
	struct bcm_intc_softc *sc = bcm_intc_sc;
	uint32_t pending;
	int32_t irq = last_irq + 1;
#if defined(SOC_BCM2836) || defined(SOC_BRCM_BCM2837)
	int ret;
#endif

	/* Sanity check */
	if (irq < 0)
		irq = 0;

#if defined(SOC_BCM2836) || defined(SOC_BRCM_BCM2837)
	if ((ret = bcm2836_get_next_irq(irq)) < 0)
		return (-1);
	if (ret != BCM2836_GPU_IRQ)
		return (ret + BANK3_START);
#endif

	/* TODO: should we mask last_irq? */
	if (irq < BANK1_START) {
		pending = intc_read_4(sc, INTC_PENDING_BASIC);
		if ((pending & 0xFF) == 0) {
			irq  = BANK1_START;	/* skip to next bank */
		} else do {
			if (pending & (1 << irq))
				return irq;
			irq++;
		} while (irq < BANK1_START);
	}
	if (irq < BANK2_START) {
		pending = intc_read_4(sc, INTC_PENDING_BANK1);
		if (pending == 0) {
			irq  = BANK2_START;	/* skip to next bank */
		} else do {
			if (pending & (1 << IRQ_BANK1(irq)))
				return irq;
			irq++;
		} while (irq < BANK2_START);
	}
	if (irq < BANK3_START) {
		pending = intc_read_4(sc, INTC_PENDING_BANK2);
		if (pending != 0) do {
			if (pending & (1 << IRQ_BANK2(irq)))
				return irq;
			irq++;
		} while (irq < BANK3_START);
	}
	return (-1);
}

static void
bcm_mask_irq(uintptr_t nb)
{
	struct bcm_intc_softc *sc = bcm_intc_sc;
	dprintf("%s: %lu\n", __func__, nb);

	if (IS_IRQ_BASIC(nb))
		intc_write_4(sc, INTC_DISABLE_BASIC, (1 << nb));
	else if (IS_IRQ_BANK1(nb))
		intc_write_4(sc, INTC_DISABLE_BANK1, (1 << IRQ_BANK1(nb)));
	else if (IS_IRQ_BANK2(nb))
		intc_write_4(sc, INTC_DISABLE_BANK2, (1 << IRQ_BANK2(nb)));
#if defined(SOC_BCM2836) || defined(SOC_BRCM_BCM2837)
	else if (ID_IRQ_BCM2836(nb))
		bcm2836_mask_irq(nb - BANK3_START);
#endif
	else
		printf("arm_mask_irq: Invalid IRQ number: %lu\n", nb);
}

static void
bcm_unmask_irq(uintptr_t nb)
{
	struct bcm_intc_softc *sc = bcm_intc_sc;
	dprintf("%s: %lu\n", __func__, nb);

	if (IS_IRQ_BASIC(nb))
		intc_write_4(sc, INTC_ENABLE_BASIC, (1 << nb));
	else if (IS_IRQ_BANK1(nb))
		intc_write_4(sc, INTC_ENABLE_BANK1, (1 << IRQ_BANK1(nb)));
	else if (IS_IRQ_BANK2(nb))
		intc_write_4(sc, INTC_ENABLE_BANK2, (1 << IRQ_BANK2(nb)));
#if defined(SOC_BCM2836) || defined(SOC_BRCM_BCM2837)
	else if (ID_IRQ_BCM2836(nb))
		bcm2836_unmask_irq(nb - BANK3_START);
#endif
	else
		printf("arm_mask_irq: Invalid IRQ number: %lu\n", nb);
}

#if defined(__arm__)
#ifdef SMP
void
intr_pic_init_secondary(void)
{
}
#endif
#endif /* __arm__ */
