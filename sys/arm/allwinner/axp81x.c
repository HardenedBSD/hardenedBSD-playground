/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * X-Powers AXP813/818 PMU for Allwinner SoCs
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "iicbus_if.h"

#define	AXP_ICTYPE		0x03
#define	AXP_POWERBAT		0x32
#define	 AXP_POWERBAT_SHUTDOWN	(1 << 7)

static struct ofw_compat_data compat_data[] = {
	{ "x-powers,axp813",			1 },
	{ "x-powers,axp818",			1 },
	{ NULL,					0 }
};

struct axp81x_softc {
	uint16_t		addr;
	struct intr_config_hook	enum_hook;
};

static int
axp81x_read(device_t dev, uint8_t reg, uint8_t *data, uint8_t size)
{
	struct axp81x_softc *sc;
	struct iic_msg msg[2];

	sc = device_get_softc(dev);

	msg[0].slave = sc->addr;
	msg[0].flags = IIC_M_WR;
	msg[0].len = 1;
	msg[0].buf = &reg;

	msg[1].slave = sc->addr;
	msg[1].flags = IIC_M_RD;
	msg[1].len = size;
	msg[1].buf = data;

	return (iicbus_transfer(dev, msg, 2));
}

static int
axp81x_write(device_t dev, uint8_t reg, uint8_t val)
{
	struct axp81x_softc *sc;
	struct iic_msg msg[2];

	sc = device_get_softc(dev);

	msg[0].slave = sc->addr;
	msg[0].flags = IIC_M_WR;
	msg[0].len = 1;
	msg[0].buf = &reg;

	msg[1].slave = sc->addr;
	msg[1].flags = IIC_M_WR;
	msg[1].len = 1;
	msg[1].buf = &val;

	return (iicbus_transfer(dev, msg, 2));
}

static void
axp81x_shutdown(void *devp, int howto)
{
	device_t dev;

	if ((howto & RB_POWEROFF) == 0)
		return;

	dev = devp;

	if (bootverbose)
		device_printf(dev, "Shutdown AXP81x\n");

	axp81x_write(dev, AXP_POWERBAT, AXP_POWERBAT_SHUTDOWN);
}

static int
axp81x_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "X-Powers AXP81x Power Management Unit");

	return (BUS_PROBE_DEFAULT);
}

static int
axp81x_attach(device_t dev)
{
	struct axp81x_softc *sc;
	uint8_t chip_id;

	sc = device_get_softc(dev);

	sc->addr = iicbus_get_addr(dev);

	if (bootverbose) {
		axp81x_read(dev, AXP_ICTYPE, &chip_id, 1);
		device_printf(dev, "chip ID 0x%02x\n", chip_id);
	}

	EVENTHANDLER_REGISTER(shutdown_final, axp81x_shutdown, dev,
	    SHUTDOWN_PRI_LAST);

	return (0);
}

static device_method_t axp81x_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		axp81x_probe),
	DEVMETHOD(device_attach,	axp81x_attach),

	DEVMETHOD_END
};

static driver_t axp81x_driver = {
	"axp81x_pmu",
	axp81x_methods,
	sizeof(struct axp81x_softc),
};

static devclass_t axp81x_devclass;

DRIVER_MODULE(axp81x, iicbus, axp81x_driver, axp81x_devclass, 0, 0);
MODULE_VERSION(axp81x, 1);
MODULE_DEPEND(axp81x, iicbus, 1, 1, 1);
