if is_cmd clang; then
	cc=clang
elif is_cmd gcc; then
	warn "note: Clang is unavailable, using GCC instead"
	warn "      This is probably fine, but the project primarily uses Clang"
	warn "* if you want a reproducible build, install Clang $_clang_ver! *"
	cc=gcc
else
	warn 'warning: unsure what C compiler to use, just using `cc`'
	warn "* if something fails to build, install Clang $_clang_ver! *"
	cc=cc
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
