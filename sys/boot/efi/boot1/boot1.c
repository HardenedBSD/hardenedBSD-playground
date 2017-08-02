/*-
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 * Copyright (c) 2014 Nathan Whitehorn
 * All rights reserved.
 * Copyright (c) 2015 Eric McCorkle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disk.h>
#include <machine/elf.h>
#include <machine/stdarg.h>
#include <stand.h>
#include <disk.h>

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <eficonsctl.h>
#ifdef EFI_ZFS_BOOT
#include <libzfs.h>
#endif

#include <bootstrap.h>

#include "efi_drivers.h"
#include "efizfs.h"
#include "paths.h"

#ifdef EFI_DEBUG
#define DPRINTF(fmt, args...) printf(fmt, ##args)
#define DSTALL(d) BS->Stall(d)
#else
#define DPRINTF(fmt, ...) {}
#define DSTALL(d) {}
#endif

struct arch_switch archsw;	/* MI/MD interface boundary */

static const efi_driver_t *efi_drivers[] = {
        NULL
};

extern struct console efi_console;
#if defined(__amd64__) || defined(__i386__)
extern struct console comconsole;
extern struct console nullconsole;
#endif

#ifdef EFI_ZFS_BOOT
uint64_t pool_guid;
#endif

struct fs_ops *file_system[] = {
#ifdef EFI_ZFS_BOOT
	&zfs_fsops,
#endif
	&dosfs_fsops,
#ifdef EFI_UFS_BOOT
	&ufs_fsops,
#endif
	&cd9660_fsops,
	&nfs_fsops,
	&gzipfs_fsops,
	&bzipfs_fsops,
	NULL
};

struct devsw *devsw[] = {
	&efipart_hddev,
	&efipart_fddev,
	&efipart_cddev,
#ifdef EFI_ZFS_BOOT
	&zfs_dev,
#endif
	NULL
};

struct console *consoles[] = {
	&efi_console,
	NULL
};

/* Definitions we don't actually need for boot, but we need to define
 * to make the linker happy.
 */
struct file_format *file_formats[] = { NULL };

struct netif_driver *netif_drivers[] = { NULL };

static int
efi_autoload(void)
{
  printf("******** Boot block should not call autoload\n");
  return (-1);
}

static ssize_t
efi_copyin(const void *src __unused, vm_offset_t dest __unused,
    const size_t len __unused)
{
  printf("******** Boot block should not call copyin\n");
  return (-1);
}

static ssize_t
efi_copyout(vm_offset_t src __unused, void *dest __unused,
    const size_t len __unused)
{
  printf("******** Boot block should not call copyout\n");
  return (-1);
}

static ssize_t
efi_readin(int fd __unused, vm_offset_t dest __unused,
    const size_t len __unused)
{
  printf("******** Boot block should not call readin\n");
  return (-1);
}

/* The initial number of handles used to query EFI for partitions. */
#define NUM_HANDLES_INIT	24

static EFI_GUID DevicePathGUID = DEVICE_PATH_PROTOCOL;
static EFI_GUID LoadedImageGUID = LOADED_IMAGE_PROTOCOL;

/*
 * nodes_match returns TRUE if the imgpath isn't NULL and the nodes match,
 * FALSE otherwise.
 */
static BOOLEAN
nodes_match(EFI_DEVICE_PATH *imgpath, EFI_DEVICE_PATH *devpath)
{
	int len;

	if (imgpath == NULL || imgpath->Type != devpath->Type ||
	    imgpath->SubType != devpath->SubType)
		return (FALSE);

	len = DevicePathNodeLength(imgpath);
	if (len != DevicePathNodeLength(devpath))
		return (FALSE);

	return (memcmp(imgpath, devpath, (size_t)len) == 0);
}

/*
 * device_paths_match returns TRUE if the imgpath isn't NULL and all nodes
 * in imgpath and devpath match up to their respective occurrences of a
 * media node, FALSE otherwise.
 */
