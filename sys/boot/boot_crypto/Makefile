# $FreeBSD$
# libboot_crypto

MAN=

.include <src.opts.mk>
MK_SSP=		no

LIB=		boot_crypto
INTERNALLIB=
MK_PROFILE=	no
NO_PIC=

WARNS?=		0

# sha256 and sha512 from sys/crypto
.PATH: ${.CURDIR}/../../crypto/sha2
CFLAGS+=	-DWEAK_REFS
SRCS+=		sha256c.c sha512c.c

# md5 from libmd
.PATH: ${.CURDIR}/../../../lib/libmd
SRCS+=		md5c.c

# AES implementation from sys/crypto
.PATH: ${.CURDIR}/../../crypto/rijndael
CFLAGS+=	-I${.CURDIR}/../../
# Remove asserts
CFLAGS+=	-DNDEBUG
SRCS+=		rijndael-alg-fst.c rijndael-api-fst.c rijndael-api.c
SRCS+=		boot_crypto.c boot_crypto_aes.c

.include <bsd.stand.mk>
.include <bsd.lib.mk>
