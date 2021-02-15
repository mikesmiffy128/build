__progname="`basename "$0"`"
is_cmd() { command -v "$1" >/dev/null; }
warn() { printf "%s: %s\n" "$__progname" "$1" >&2; }
die() { warn "$2"; exit "$1"; }

( : >/dev/tty ) 2>/dev/null && __ttyerr=0 || __ttyerr=1
is_tty() { return $__ttyerr; }

# for use in build scripts
use() {
	build-infile "scripts/$1.sh"
	. "scripts/$1.sh"
}

shellesc() { echo "$1" | sed -e "s/'/'\\\\''/g"; }

# vi: sw=4 ts=4 noet tw=80 cc=80
