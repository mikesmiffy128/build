# This file is dedicated to the public domain.

# TODO try to support more compilers
# note: changed `` to $() here because illumos' ksh is a bit broken
cc_type="$(echo "
#if defined(__clang__)
clang
#elif defined(__GNUC__)
gcc
#else
unknown
#endif
" | $cc -E - | tail -n1)"

version_clang() {
	cc_ver=
	_suffix=
	_prefix=
	_split() {
		# catch "FreeBSD clang," "OpenBSD clang," "Apple clang," etc
		while [ $1 != "clang" ]; do
			_suffix="$_suffix-`echo "$1" | awk '{print tolower($0)}'`"
			_prefix="$1 "
			shift
		done
		shift 2 # clang version ____
		cc_ver="$1$_suffix"
		cc_friendly="${_prefix}Clang $cc_ver"
	}
	_split $($cc --version | head -1)
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
		elif [ "$2" = "[DragonFly]" ]; then
			# they just had to go and be different!
			cc_ver="$1-dragonfly"
			cc_friendly="DragonFly GCC $1"
			return
		else
			shift # (GCC)
		fi
		while [ $# != 1 ]; do shift; done
		cc_ver="$1$_suffix"
		cc_friendly="${_prefix}GCC $cc_ver"
	}
	_split $($cc --version | head -1)
}

version_unknown() {
	cc_ver="0"
	cc_friendly="an unknown compiler"
}

eval "version_$cc_type"

# vi: sw=4 ts=4 noet tw=80 cc=80
