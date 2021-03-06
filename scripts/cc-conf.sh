extra_ldflags=
extra_cflags=

case "$cc_type" in
	clang)
		extra_cflags="-flto -fpic"
		extra_ldflags="-flto -fpic"
		# for now, only trying LLD with Clang as GCC seems to have trouble using
		# it (undefined symbol main)
		# also, not using it on macos since it doesn't work there
		if [ "$target_os" != macos ] && is_cmd ld.lld; then
			extra_ldflags="$extra_ldflags -fuse-ld=lld"
		fi ;;
	gcc)
		# XXX GCC LTO is completely busted at the moment, reintroduce later
		# errors include:
		# lto1: internal compiler error: in read_cgraph_and_symbols, at lto/lto-common.c:2702
		# lto1: internal compiler error: Bus error
		extra_cflags="-fpic"
		extra_ldflags="-fpic" ;;
esac

# see DevDocs/pie.txt for this part
pie=
if [ "$cc_type" = gcc -o "$cc_type" = clang ] && \
		[ $target_os != "freebsd" ]; then
	pie="-pie"
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
