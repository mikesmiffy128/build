# This file is dedicated to the public domain.

cc=cc

# most platforms use clang these days but netbsd and illumos still ship GCC
# we want to use the standard host compiler as the preferred compiler by
# default, assuming no cross-compiling (if the user wants to cross-compile they
# will need to set a specific $CC, so this logic here won't matter)
_uname="`uname`"
case "`uname`" in
	NetBSD|DragonFly|SunOS) _try="gcc clang" ;;
	*) _try="clang gcc" ;;
esac

for _t in $_try; do
	if is_cmd $_t; then
		cc=$_t
		break
	fi
done

if [ "$cc" = cc ]; then
	if ! is_cmd cc; then
		warn "error: missing a C compiler - install Clang or GCC!"
		exit 1
	fi
	warn 'warning: unsure what C compiler to use, just using `cc`'
	warn "* if something fails to build, install Clang or GCC! *"
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
