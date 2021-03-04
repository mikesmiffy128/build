#!/bin/sh -e
build-infile scripts/lib.sh
. scripts/lib.sh

build_dir="$BUILD_ROOT_DIR/build"
cc=
hostcc=

is_default=1

# Make-style key=value options :)
while [ $# != 0 ]; do
	case "$1" in
		build-dir=*) build_dir="${1##build-dir=}" ;;
		cc=*) is_default=0; cc="${1##cc=}" ;;
		hostcc=*) is_default=0 hostcc="${1##hostcc=}" ;;
		*) die 2 "invalid option: $1" ;;
	esac
	shift
done

if [ "$cc" = "" ]; then use cc-pick; fi
use cc-target
use cc-info
use cc-pref

if [ "$hostcc" = "" ]; then hostcc="$cc"; fi
use hostcc-info

full_build_dir="$build_dir/$target_os-$target_arch-$cc_type-$cc_ver"
host_build_dir="$build_dir/host" # good enough, I think

build-dep -n "scripts/target-build.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -n "scripts/target-libbuild.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -n "scripts/target-build-dep.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"
build-dep -n "scripts/target-build-infile.build" "$full_build_dir" "$cc" "$cc_type" "$target_os"

# target all the widely used lua versions - people literally use all of these
# all the time. 5.1 should also cover LuaJit
build-dep -n "scripts/target-lbuild.build" "$full_build_dir" "$cc" "$cc_type" "$target_os" 5.4
build-dep -n "scripts/target-lbuild.build" "$full_build_dir" "$cc" "$cc_type" "$target_os" 5.3
build-dep -n "scripts/target-lbuild.build" "$full_build_dir" "$cc" "$cc_type" "$target_os" 5.2
build-dep -n "scripts/target-lbuild.build" "$full_build_dir" "$cc" "$cc_type" "$target_os" 5.1

# tests!
build-dep -n "scripts/test.build" "$host_build_dir" "$hostcc" "$hostcc_type" fpath

build-dep -w

build-infile include/build.h
mkdir -p "$full_build_dir/out/include"
cp include/build.h "$full_build_dir/out/include"

# vi: sw=4 ts=4 noet tw=80 cc=80
