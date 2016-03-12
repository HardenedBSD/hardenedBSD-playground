/*-
 * Copyright (c) 2016 Andrew Turner
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker.h>

#include <machine/elf.h>
#include <machine/metadata.h>
#include <machine/vmparam.h>

#include <stand.h>
#include <bootstrap.h>

#include <fdt.h>
#include <libfdt.h>

extern char end[];
extern struct console pl011_console;

struct console *consoles[] = {
	&pl011_console,
	NULL
};

/*
 * Copy module-related data into the load area, where it can be
 * used as a directory for loaded modules.
 *
 * Module data is presented in a self-describing format.  Each datum
 * is preceded by a 32-bit identifier and a 32-bit size field.
 *
 * Currently, the following data are saved:
 *
 * MOD_NAME	(variable)		module name (string)
 * MOD_TYPE	(variable)		module type (string)
 * MOD_ARGS	(variable)		module parameters (string)
 * MOD_ADDR	sizeof(vm_offset_t)	module load address
 * MOD_SIZE	sizeof(size_t)		module size
 * MOD_METADATA	(variable)		type-specific metadata
 */
#define COPY32(v, a, c) {				\
	uint32_t *x = (uint32_t *)(a);			\
	if (c)						\
		*x = (v);				\
	a += sizeof(*x);				\
}

#define MOD_STR(t, a, s, c) {				\
	COPY32(t, a, c);				\
	COPY32(strlen(s) + 1, a, c);			\
	if (c)						\
		memcpy((void *)a, s, strlen(s) + 1);	\
	a += roundup(strlen(s) + 1, sizeof(u_long));	\
}

#define MOD_NAME(a, s, c)	MOD_STR(MODINFO_NAME, a, s, c)
#define MOD_TYPE(a, s, c)	MOD_STR(MODINFO_TYPE, a, s, c)
#define MOD_ARGS(a, s, c)	MOD_STR(MODINFO_ARGS, a, s, c)

#define MOD_VAR(t, a, s, c) {				\
	COPY32(t, a, c);				\
	COPY32(sizeof(s), a, c);			\
	if (c)						\
		memcpy((void *)a, &s, sizeof(s));	\
	a += roundup(sizeof(s), sizeof(u_long));	\
}

#define MOD_ADDR(a, s, c)	MOD_VAR(MODINFO_ADDR, a, s, c)
#define MOD_SIZE(a, s, c)	MOD_VAR(MODINFO_SIZE, a, s, c)

#define MOD_METADATA(a, t, d, c) {			\
	COPY32(MODINFO_METADATA | t, a, c);		\
	COPY32(sizeof(d), a, c);			\
	if (c)						\
		memcpy((void *)a, &(d), sizeof(d));	\
	a += roundup(sizeof(d), sizeof(u_long));	\
}

#define MOD_END(a, c) {					\
	COPY32(MODINFO_END, a, c);			\
	COPY32(0, a, c);				\
}

static vm_offset_t
bi_copymodules(vm_offset_t addr, struct preloaded_file *fp, int howto,
    void *envp, vm_offset_t dtbp, vm_offset_t kernend)
{
	int copy;

	copy = addr != 0;

	MOD_NAME(addr, "/boot/kernel/kernel", copy);
	MOD_TYPE(addr, "elf kernel", copy);
	MOD_ADDR(addr, fp->f_addr, copy);
	MOD_SIZE(addr, fp->f_size, copy);

	MOD_METADATA(addr, MODINFOMD_HOWTO, howto, copy);
	MOD_METADATA(addr, MODINFOMD_ENVP, envp, copy);
	MOD_METADATA(addr, MODINFOMD_DTBP, dtbp, copy);
	MOD_METADATA(addr, MODINFOMD_KERNEND, kernend, copy);

	MOD_END(addr, copy);

	return (addr);
}

