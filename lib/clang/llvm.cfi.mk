.sinclude <bsd.init.mk>

.if ${MK_CFI} != "no"
# The llvm toolchain violates multiple CFI schemes. Disable CFI for
# llvm but keep LTO. With everything else compiled with LTO, the llvm
# toolchain itself still needs LTO applied, other wise the toolchain
# components (clang, lld, etc.) freak out.

NOCFI=	yes
CFLAGS+=	-flto
CXXFLAGS+=	-flto
LDFLAGS+=	-flto
.endif