static BOOLEAN
device_paths_match(EFI_DEVICE_PATH *imgpath, EFI_DEVICE_PATH *devpath)
{

	if (imgpath == NULL)
		return (FALSE);

	while (!IsDevicePathEnd(imgpath) && !IsDevicePathEnd(devpath)) {
		if (IsDevicePathType(imgpath, MEDIA_DEVICE_PATH) &&
		    IsDevicePathType(devpath, MEDIA_DEVICE_PATH))
			return (TRUE);

		if (!nodes_match(imgpath, devpath))
			return (FALSE);

		imgpath = NextDevicePathNode(imgpath);
		devpath = NextDevicePathNode(devpath);
	}

	return (FALSE);
}

/*
 * devpath_last returns the last non-path end node in devpath.
 */
static EFI_DEVICE_PATH *
devpath_last(EFI_DEVICE_PATH *devpath)
{

	while (!IsDevicePathEnd(NextDevicePathNode(devpath)))
		devpath = NextDevicePathNode(devpath);

	return (devpath);
}

#ifdef EFI_DEBUG
/*
 * devpath_node_str is a basic output method for a devpath node which
 * only understands a subset of the available sub types.
 *
 * If we switch to UEFI 2.x then we should update it to use:
 * EFI_DEVICE_PATH_TO_TEXT_PROTOCOL.
 */
static int
devpath_node_str(char *buf, size_t size, EFI_DEVICE_PATH *devpath)
{

	switch (devpath->Type) {
	case MESSAGING_DEVICE_PATH:
		switch (devpath->SubType) {
		case MSG_ATAPI_DP: {
			ATAPI_DEVICE_PATH *atapi;

			atapi = (ATAPI_DEVICE_PATH *)(void *)devpath;
			return snprintf(buf, size, "ata(%s,%s,0x%x)",
			    (atapi->PrimarySecondary == 1) ?  "Sec" : "Pri",
			    (atapi->SlaveMaster == 1) ?  "Slave" : "Master",
			    atapi->Lun);
		}
		case MSG_USB_DP: {
			USB_DEVICE_PATH *usb;

			usb = (USB_DEVICE_PATH *)devpath;
			return snprintf(buf, size, "usb(0x%02x,0x%02x)",
			    usb->ParentPortNumber, usb->InterfaceNumber);
		}
		case MSG_SCSI_DP: {
			SCSI_DEVICE_PATH *scsi;

			scsi = (SCSI_DEVICE_PATH *)(void *)devpath;
			return snprintf(buf, size, "scsi(0x%02x,0x%02x)",
			    scsi->Pun, scsi->Lun);
		}
		case MSG_SATA_DP: {
			SATA_DEVICE_PATH *sata;

			sata = (SATA_DEVICE_PATH *)(void *)devpath;
			return snprintf(buf, size, "sata(0x%x,0x%x,0x%x)",
			    sata->HBAPortNumber, sata->PortMultiplierPortNumber,
			    sata->Lun);
		}
		default:
			return snprintf(buf, size, "msg(0x%02x)",
			    devpath->SubType);
		}
		break;
	case HARDWARE_DEVICE_PATH:
		switch (devpath->SubType) {
		case HW_PCI_DP: {
			PCI_DEVICE_PATH *pci;

			pci = (PCI_DEVICE_PATH *)devpath;
			return snprintf(buf, size, "pci(0x%02x,0x%02x)",
			    pci->Device, pci->Function);
		}
		default:
			return snprintf(buf, size, "hw(0x%02x)",
			    devpath->SubType);
		}
		break;
	case ACPI_DEVICE_PATH: {
		ACPI_HID_DEVICE_PATH *acpi;

		acpi = (ACPI_HID_DEVICE_PATH *)(void *)devpath;
		if ((acpi->HID & PNP_EISA_ID_MASK) == PNP_EISA_ID_CONST) {
			switch (EISA_ID_TO_NUM(acpi->HID)) {
			case 0x0a03:
				return snprintf(buf, size, "pciroot(0x%x)",
				    acpi->UID);
			case 0x0a08:
				return snprintf(buf, size, "pcieroot(0x%x)",
				    acpi->UID);
			case 0x0604:
				return snprintf(buf, size, "floppy(0x%x)",
				    acpi->UID);
			case 0x0301:
				return snprintf(buf, size, "keyboard(0x%x)",
				    acpi->UID);
			case 0x0501:
				return snprintf(buf, size, "serial(0x%x)",
				    acpi->UID);
			case 0x0401:
				return snprintf(buf, size, "parallelport(0x%x)",
				    acpi->UID);
			default:
				return snprintf(buf, size, "acpi(pnp%04x,0x%x)",
				    EISA_ID_TO_NUM(acpi->HID), acpi->UID);
			}
		}

		return snprintf(buf, size, "acpi(0x%08x,0x%x)", acpi->HID,
		    acpi->UID);
	}
	case MEDIA_DEVICE_PATH:
		switch (devpath->SubType) {
		case MEDIA_CDROM_DP: {
			CDROM_DEVICE_PATH *cdrom;

			cdrom = (CDROM_DEVICE_PATH *)(void *)devpath;
			return snprintf(buf, size, "cdrom(%x)",
			    cdrom->BootEntry);
		}
		case MEDIA_HARDDRIVE_DP: {
			HARDDRIVE_DEVICE_PATH *hd;

			hd = (HARDDRIVE_DEVICE_PATH *)(void *)devpath;
			return snprintf(buf, size, "hd(%x)",
			    hd->PartitionNumber);
		}
		default:
			return snprintf(buf, size, "media(0x%02x)",
			    devpath->SubType);
		}
	case BBS_DEVICE_PATH:
		return snprintf(buf, size, "bbs(0x%02x)", devpath->SubType);
	case END_DEVICE_PATH_TYPE:
		return (0);
	}

	return snprintf(buf, size, "type(0x%02x, 0x%02x)", devpath->Type,
	    devpath->SubType);
}

