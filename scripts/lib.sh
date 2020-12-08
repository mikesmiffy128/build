__progname="`basename "$0"`"
is_cmd() { command -v "$1" >/dev/null; }
warn() { printf "%s: %s\n" "$__progname" "$1" >&2; }

# vi: sw=4 ts=4 noet tw=80 cc=80
