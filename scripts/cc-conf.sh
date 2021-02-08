extra_ldflags=
extra_cflags=
can_sanitize=0

cc_type="`echo "
#if defined(__clang__)
clang
#elif defined(__GNUC__)
gcc
#elif defined(__TINYC__)
tcc
#else
unknown
#endif
" | $cc -E - | tail -n1`"

version_clang() {
	cc_ver=
	_suffix=
	_prefix=
	_split() {
		# catch "FreeBSD Clang," "OpenBSD Clang," "Apple Clang," etc
		while [ $1 != "clang" ]; do
			_suffix="$_suffix-`echo "$1" | awk '{print tolower($0)}'`"
			_prefix="$1 "
			shift
		done
		shift 2 # clang version ____
		cc_ver="$1$_suffix"
	}
	_split $("$cc" --version | head -1)
	cc_friendly="${_prefix}Clang $cc_ver"
}

version_gcc() {
	cc_ver=
	_suffix=
	_prefix=
	_split() {
		shift # first word should always be gcc
		# HACK: hardcode nb4 yyyymmdd (do *other* OSes have GCC forks?)
		if [ "$1" = "(nb4" ]; then
			shift 2
			_suffix="-netbsd"
			_prefix="NetBSD "
		else
			shift # (GCC)
		fi
		while [ $# != 1 ]; do shift; done
		cc_ver="$1$_suffix"
	}
	_split $("$cc" --version | head -1)
	cc_friendly="${_prefix}GCC $cc_ver"
}

version_tcc() {
	_split() {
		shift 2 # tcc version ____
		cc_ver="$1"
	}
	_split $("$cc" -version)
	cc_friendly="tcc $cc_ver"
}

version_unknown() {
	cc_ver="0"
	cc_friendly="an unknown compiler"
}

eval "version_$cc_type"

# to make it as easy as possible for users to reproduce builds (and to make it
# as easy as possible for me to test-compile on supported platforms), use the
# native compiler that ships will each stable OS release - except on linux,
# since every distro is different. in that case I happen to use the latest
# stable clang, so that's the canonical preferred linux compiler
plat_supported_cc=
case "$target_os" in
	linux)
		plat_supported_cc="clang-11.0.0"
		plat_friendly_cc="Clang 11.0.0" ;;
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

case "$cc_type" in
	clang)
		extra_cflags="-flto -fpic"
		extra_ldflags="-flto -fpic"
		can_sanitize=1
		# for now, only trying LLD with Clang as GCC seems to have trouble using
		# it (undefined symbol main) - other things may not even try to use it
		# anyway e.g. tcc just ignores
		if [ "$target_os" = macos ]; then
			: # oh, don't use lld on macos though, it doesn't work
		elif is_cmd ld.lld; then
			extra_ldflags="$extra_ldflags -fuse-ld=lld"
		else
			warn "note: lld is unavailable, using system default linker instead"
			warn "* this should still work, but the project normally uses lld with Clang"
			warn "* to reproduce a build that definitely works, install lld!"
		fi ;;
	gcc)
		extra_cflags="-flto -fpic"
		extra_ldflags="-flto -fpic"
		can_sanitize=1 ;;
	tcc)
		if [ "$target_os" = "linux" ]; then
			# HACK: link an extra file; see comment in that file for insight
			extra_ldflags="$extra_ldflags scripts/tcchack.c"
		fi ;;
	*) ;;
esac

# see DevDocs/pie.txt for this part
pie=
if [ "$cc_type" = gcc -o "$cc_type" = clang ] && \
		[ $target_os != "freebsd" ]; then
	pie="-pie"
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
