__progname="`basename "$0"`"
is_cmd() { command -v "$1" >/dev/null; }
warn() { printf "%s: %s\n" "$__progname" "$1" >&2; }

( : >/dev/tty ) 2>/dev/null && __ttyerr=0 || __ttyerr=1
is_tty() { return $__ttyerr; }

# vi: sw=4 ts=4 noet tw=80 cc=80
