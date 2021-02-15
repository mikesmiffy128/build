#!/bin/sh -e
build-infile scripts/lib.sh
. scripts/lib.sh

build_dir="$BUILD_ROOT_DIR/build"
cc=

is_default=1

# Make-style key=value options :)
while [ $# != 0 ]; do
	case "$1" in
		build-dir=*) build_dir="${1##build-dir=}" ;;
		cc=*) is_default=0; cc="${1##cc=}" ;;
		*) die 2 "invalid option: $1" ;;
	esac
	shift
done

if [ "$cc" = "" ]; then use cc-pick; fi
use cc-target
use cc-info
use cc-pref

full_build_dir="$build_dir/$target_os-$target_arch-$cc_type-$cc_ver"

build-dep -n "scripts/target-build.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -n "scripts/target-libbuild.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -n "scripts/target-build-dep.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -n "scripts/target-build-infile.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -w

build-infile include/build.h
mkdir -p "$full_build_dir/out/include"
cp include/build.h "$full_build_dir/out/include"

if [ $is_default = 1 ]; then
	ln -sf "$target_os-$target_arch-$cc_type-$cc_ver" "$build_dir/default"
fi

# vi: sw=4 ts=4 noet tw=80 cc=80
