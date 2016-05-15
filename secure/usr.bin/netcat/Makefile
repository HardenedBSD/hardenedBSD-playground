#	$OpenBSD: Makefile,v 1.7 2015/09/11 21:07:01 beck Exp $

.include "../../lib/libcrypto/Makefile.inc.libressl"

PROG=	netcat
SRCS=	netcat.c atomicio.c socks.c
LDADD+= -ltls -lssl -lcrypto
DPADD+=  ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

MAN1=	netcat.1

netcat.1:
	cp ${LIBRESSL_SRC}/apps/nc/nc.1 netcat.1

.PATH:	${LIBRESSL_SRC}/apps/nc

.include <bsd.prog.mk>
