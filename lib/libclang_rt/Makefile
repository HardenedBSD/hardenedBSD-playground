# $FreeBSD$

.include <src.opts.mk>

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_ARCH} == "amd64" || \
    ${MACHINE_CPUARCH} == "aarch64"
SUBDIR+=	include
.endif

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SUBDIR+=	asan
SUBDIR+=	asan-preinit
SUBDIR+=	asan_cxx
<<<<<<< HEAD
=======
SUBDIR+=	asan_dynamic
SUBDIR+=	safestack
>>>>>>> origin/hardened/current/master
SUBDIR+=	stats
SUBDIR+=	stats_client
.if ${MACHINE_CPUARCH} == "amd64"
SUBDIR+=	tsan
SUBDIR+=	tsan_cxx
.endif
SUBDIR+=	ubsan_minimal
SUBDIR+=	ubsan_standalone
SUBDIR+=	ubsan_standalone_cxx
.endif

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_ARCH} == "amd64" || \
    ${MACHINE_CPUARCH} == "aarch64"
SUBDIR+=	safestack
.endif

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64" || \
    (${MACHINE_CPUARCH} == "arm" && ${MACHINE_ARCH} != "armeb")
SUBDIR+=	profile
.endif

.include <bsd.subdir.mk>
