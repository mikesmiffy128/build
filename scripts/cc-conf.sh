_clang_ver="11.0.0"

extra_ldflags=
extra_cflags=
can_sanitize=0

case "$cc" in
	*gcc)
		extra_cflags="-flto"
		extra_ldflags="-flto"
		can_sanitize=1 ;;
	clang*)
		extra_cflags="-flto"
		extra_ldflags="-flto"
		can_sanitize=1
		_havever="`clang --version | grep "clang version "`"
		if [ "$_havever" != "clang version $_clang_ver" ]; then
			warn "note: you have $_havever, but the project primarily uses $_clang_ver"
			# XXX this assumes we're newer, technically we could be older briefly
			# if I don't pacman -Syu for a while :^)
			warn "* if you want a reproducible build, you need the exact version! *"
		fi
		# XXX for now, only trying LLD with Clang as GCC seems to have trouble
		# using it (undefined symbol main) - other things may not even try to
		# use it anyway e.g. tcc just ignores
		if is_cmd ld.lld; then
			extra_ldflags="$extra_ldflags -fuse-ld=lld"
		else
			warn "note: ld.lld is unavailable, using system default linker instead"
			warn "      This is probably fine, but the project primarily uses LLD"
			warn "* if you want a reproducible build, you might need to install lld! *"
		fi ;;
	tcc)
		if [ "$target_os" = "linux" ]; then
			extra_ldflags="$extra_ldflags scripts/tcchack.c"
		fi ;;
	*) ;;
esac

# vi: sw=4 ts=4 noet tw=80 cc=80
