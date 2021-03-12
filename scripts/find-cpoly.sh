# allow these to be overridden for hypothetical cross-compile purposes
# WARNING: there's no dependency on these variables, so be careful if you ever
# change them (use -B to be safe)
cpoly_config=cpoly-config
if [ "$CPOLY_CONFIG" != "" ]; then cpoly_config="$CPOLY_CONFIG"; fi
cpoly_ldflags=-lcpoly
if [ "$CPOLY_LDFLAGS" != "" ]; then cpoly_ldflags="$CPOLY_LDFLAGS"; fi
cpoly_use_bundled=0

if is_cmd $cpoly_config; then
	cpoly_cflags="`cpoly-config`"
else
	warn "warning: couldn't find libcpoly installation (libc compatibility layer)"
	warn "         statically linking a bundled copy instead"
	warn "* this is not a recommended configuration and may produce the wrong binaries! *"
	warn "note: if youre confused, read the README for more details"
	# note: cpoly-config normally defines _FILE_OFFSET_BITS for us; the whole
	# point of libcpoly is to let us pretend such abominations don't exist
	# however, until build is widely packaged, we can put up with such nonsense
	# in the name of (relative) user convenience...
	cpoly_cflags="-isystem libcpoly/include -D_FILE_OFFSET_BITS=64"
	cpoly_ldflags=""
	cpoly_use_bundled=1
fi
