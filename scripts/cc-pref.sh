# This file is dedicated to the public domain.

# to make it as easy as possible for users to reproduce builds (and to make it
# as easy as possible for me to test-compile on supported platforms), we try to
# use the native compiler that ships will each stable OS release - except on
# linux, since every distro is different. in that case I happen to use the
# latest stable clang, so that's the canonical preferred linux compiler for the
# project.

# if some other random compiler is used, it'll probably still be fine, but if
# something goes wrong, these warnings aim to prompt the user to try the thing
# that Works On My Machine™

plat_supported_cc=
case "$target_os" in
	linux)
		plat_supported_cc="clang-11.0.1"
		plat_friendly_cc="Clang 11.0.1" ;;
	freebsd)
		plat_supported_cc="clang-10.0.1-freebsd"
		plat_friendly_cc="FreeBSD Clang 10.0.1" ;;
	openbsd)
		plat_supported_cc="clang-10.0.1-openbsd"
		plat_friendly_cc="OpenBSD Clang 10.0.1" ;;
	netbsd)
		plat_supported_cc="gcc-7.5.0-netbsd"
		plat_friendly_cc="NetBSD GCC 7.5.0" ;;
	# TODO(port) want illumos here too! can't get a test vm running atm though
	*) ;;
esac

if [ "$plat_supported_cc" != "" -a \
		"$cc_type-$cc_ver" != "$plat_supported_cc" ]; then
	warn "note: compiling using $cc_friendly rather than $plat_friendly_cc,"
	warn "      which is the supported compiler for your target platform"
	warn "* to reproduce a build that definitely works, use $plat_friendly_cc!"
fi

[ "$cc_type" != clang -o "$target_os" = macos ] || if ! is_cmd ld.lld; then
	warn "note: lld is unavailable, using system default linker instead"
	warn "* this should still work, but the project normally uses lld with Clang"
	warn "* to reproduce a build that definitely works, install lld!"
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
