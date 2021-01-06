target_os="`echo "
#if defined(__linux__)
#undef linux // lol, forgot about this for a minute
linux
#elif defined(__FreeBSD__)
freebsd
#elif defined(__OpenBSD__)
openbsd
#elif defined(__NetBSD__)
netbsd
#elif defined(__SunOS)
illumos
#elif defined(__APPLE__)
macos
#else
unknown
#endif
" | $cc -E - | tail -n1`"

# vi: sw=4 ts=4 noet tw=80 cc=80
