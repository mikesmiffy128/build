# This file is dedicated to the public domain.

_target_info() {
	target_os="$1"
	target_arch="$2"
}
_target_info `echo "
#if defined(__linux__)
#include <features.h>
#ifdef __GLIBC__
#undef linux // lol, forgot about this for a minute
linux
#else // assume musl, other libcs are largely irrelevant
linux_musl
#endif
#elif defined(__FreeBSD__)
freebsd
#elif defined(__OpenBSD__)
openbsd
#elif defined(__NetBSD__)
netbsd
#elif defined(__SunOS) || defined(__sun)
illumos
#elif defined(__APPLE__)
macos
#else
unknown
#endif
#if defined(__x86_64__) || defined(__amd64__)
x64
#elif defined(__i386__)
x86
#elif defined(__aarch64__)
arm64
#elif defined(__arm__)
arm
#elif defined(__ppc64__)
ppc64
#elif defined(__ppc__)
ppc
#elif defined(__mips__)
mips
#elif defined(__riscv__) || defined(__riscv)
riscv
#else
unknown
#endif
" | $cc -E - | grep -v "#" | grep -v '^$' | tail -n2`

# vi: sw=4 ts=4 noet tw=80 cc=80