static void
exec(void *base, size_t length, struct fdt_header *dtb)
{
	struct preloaded_file kfp;
	Elf_Ehdr *e;
	void (*entry)(void *);
	vm_offset_t kern_base;
	vm_offset_t addr, size;
	vm_offset_t dtbp, kernend;
	void *envp;
	int howto;

	printf("Booting kernel at %p, size %zu\n", base, length);

	kern_base = (vm_offset_t)base;

	bzero(&kfp, sizeof(kfp));
	kfp.f_addr = kern_base;
	kfp.f_size = length + 0xf1000;

	howto = 0;
	envp = NULL;

	addr = roundup2(kfp.f_addr + kfp.f_size, PAGE_SIZE);
	memcpy((void *)addr, dtb, fdt_totalsize(dtb));
	dtbp = addr - kern_base + VM_MIN_KERNEL_ADDRESS;
	printf("DTB: %#lx %#lx\n", addr, dtbp);
	addr += roundup2(fdt_totalsize(dtb), PAGE_SIZE);

	size = bi_copymodules(0, &kfp, 0, NULL, dtbp, 0);
	kernend = roundup(addr + size, PAGE_SIZE);
	kernend = kernend - kern_base + VM_MIN_KERNEL_ADDRESS;
	(void)bi_copymodules(addr, &kfp, 0, NULL, dtbp, kernend);

	e = base;
	entry = (void *)((u_long)e->e_entry + kern_base -
	    VM_MIN_KERNEL_ADDRESS);
	addr = addr - kern_base + VM_MIN_KERNEL_ADDRESS;

	printf("entry: %p %x %lx\n", entry, *(uint32_t *)entry, addr);
	entry((void *)addr);
}

void pl011_cons_putchar(int c);
void putch(int);

int
arm64_main(void *fdt_addr)
{
	struct fdt_header *header;
	uintptr_t loader_end;
	int o;

	/* Only needed for printf debugging to work */
#if 1
	/*
	 * Initialise the heap as early as possible.  Once this is done,
	 * alloc() is usable. The stack is buried inside us, so this is safe.
	 */
	loader_end = roundup2((uintptr_t)end, PAGE_SIZE);
	setheap((void *)loader_end, (void *)(loader_end + 512 * 1024));

	/*
	 * Set up console.
	 */
	cons_probe();
	printf("FreeBSD Raspberry Pi 3 loader\n");
#endif

	/* Search for the kernel in the fdt data */
	o = fdt_path_offset(fdt_addr, "/chosen");
	if (o >= 0) {
		const struct fdt_property *prop;
		uint32_t len, tag;
		int depth, next;
		const char *name;
		void *boot_addr = NULL;
		void *boot_end = NULL;

		depth = 0;
		do {
			tag = fdt_next_tag(fdt_addr, o, &next);
			switch (tag) {
			case FDT_PROP:
				if (depth != 1)
					break;

				prop = fdt_offset_ptr(fdt_addr, o,
				    sizeof(*prop));
				name = fdt_string(fdt_addr,
				    fdt32_to_cpu(prop->nameoff));
				len = fdt32_to_cpu(prop->len);
				if (len == 0 || (len % 4) != 0)
					break;
				/*
				 * This will only work if the kernel is loaded
				 * withing the first 4GiB of memory.
				 */
				if (strcmp("linux,initrd-start", name) == 0)
					boot_addr = fdt32_to_cpu(
					    *(uint32_t *)prop->data);
				else if (strcmp("linux,initrd-end", name) == 0)
					boot_end = fdt32_to_cpu(
					    *(uint32_t *)prop->data);
				break;
			case FDT_BEGIN_NODE:
				depth++;
				break;
			case FDT_END_NODE:
				depth--;
				break;
			}
			o = next;
		} while (depth > 0 && (boot_addr == NULL || boot_end == NULL));

		exec(boot_addr, boot_end - boot_addr, fdt_addr);
	}

	while (1)
		asm volatile("wfe");
}

void
exit(int code)
{

	while (1)
		asm volatile("wfe");
}

void
delay(int usecs)
{
	int i, j;

	for (i = 0; i < usecs; i++)
		for (i = 0; i < 100000; i++)
			asm volatile("nop");
}

