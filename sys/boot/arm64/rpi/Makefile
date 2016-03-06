# $FreeBSD$

.include <src.opts.mk>

PROG=		fbsdboot.sym
BINDIR?=	/boot
INTERNALPROG=
SSP_CFLAGS=
MAN=

FILES=		fbsdboot.bin
FILESMODE_fbsdboot.bin= ${BINMODE}
CLEANFILES+=	fbsdboot.bin

.PATH:		${.CURDIR}/boot-wrapper-aarch64
# TODO: Enable PSCI support
SRCS=		boot.S		\
		ns.S		\
		spin.S

# Local source files
SRCS+=		main.c		\
		pl011_console.c

# MI source files
.PATH:          ${.CURDIR}/../../common
SRCS+=		console.c	\
		panic.c

# TODO: Find a better value for this
CFLAGS+=	-DCNTFRQ=0
# This is hard coded because the firmware doesn't seem to pass us this value
CFLAGS+=	-DDTB_ADDR=0x100000
CFLAGS+=	-fno-builtin -mgeneral-regs-only
CFLAGS+=	-ffreestanding
CFLAGS+=	-I.
CFLAGS+=	-I${.CURDIR}/../../common

# FDT support
CFLAGS+=	-I${.CURDIR}/../../fdt
CFLAGS+=	-I${.CURDIR}/../../../contrib/libfdt/
LIBFDT=		${.OBJDIR}/../../fdt/libfdt.a

LDSCRIPT=	${.CURDIR}/ldscript.arm64
LDFLAGS=	-nostdlib -static
LDFLAGS+=	-Wl,-T${LDSCRIPT}

LIBSTAND=       ${.OBJDIR}/../../../../lib/libstand/libstand.a

DPADD=		${LIBFDT} ${LIBSTAND}
LDADD=		${LIBFDT} ${LIBSTAND}

${PROG}:	${LDSCRIPT}

fbsdboot.bin:	${PROG}
	if [ `${OBJDUMP} -t ${.ALLSRC} | fgrep '*UND*' | wc -l` != 0 ]; then \
		${OBJDUMP} -t ${.ALLSRC} | fgrep '*UND*'; \
		exit 1; \
	fi
	${OBJCOPY} -S -O binary ${PROG} ${.TARGET}

.include <bsd.prog.mk>