/*
 * devpath_strlcat appends a text description of devpath to buf but not more
 * than size - 1 characters followed by NUL-terminator.
 */
static int
devpath_strlcat(char *buf, size_t size, EFI_DEVICE_PATH *devpath)
{
	size_t len, used;
	const char *sep;

	sep = "";
	used = 0;
	while (!IsDevicePathEnd(devpath)) {
		len = snprintf(buf, size - used, "%s", sep);
		used += len;
		if (used > size)
			return (used);
		buf += len;

		len = devpath_node_str(buf, size - used, devpath);
		used += len;
		if (used > size)
			return (used);
		buf += len;
		devpath = NextDevicePathNode(devpath);
		sep = ":";
	}

	return (used);
}

/*
 * devpath_str is convenience method which returns the text description of
 * devpath using a static buffer, so it isn't thread safe!
 */
static char *
devpath_str(EFI_DEVICE_PATH *devpath)
{
	static char buf[256];

	devpath_strlcat(buf, sizeof(buf), devpath);

	return (buf);
}
#endif

static EFI_STATUS
do_load(const char *filepath, void **bufp, size_t *bufsize)
{
	struct stat st;
        void *buf = NULL;
        int fd, err;
        size_t fsize, remaining;
        ssize_t readsize;

        if ((fd = open(filepath, O_RDONLY)) < 0) {
                return (ENOTSUP);
        }

        if ((err = fstat(fd, &st)) != 0) {
                goto close_file;
        }

        fsize = st.st_size;

        if ((buf = malloc(fsize)) == NULL) {
                err = ENOMEM;
                goto close_file;
        }

        remaining = fsize;

        do {
                if ((readsize = read(fd, buf, fsize)) < 0) {
                        err = (-readsize);
                        goto free_buf;
                }

                remaining -= readsize;
        } while(remaining != 0);

        close(fd);
        *bufsize = st.st_size;
        *bufp = buf;

 close_file:
        close(fd);

        return errno_to_efi_status(err);

 free_buf:
        free(buf);
        goto close_file;
}

