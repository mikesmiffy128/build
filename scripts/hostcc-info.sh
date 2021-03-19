# This file is dedicated to the public domain.

# XXX HACK! should make cc-info more generic; too lazy for now
# (this is just needed for the test stuff in Buildfile which was added late)
_cc_type="$cc_type"
_cc_ver="$cc_ver"
_cc_friendly="$cc_friendly"
_cc="$cc"
cc="$hostcc"
use cc-info
cc="$_cc"
hostcc_type="$cc_type"
hostcc_ver="$cc_ver"
hostcc_friendly="$cc_ver"
cc_type="$_cc_type"
cc_ver="$_cc_ver"
cc_friendly="$_cc_friendly"

# vi: sw=4 ts=4 noet tw=80 cc=80
