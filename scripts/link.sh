set -- # use the shell's single array for object file names
for s in $src; do
	# hash the flags, so multiple configurations of eg lbuild produce different
	# objects (truncate said hash a bit to avoid unwieldiness)
	sha1="`printf %s "$cflags" | scripts/sha1 | cut -c -8`"
	if [ "$sha1" = "" ]; then exit 1; fi # sigh, no pipefail
	o="$build_dir/`echo "${s%%.c}" | sed -e s@src/@@g -e s@/@:@g`:$sha1.o"
	set -- "$@" "$o"
	build-dep -n scripts/cc.build "$build_dir" "$cc" "-c $cflags" "$s" "$o"
done
# wait for required libraries (ie libbuild)
for d in $libs; do
	build-dep -n "scripts/target-$d.build" "$build_dir" "$cc" "$cc_type" "$target_os"
done
build-dep -w

mkdir -p "$build_dir/out/`dirname "$out"`"
# XXX unconditionally passing -Lbuild/out/lib here because I couldn't be
# bothered figuring out the shell quoting nonsense to get paths with spaces to
# pass properly via an ldflags variable - doesn't *really* matter that much
$cc $ldflags -L"$build_dir/out/lib" -o "$build_dir/out/$out" "$@"

# vi: sw=4 ts=4 noet tw=80 cc=80