static int
probe_fs(const char *filepath)
{
        int fd;

        if ((fd = open(filepath, O_RDONLY)) < 0) {
                return (ENOTSUP);
        }

        close(fd);

        return (0);
}

static int
probe_dev(struct devsw *dev, int unit, const char *filepath)
{
        struct devdesc currdev;
        char *devname;
        int err;

	currdev.d_dev = dev;
	currdev.d_type = currdev.d_dev->dv_type;
	currdev.d_unit = unit;
	currdev.d_opendata = NULL;
        devname = efi_fmtdev(&currdev);

        env_setenv("currdev", EV_VOLATILE, devname, efi_setcurrdev,
            env_nounset);

        err = probe_fs(filepath);

        return (err);
}

static int
load_preferred(EFI_LOADED_IMAGE *img, const char *filepath, void **bufp,
    size_t *bufsize, EFI_HANDLE *handlep)
{
	pdinfo_list_t *pdi_list;
	pdinfo_t *dp, *pp;
	EFI_DEVICE_PATH *devpath, *copy;
	EFI_HANDLE h;
	struct devsw *dev;
	int unit;
	uint64_t extra;
	char *devname;

#ifdef EFI_ZFS_BOOT
	/* Did efi_zfs_probe() detect the boot pool? */
	if (pool_guid != 0) {
                struct zfs_devdesc currdev;

		currdev.d_dev = &zfs_dev;
		currdev.d_unit = 0;
		currdev.d_type = currdev.d_dev->dv_type;
		currdev.d_opendata = NULL;
		currdev.pool_guid = pool_guid;
		currdev.root_guid = 0;
		devname = efi_fmtdev(&currdev);

		env_setenv("currdev", EV_VOLATILE, devname, efi_setcurrdev,
		    env_nounset);

                if (probe_fs(filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = img->DeviceHandle;
                        return (0);
                }
	}
#endif /* EFI_ZFS_BOOT */

	/* We have device lists for hd, cd, fd, walk them all. */
	pdi_list = efiblk_get_pdinfo_list(&efipart_hddev);
	STAILQ_FOREACH(dp, pdi_list, pd_link) {
                struct disk_devdesc currdev;

		currdev.d_dev = &efipart_hddev;
		currdev.d_type = currdev.d_dev->dv_type;
		currdev.d_unit = dp->pd_unit;
		currdev.d_opendata = NULL;
		currdev.d_slice = -1;
		currdev.d_partition = -1;
		devname = efi_fmtdev(&currdev);

		env_setenv("currdev", EV_VOLATILE, devname, efi_setcurrdev,
		    env_nounset);

	        if (dp->pd_handle == img->DeviceHandle &&
                    probe_fs(filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = img->DeviceHandle;
                        return (0);
		}

                /* Assuming GPT partitioning. */
		STAILQ_FOREACH(pp, &dp->pd_part, pd_link) {
			if (pp->pd_handle == img->DeviceHandle) {
				currdev.d_slice = pp->pd_unit;
				currdev.d_partition = 255;
                                devname = efi_fmtdev(&currdev);

                                env_setenv("currdev", EV_VOLATILE, devname,
                                    efi_setcurrdev, env_nounset);

                                if (probe_fs(filepath) == 0 &&
                                    do_load(filepath, bufp, bufsize) ==
                                        EFI_SUCCESS) {
                                        *handlep = img->DeviceHandle;
                                        return (0);
                                }
			}
		}
	}

	pdi_list = efiblk_get_pdinfo_list(&efipart_cddev);
	STAILQ_FOREACH(dp, pdi_list, pd_link) {
                if ((dp->pd_handle == img->DeviceHandle ||
		     dp->pd_alias == img->DeviceHandle) &&
                    probe_dev(&efipart_cddev, dp->pd_unit, filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = img->DeviceHandle;
                        return (0);
		}
	}

	pdi_list = efiblk_get_pdinfo_list(&efipart_fddev);
	STAILQ_FOREACH(dp, pdi_list, pd_link) {
        	if (dp->pd_handle == img->DeviceHandle &&
                    probe_dev(&efipart_cddev, dp->pd_unit, filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = img->DeviceHandle;
                        return (0);
		}
	}

	/*
	 * Try the device handle from our loaded image first.  If that
	 * fails, use the device path from the loaded image and see if
	 * any of the nodes in that path match one of the enumerated
	 * handles.
	 */
	if (efi_handle_lookup(img->DeviceHandle, &dev, &unit, &extra) == 0 &&
            probe_dev(dev, dp->pd_unit, filepath) == 0 &&
            do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                *handlep = img->DeviceHandle;
                return (0);
	}

	copy = NULL;
	devpath = efi_lookup_image_devpath(IH);
	while (devpath != NULL) {
		h = efi_devpath_handle(devpath);
		if (h == NULL)
			break;

		free(copy);
		copy = NULL;

		if (efi_handle_lookup(h, &dev, &unit, &extra) == 0 &&
                    probe_dev(dev, dp->pd_unit, filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = img->DeviceHandle;
                        return (0);
		}

		devpath = efi_lookup_devpath(h);
		if (devpath != NULL) {
			copy = efi_devpath_trim(devpath);
			devpath = copy;
		}
	}
	free(copy);

	return (ENOENT);
}

static int
load_all(const char *filepath, void **bufp, size_t *bufsize,
    EFI_HANDLE *handlep)
{
	pdinfo_list_t *pdi_list;
	pdinfo_t *dp, *pp;
	zfsinfo_list_t *zfsi_list;
	zfsinfo_t *zi;
        char *devname;

#ifdef EFI_ZFS_BOOT
	zfsi_list = efizfs_get_zfsinfo_list();
	STAILQ_FOREACH(zi, zfsi_list, zi_link) {
                struct zfs_devdesc currdev;

		currdev.d_dev = &zfs_dev;
		currdev.d_unit = 0;
		currdev.d_type = currdev.d_dev->dv_type;
		currdev.d_opendata = NULL;
		currdev.pool_guid = zi->zi_pool_guid;
		currdev.root_guid = 0;
		devname = efi_fmtdev(&currdev);

		env_setenv("currdev", EV_VOLATILE, devname, efi_setcurrdev,
		    env_nounset);

                if (probe_fs(filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = zi->zi_handle;
                        printf("Succeeded\n");

                        return (0);
                }
	}
#endif /* EFI_ZFS_BOOT */

	/* We have device lists for hd, cd, fd, walk them all. */
	pdi_list = efiblk_get_pdinfo_list(&efipart_hddev);
	STAILQ_FOREACH(dp, pdi_list, pd_link) {
                struct disk_devdesc currdev;

		currdev.d_dev = &efipart_hddev;
		currdev.d_type = currdev.d_dev->dv_type;
		currdev.d_unit = dp->pd_unit;
		currdev.d_opendata = NULL;
		currdev.d_slice = -1;
		currdev.d_partition = -1;
		devname = efi_fmtdev(&currdev);

		env_setenv("currdev", EV_VOLATILE, devname, efi_setcurrdev,
		    env_nounset);

		if (probe_fs(filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = dp->pd_handle;

                        return (0);
		}

                /* Assuming GPT partitioning. */
		STAILQ_FOREACH(pp, &dp->pd_part, pd_link) {
                        currdev.d_slice = pp->pd_unit;
                        currdev.d_partition = 255;
                        devname = efi_fmtdev(&currdev);

                        env_setenv("currdev", EV_VOLATILE, devname,
                            efi_setcurrdev, env_nounset);

                        if (probe_fs(filepath) == 0 &&
                            do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                                *handlep = pp->pd_handle;

                                return (0);
			}
		}
	}

	pdi_list = efiblk_get_pdinfo_list(&efipart_cddev);
	STAILQ_FOREACH(dp, pdi_list, pd_link) {
		if (probe_dev(&efipart_cddev, dp->pd_unit, filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = dp->pd_handle;

                        return (0);
		}
	}

	pdi_list = efiblk_get_pdinfo_list(&efipart_fddev);
	STAILQ_FOREACH(dp, pdi_list, pd_link) {
		if (probe_dev(&efipart_fddev, dp->pd_unit, filepath) == 0 &&
                    do_load(filepath, bufp, bufsize) == EFI_SUCCESS) {
                        *handlep = dp->pd_handle;

                        return (0);
		}
	}

	return (ENOENT);
}

static EFI_STATUS
load_loader(EFI_HANDLE *handlep, void **bufp, size_t *bufsize)
{
	EFI_LOADED_IMAGE *boot_image;
        EFI_STATUS status;

	if ((status = BS->OpenProtocol(IH, &LoadedImageGUID,
            (VOID**)&boot_image, IH, NULL,
            EFI_OPEN_PROTOCOL_GET_PROTOCOL)) != EFI_SUCCESS) {
		panic("Failed to query LoadedImage (%lu)\n",
		    EFI_ERROR_CODE(status));
	}

        /* Try the preferred handles first, then all the handles */
        if (load_preferred(boot_image, PATH_LOADER_EFI, bufp, bufsize,
                handlep) == 0) {
                return (0);
        }

        if (load_all(PATH_LOADER_EFI, bufp, bufsize, handlep) == 0) {
                return (0);
        }

	return (EFI_NOT_FOUND);
}

/*
 * try_boot only returns if it fails to load the loader. If it succeeds
 * it simply boots, otherwise it returns the status of last EFI call.
 */
static EFI_STATUS
try_boot(void)
{
	size_t bufsize, loadersize, cmdsize;
	void *buf, *loaderbuf;
	char *cmd;
        EFI_HANDLE fshandle;
	EFI_HANDLE loaderhandle;
	EFI_LOADED_IMAGE *loaded_image;
	EFI_STATUS status;
        EFI_DEVICE_PATH *fspath;

	status = load_loader(&fshandle, &loaderbuf, &loadersize);

        if (status != EFI_SUCCESS) {
                return (status);
        }

	fspath = NULL;
	if (status == EFI_SUCCESS) {
		status = BS->OpenProtocol(fshandle, &DevicePathGUID,
                    (void **)&fspath, IH, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (status != EFI_SUCCESS) {
			DPRINTF("Failed to get image DevicePath (%lu)\n",
			    EFI_ERROR_CODE(status));
                }
		DPRINTF("filesystem device path: %s\n", devpath_str(fspath));
	}

	/*
	 * Read in and parse the command line from /boot.config or /boot/config,
	 * if present. We'll pass it the next stage via a simple ASCII
	 * string. loader.efi has a hack for ASCII strings, so we'll use that to
	 * keep the size down here. We only try to read the alternate file if
	 * we get EFI_NOT_FOUND because all other errors mean that the boot_module
	 * had troubles with the filesystem. We could return early, but we'll let
	 * loading the actual kernel sort all that out. Since these files are
	 * optional, we don't report errors in trying to read them.
	 */
	cmd = NULL;
	cmdsize = 0;
	status = do_load(PATH_DOTCONFIG, &buf, &bufsize);
	if (status == EFI_NOT_FOUND)
		status = do_load(PATH_CONFIG, &buf, &bufsize);
	if (status == EFI_SUCCESS) {
		cmdsize = bufsize + 1;
		cmd = malloc(cmdsize);
		if (cmd == NULL)
			goto errout;
		memcpy(cmd, buf, bufsize);
		cmd[bufsize] = '\0';
		free(buf);
		buf = NULL;
	}

	if ((status = BS->LoadImage(TRUE, IH, devpath_last(fspath),
	    loaderbuf, loadersize, &loaderhandle)) != EFI_SUCCESS) {
		printf("Failed to load image, size: %zu, (%lu)\n",
		     loadersize, EFI_ERROR_CODE(status));
		goto errout;
	}

	if ((status = BS->OpenProtocol(loaderhandle, &LoadedImageGUID,
            (VOID**)&loaded_image, IH, NULL,
            EFI_OPEN_PROTOCOL_GET_PROTOCOL)) != EFI_SUCCESS) {
		printf("Failed to query LoadedImage (%lu)\n",
		    EFI_ERROR_CODE(status));
		goto errout;
	}

	if (cmd != NULL)
		printf("    command args: %s\n", cmd);

	loaded_image->DeviceHandle = fshandle;
	loaded_image->LoadOptionsSize = cmdsize;
	loaded_image->LoadOptions = cmd;

	DPRINTF("Starting '%s' in 5 seconds...", PATH_LOADER_EFI);
	DSTALL(1000000);
	DPRINTF(".");
	DSTALL(1000000);
	DPRINTF(".");
	DSTALL(1000000);
	DPRINTF(".");
	DSTALL(1000000);
	DPRINTF(".");
	DSTALL(1000000);
	DPRINTF(".\n");

	if ((status = BS->StartImage(loaderhandle, NULL, NULL)) !=
	    EFI_SUCCESS) {
		printf("Failed to start image (%lu)\n",
		    EFI_ERROR_CODE(status));
		loaded_image->LoadOptionsSize = 0;
		loaded_image->LoadOptions = NULL;
	}

errout:
	if (cmd != NULL)
		free(cmd);
	if (buf != NULL)
		free(buf);
	if (loaderbuf != NULL)
		free(loaderbuf);

	return (status);
}

EFI_STATUS
main(int argc __unused, CHAR16 *argv[] __unused)
{
        EFI_STATUS status;

        SIMPLE_TEXT_OUTPUT_INTERFACE *conout = NULL;
        UINTN i, max_dim, best_mode, cols, rows;

	archsw.arch_autoload = efi_autoload;
	archsw.arch_getdev = efi_getdev;
	archsw.arch_copyin = efi_copyin;
	archsw.arch_copyout = efi_copyout;
	archsw.arch_readin = efi_readin;
#ifdef EFI_ZFS_BOOT
        /* Note this needs to be set before ZFS init. */
        archsw.arch_zfs_probe = efi_zfs_probe;
#endif

	/* Init the time source */
	efi_time_init();

        cons_probe();

	/*
	 * Reset the console and find the best text mode.
	 */
	conout = ST->ConOut;
	conout->Reset(conout, TRUE);
	max_dim = best_mode = 0;
	for (i = 0; ; i++) {
		status = conout->QueryMode(conout, i, &cols, &rows);
		if (EFI_ERROR(status))
			break;
		if (cols * rows > max_dim) {
			max_dim = cols * rows;
			best_mode = i;
		}
	}
	if (max_dim > 0)
		conout->SetMode(conout, best_mode);
	conout->EnableCursor(conout, TRUE);
	conout->ClearScreen(conout);

	/*
	 * Initialise the block cache. Set the upper limit.
	 */
	bcache_init(32768, 512);

	printf("\n>> FreeBSD EFI boot block\n");

	archsw.arch_autoload = efi_autoload;
	archsw.arch_getdev = efi_getdev;
	archsw.arch_copyin = efi_copyin;
	archsw.arch_copyout = efi_copyout;
	archsw.arch_readin = efi_readin;

	printf("   Loader path: %s\n\n", PATH_LOADER_EFI);
	printf("   Initializing modules:");

	for (i = 0; efi_drivers[i] != NULL; i++) {
		printf(" %s", efi_drivers[i]->name);
		if (efi_drivers[i]->init != NULL)
			efi_drivers[i]->init();
	}

	for (i = 0; devsw[i] != NULL; i++) {
                if (devsw[i]->dv_init != NULL) {
                        printf(" %s", devsw[i]->dv_name);
			(devsw[i]->dv_init)();
                }
        }

	putchar('\n');

	try_boot();

	/* If we get here, we're out of luck... */
	panic("No bootable partitions found!");
}
