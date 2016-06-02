/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_DEVICE_H_
#define	_LINUX_DEVICE_H_

#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kdev_t.h>

#include <sys/bus.h>

enum irqreturn	{ IRQ_NONE = 0, IRQ_HANDLED, IRQ_WAKE_THREAD, };
typedef enum irqreturn	irqreturn_t;

struct device;

struct class {
	const char	*name;
	struct module	*owner;
	struct kobject	kobj;
	devclass_t	bsdclass;
	void		(*class_release)(struct class *class);
	void		(*dev_release)(struct device *dev);
	char *		(*devnode)(struct device *dev, umode_t *mode);
};

struct device_driver {
	const char		*name;
	struct bus_type		*bus;
	struct module		*owner;
#ifdef notyet
	const char		*mod_name;	/* used for built-in modules */

	bool suppress_bind_attrs;	/* disables bind/unbind via sysfs */

	const struct of_device_id	*of_match_table;
	const struct acpi_device_id	*acpi_match_table;
#endif
	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	int (*suspend) (struct device *dev, pm_message_t state);
	int (*resume) (struct device *dev);
	const struct attribute_group **groups;

	const struct dev_pm_ops *pm;
#ifdef notyet
	struct driver_private *p;
#endif	
};

#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)

/*
 * The type of device, "struct device" is embedded in. A class
 * or bus can contain devices of different types
 * like "partitions" and "disks", "mouse" and "event".
 * This identifies the device type and carries type-specific
 * information, equivalent to the kobj_type of a kobject.
 * If "name" is specified, the uevent will contain it in
 * the DEVTYPE variable.
 */

struct kobj_uevent_env;
struct bus_type {
	const char		*name;
	const char		*dev_name;
	struct device		*dev_root;
	struct device_attribute	*dev_attrs;	/* use dev_groups instead */
	const struct attribute_group **bus_groups;
	const struct attribute_group **dev_groups;
	const struct attribute_group **drv_groups;

	int (*match)(struct device *dev, struct device_driver *drv);
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	int (*online)(struct device *dev);
	int (*offline)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);

	const struct dev_pm_ops *pm;

};

struct device_type {
	const char *name;
	const struct attribute_group **groups;
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	char *(*devnode)(struct device *dev, umode_t *mode);
	void (*release)(struct device *dev);

	const struct dev_pm_ops *pm;
};

struct fwnode_handle;

struct device {
	struct device	*parent;
	struct list_head irqents;
	device_t	bsddev;
	dev_t		devt;
	struct class	*class;
	void		(*release)(struct device *dev);
	struct kobject	kobj;
	uint64_t	*dma_mask;
	void		*driver_data;
	unsigned int	irq;
	unsigned int	msix;
	unsigned int	msix_max;
	struct device_type *type;
	struct device_node	*of_node; /* associated device tree node */	
	struct fwnode_handle	*fwnode;
	struct device_driver *driver;	/* which driver has allocated this device */
	struct dev_pm_info	power;
	const struct attribute_group **groups;	/* optional groups */
	const char		*init_name; /* initial name of the device */
	struct bus_type	*bus;		/* type of bus device is on */

};

extern struct device linux_root_device;
extern struct kobject linux_class_root;
extern const struct kobj_type linux_dev_ktype;
extern const struct kobj_type linux_class_ktype;

struct class_attribute {
        struct attribute attr;
        ssize_t (*show)(struct class *, struct class_attribute *, char *);
        ssize_t (*store)(struct class *, struct class_attribute *, const char *, size_t);
        const void *(*namespace)(struct class *, const struct class_attribute *);
};

