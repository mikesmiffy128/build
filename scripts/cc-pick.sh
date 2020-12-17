# version primarily in use for the project; other things might work, but that's
# for other people to find out
_clang_major=11
_clang_minor=0
_clang_patch=0
_clang_ver="$_clang_major.$_clang_minor.$_clang_patch"

can_sanitize=0
extra_ldflags=

# TODO(config-cc): just a bit of gruntwork to reshuffle these ifs...
if is_cmd clang; then
	cc=clang
	extra_cflags="-flto"
	extra_ldflags="-flto"
	can_sanitize=1
	_havever="`clang --version | grep "clang version "`"
	if [ "$_havever" != "clang version $_clang_ver" ]; then
		warn "note: you have $_havever, but the project primarily uses $_clang_ver"
		# XXX this assumes we're newer, technically we could be older briefly
		# if I don't pacman -Syu for a while :^)
		warn "* if something fails to build, update Clang! *"
		warn "* if you want a reproducible build, you need the exact version! *"
	fi

	# XXX for now, only trying LLD with Clang as GCC seems to have trouble using
	# it (undefined symbol main) - other things may not even try to use it
	# anyway e.g. tcc just ignores
	if is_cmd ld.lld; then
		extra_ldflags="$extra_ldflags -fuse-ld=lld"
	else
		warn "note: ld.lld is unavailable, using system default linker instead"
		warn "      This is probably fine, but the project primarily uses LLD"
		warn "* if you want a reproducible build, you might need to install lld! *"
		extra_ldflags=""
	fi
elif is_cmd gcc; then
	warn "note: Clang is unavailable, using GCC instead"
	warn "      This is probably fine, but the project primarily uses Clang"
	warn "* if you want a reproducible build, install Clang $_clang_ver! *"
	cc=gcc
	extra_cflags="-flto"
	extra_ldflags="-flto"
	can_sanitize=1
else
	warn 'warning: unsure what C compiler to use, just using `cc`'
	warn "* if something fails to build, install Clang $_clang_ver! *"
	cc=cc
	extra_cflags=""
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
