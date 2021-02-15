# allow these to be overridden for hypothetical cross-compile purposes
# WARNING: there's no dependency on these variables, so be careful if you ever
# change them (use -B to be safe)
cpoly_config=cpoly-config
if [ "$CPOLY_CONFIG" != "" ]; then cpoly_config="$CPOLY_CONFIG"; fi
cpoly_ldflags=-lcpoly
if [ "$CPOLY_LDFLAGS" != "" ]; then cpoly_ldflags="$CPOLY_LDFLAGS"; fi

if is_cmd $cpoly_config; then
	cpoly_cflags="`cpoly-config`"
else
	warn "error: couldn't find libcpoly installation (libc compatibility layer)"
	warn "unless you're using an OS that doesn't exist yet, install this:"
	warn "  https://gitlab.com/mikesmiffy128/libcpoly"
	exit 1
fi
