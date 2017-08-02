# $FreeBSD$

.include <src.opts.mk>

.if ${MK_FORTH} != "no"
CFLAGS+= -DBOOT_FORTH
.include "${.CURDIR}/../../Makefile.ficl"
.endif

LIB=	efi
INTERNALLIB=
WARNS?=	2

SRCS=	delay.c devpath.c efi_console.c efinet.c efipart.c env.c errno.c \
	handles.c wchar.c libefi.c efizfs.c devicename.c key_inject.c \
        geli_driver.c

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCS+=	time.c
.elif ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "arm"
SRCS+=	time_event.c
.endif

.PATH: ${.CURDIR}/../../../geom/eli
CFLAGS+= -D_STAND
SRCS+=		g_eli_hmac.c g_eli_key.c g_eli_key_cache.c pkcs5v2.c

# We implement a slightly non-standard %S in that it always takes a
# CHAR16 that's common in UEFI-land instead of a wchar_t. This only
# seems to matter on arm64 where wchar_t defaults to an int instead
# of a short. There's no good cast to use here so just ignore the
# warnings for now.
CWARNFLAGS.efinet.c+=	-Wno-format
CWARNFLAGS.efipart.c+=	-Wno-format
CWARNFLAGS.env.c+=	-Wno-format

.if ${MACHINE_CPUARCH} == "aarch64"
NOPIE=		1
CFLAGS+=	-mgeneral-regs-only
.endif
.if ${MACHINE_ARCH} == "amd64"
CFLAGS+= -fPIC -mno-red-zone
.endif
CFLAGS+= -I${.CURDIR}/../include
CFLAGS+= -I${.CURDIR}/../include/${MACHINE}
CFLAGS+= -I${.CURDIR}/../../../../lib/libstand
.if ${MK_ZFS} != "no"
CFLAGS+=	-I${.CURDIR}/../../zfs
CFLAGS+=	-I${.CURDIR}/../../../cddl/boot/zfs
CFLAGS+=	-I${.CURDIR}/../../../crypto/skein
CFLAGS+=	-DEFI_ZFS_BOOT
.endif

CFLAGS+= -I${.CURDIR}/../../boot_crypto
CFLAGS+= -I${.CURDIR}/../../..

# Pick up the bootstrap header for some interface items
CFLAGS+= -I${.CURDIR}/../../common

# Handle FreeBSD specific %b and %D printf format specifiers
CFLAGS+= ${FORMAT_EXTENSIONS}

# Do not use TERM_EMU on arm and arm64 as it doesn't behave well with serial console
.if ${MACHINE_CPUARCH} != "arm" && ${MACHINE_CPUARCH} != "aarch64"
CFLAGS+= -DTERM_EMU
.endif

beforedepend ${OBJS}: machine

CLEANFILES+=   machine

machine: .NOMETA
	ln -sf ${.CURDIR}/../../../${MACHINE}/include machine


.include <bsd.lib.mk>