#define	CLASS_ATTR(_name, _mode, _show, _store)				\
	struct class_attribute class_attr_##_name =			\
	    { { #_name, NULL, _mode }, _show, _store }

struct device_attribute {
	struct attribute	attr;
	ssize_t			(*show)(struct device *,
					struct device_attribute *, char *);
	ssize_t			(*store)(struct device *,
					struct device_attribute *, const char *,
					size_t);
};

/* Simple class attribute that is just a static string */
struct class_attribute_string {
	struct class_attribute attr;
	char *str;
};

static inline ssize_t
show_class_attr_string(struct class *class,
				struct class_attribute *attr, char *buf)
{
	struct class_attribute_string *cs;
	cs = container_of(attr, struct class_attribute_string, attr);
	return snprintf(buf, PAGE_SIZE, "%s\n", cs->str);
}

/* Currently read-only only */
#define _CLASS_ATTR_STRING(_name, _mode, _str) \
	{ __ATTR(_name, _mode, show_class_attr_string, NULL), _str }
#define CLASS_ATTR_STRING(_name, _mode, _str) \
	struct class_attribute_string class_attr_##_name = \
		_CLASS_ATTR_STRING(_name, _mode, _str)

/*
 * should we create device_printf with corresponding
 * syslog priorities?
 */ 
#define	dev_err(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_warn(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_info(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_notice(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_printk(lvl, dev, fmt, ...)					\
	    device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)

static inline void *
dev_get_drvdata(const struct device *dev)
{

	return dev->driver_data;
}

static inline void
dev_set_drvdata(struct device *dev, void *data)
{

	dev->driver_data = data;
}

static inline struct device *
get_device(struct device *dev)
{

	if (dev)
		kobject_get(&dev->kobj);

	return (dev);
}

static inline char *
dev_name(const struct device *dev)
{

 	return kobject_name(&dev->kobj);
}

#define	dev_set_name(_dev, _fmt, ...)					\
	kobject_set_name(&(_dev)->kobj, (_fmt), ##__VA_ARGS__)

static inline void
put_device(struct device *dev)
{

	if (dev)
		kobject_put(&dev->kobj);
}

static inline int
class_register(struct class *class)
{

	class->bsdclass = devclass_create(class->name);
	kobject_init(&class->kobj, &linux_class_ktype);
	kobject_set_name(&class->kobj, class->name);
	kobject_add(&class->kobj, &linux_class_root, class->name);

	return (0);
}

static inline void
class_unregister(struct class *class)
{

	kobject_put(&class->kobj);
}

static inline int
device_is_registered(struct device *dev)
{
	return dev->kobj.state_in_sysfs;
}

static inline struct device *
kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}

/*
 * Devices are registered and created for exporting to sysfs. Create
 * implies register and register assumes the device fields have been
 * setup appropriately before being called.
 */
static inline void
device_initialize(struct device *dev)
{
	device_t bsddev;
	int unit;

	bsddev = NULL;
	unit = -1;
	if (dev->devt) {
		unit = MINOR(dev->devt);
		bsddev = devclass_get_device(dev->class->bsdclass, unit);
	} else if (dev->parent == NULL) {
		bsddev = devclass_get_device(dev->class->bsdclass, 0);
	}
	if (bsddev == NULL && dev->parent != NULL) {
		bsddev = device_add_child(dev->parent->bsddev,
		    dev->class->kobj.name, unit);
	}

	if (bsddev != NULL)
		device_set_softc(bsddev, dev);

	dev->bsddev = bsddev;
	MPASS(dev->bsddev != NULL);
	kobject_init(&dev->kobj, &linux_dev_ktype);
}

static inline int
device_add(struct device *dev)
{	
	if (dev->bsddev != NULL) {
		if (dev->devt == 0)
			dev->devt = makedev(0, device_get_unit(dev->bsddev));
	}
	kobject_add(&dev->kobj, &dev->class->kobj, dev_name(dev));
	return (0);
}

static inline void
device_create_release(struct device *dev)
{
	kfree(dev);
}

static inline struct device *
device_create_groups_vargs(struct class *class, struct device *parent,
    dev_t devt, void *drvdata, const struct attribute_group **groups,
    const char *fmt, va_list args)
{
	struct device *dev = NULL;
	int retval = -ENODEV;

	if (class == NULL || IS_ERR(class))
		goto error;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	dev->devt = devt;
	dev->class = class;
	dev->parent = parent;
	dev->groups = groups;
	dev->release = device_create_release;
	dev->bsddev = devclass_get_device(dev->class->bsdclass, MINOR(devt));
	device_initialize(dev);
	MPASS(dev->bsddev != NULL);
	dev_set_drvdata(dev, drvdata);

	retval = kobject_set_name_vargs(&dev->kobj, fmt, args);
	if (retval)
		goto error;

	retval = device_add(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	return ERR_PTR(retval);
}

static inline struct device *
device_create_with_groups(struct class *class,
    struct device *parent, dev_t devt, void *drvdata,
    const struct attribute_group **groups, const char *fmt, ...)
{
	va_list vargs;
	struct device *dev;

	va_start(vargs, fmt);
	dev = device_create_groups_vargs(class, parent, devt, drvdata,
	    groups, fmt, vargs);
	va_end(vargs);
	return dev;
}

static inline int
device_register(struct device *dev)
{
	device_t bsddev;
	int unit;

	bsddev = NULL;
	unit = -1;

	if (dev->bsddev != NULL)
		goto done;

	if (dev->devt) {
		unit = MINOR(dev->devt);
		bsddev = devclass_get_device(dev->class->bsdclass, unit);
	} else if (dev->parent == NULL) {
		bsddev = devclass_get_device(dev->class->bsdclass, 0);
	}
	if (bsddev == NULL && dev->parent != NULL) {
		bsddev = device_add_child(dev->parent->bsddev,
		    dev->class->kobj.name, unit);
	}
	if (bsddev != NULL) {
		if (dev->devt == 0)
			dev->devt = makedev(0, device_get_unit(bsddev));
		device_set_softc(bsddev, dev);
	}
	dev->bsddev = bsddev;
done:
	kobject_init(&dev->kobj, &linux_dev_ktype);
	kobject_add(&dev->kobj, &dev->class->kobj, dev_name(dev));

	return (0);
}

static inline void
device_unregister(struct device *dev)
{
	device_t bsddev;

	bsddev = dev->bsddev;
	dev->bsddev = NULL;

	if (bsddev != NULL) {
		mtx_lock(&Giant);
		device_delete_child(device_get_parent(bsddev), bsddev);
		mtx_unlock(&Giant);
	}
	put_device(dev);
}

static inline void
device_del(struct device *dev)
{
	device_t bsddev;

	bsddev = dev->bsddev;
	dev->bsddev = NULL;

	if (bsddev != NULL) {
		mtx_lock(&Giant);
		device_delete_child(device_get_parent(bsddev), bsddev);
		mtx_unlock(&Giant);
	}
}

struct device *device_create(struct class *class, struct device *parent,
	    dev_t devt, void *drvdata, const char *fmt, ...);


static inline void
device_destroy(struct class *class, dev_t devt)
{
	device_t bsddev;
	int unit;

	unit = MINOR(devt);
	bsddev = devclass_get_device(class->bsdclass, unit);
	if (bsddev != NULL)
		device_unregister(device_get_softc(bsddev));
}

#include <linux/idr.h>
extern struct ida *hwmon_idap;

static inline char *
strpbrk(const char *s, const char *b)
{
	const char *p;

	do {
		for (p = b; *p != '\0' && *p != *s; ++p)
			;
		if (*p != '\0')
			return ((char *)(uintptr_t)s);
	} while (*s++);

	return (NULL);
}

static struct class hwmon_class = {
	.name = "hwmon",
	.owner = THIS_MODULE,
#ifdef __linux__	
	.dev_groups = hwmon_dev_attr_groups,
	.dev_release = hwmon_dev_release,
#endif	
};

#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

struct hwmon_device {
	const char *name;
	struct device dev;
};
static inline struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				  void *drvdata,
				  const struct attribute_group **groups)
{
	struct hwmon_device *hwdev;
	int err, id;

	/* Do not accept invalid characters in hwmon name attribute */
	if (name && (!strlen(name) || strpbrk(name, "-* \t\n")))
		return ERR_PTR(-EINVAL);

	id = ida_simple_get(hwmon_idap, 0, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
	if (hwdev == NULL) {
		err = -ENOMEM;
		goto ida_remove;
	}

	hwdev->name = name;
	hwdev->dev.class = &hwmon_class;
	hwdev->dev.parent = dev;
	hwdev->dev.groups = groups;
	hwdev->dev.of_node = dev ? dev->of_node : NULL;
	dev_set_drvdata(&hwdev->dev, drvdata);
	dev_set_name(&hwdev->dev, HWMON_ID_FORMAT, id);
	err = device_register(&hwdev->dev);
	if (err)
		goto free;

	return &hwdev->dev;

free:
	kfree(hwdev);
ida_remove:
	ida_simple_remove(hwmon_idap, id);
	return ERR_PTR(err);
}


static inline
void hwmon_device_unregister(struct device *dev)
{
	int id;

	if (likely(sscanf(dev_name(dev), HWMON_ID_FORMAT, &id) == 1)) {
		device_unregister(dev);
		ida_simple_remove(hwmon_idap, id);
	} else
		device_printf(dev->bsddev,
			"hwmon_device_unregister() failed: bad class ID!\n");
}

static inline int
driver_register(struct device_driver *drv)
{
	WARN_NOT();
	return (-ENOTSUPP);
}

static inline void
driver_unregister(struct device_driver *drv)
{
	WARN_NOT();
}

static inline struct device *
bus_find_device(struct bus_type *bus, struct device *start, void *data,
		int (*match)(struct device *dev, void *data))
{
	WARN_NOT();
	return (NULL);
}

static inline int
bus_register(struct bus_type *bus)
{
	WARN_NOT();
	return (0);
}

static inline void
bus_unregister(struct bus_type *bus)
{
	WARN_NOT();
}


static inline int
device_for_each_child(struct device *dev, void *data,
		      int (*fn)(struct device *dev, void *data))
{
	WARN_NOT();
	return (0);
}

static inline void
linux_class_kfree(struct class *class)
{

	kfree(class);
}

static inline struct class *
class_create(struct module *owner, const char *name)
{
	struct class *class;
	int error;

	class = kzalloc(sizeof(*class), M_WAITOK);
	class->owner = owner;
	class->name= name;
	class->class_release = linux_class_kfree;
	error = class_register(class);
	if (error) {
		kfree(class);
		return (NULL);
	}

	return (class);
}

static inline void
class_destroy(struct class *class)
{

	if (class == NULL)
		return;
	class_unregister(class);
}

static inline int
device_create_file(struct device *dev, const struct device_attribute *attr)
{

	if (dev)
		return sysfs_create_file(&dev->kobj, &attr->attr);
	return -EINVAL;
}

static inline void
device_remove_file(struct device *dev, const struct device_attribute *attr)
{

	if (dev)
		sysfs_remove_file(&dev->kobj, &attr->attr);
}

static inline int
class_create_file(struct class *class, const struct class_attribute *attr)
{

	if (class)
		return sysfs_create_file(&class->kobj, &attr->attr);
	return -EINVAL;
}

static inline void
class_remove_file(struct class *class, const struct class_attribute *attr)
{

	if (class)
		sysfs_remove_file(&class->kobj, &attr->attr);
}

static inline int
dev_to_node(struct device *dev)
{
                return -1;
}

char *kvasprintf(gfp_t, const char *, va_list);
char *kasprintf(gfp_t, const char *, ...);

static inline void *
devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
	/*
	 * this allocates managed memory which is automatically freed
	 * on unload - we're instead just leaking for now
	 */
	DODGY();
	return (kmalloc(size, gfp));
}

static inline void *
devm_kzalloc(struct device *dev, size_t size, gfp_t gfp)
{
	return devm_kmalloc(dev, size, gfp | __GFP_ZERO);
}

#define dev_dbg(dev, format, arg...)                            \
({                                                              \
        if (0)                                                  \
                UNIMPLEMENTED();				\
})

#endif	/* _LINUX_DEVICE_H_ */
