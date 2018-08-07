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
SUBDIR+=	asan_dynamic
SUBDIR+=	stats
SUBDIR+=	stats_client
.if ${MACHINE_CPUARCH} == "amd64"
SUBDIR+=	cfi
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
    ${MACHINE_CPUARCH} == "arm"
SUBDIR+=	profile
.endif

.include <bsd.subdir.mk>
