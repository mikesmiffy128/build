#!/bin/sh -e
# This file is dedicated to the public domain.

build-infile scripts/lib.sh
. scripts/lib.sh

build_dir="$BUILD_ROOT_DIR/build"
cc=
hostcc=

# Make-style key=value options :)
while [ $# != 0 ]; do
	case "$1" in
		build-dir=*) build_dir="${1##build-dir=}" ;;
		cc=*) cc="${1##cc=}" ;;
		hostcc=*) hostcc="${1##hostcc=}" ;;
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

# build the targets!
for t in build libbuild build-dep build-infile build-tasktitle; do
	build-dep -n scripts/target.build "$t" "$full_build_dir" "$cc" "$cc_type" "$target_os"
done
# target all the widely used lua versions - people literally use all of these
# all the time. 5.1 should also cover LuaJit
for v in 5.1 5.2 5.3 5.4; do
	build-dep -n scripts/target.build lbuild "$full_build_dir" "$cc" "$cc_type" "$target_os" $v
done

# tests!
build-dep -n "scripts/test.build" "$host_build_dir" "$hostcc" "$hostcc_type" fpath

build-dep -w

build-infile include/build.h
mkdir -p "$full_build_dir/out/include"
cp include/build.h "$full_build_dir/out/include"

# vi: sw=4 ts=4 noet tw=80 cc=80
