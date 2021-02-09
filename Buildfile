#!/bin/sh -e

# TODO(selfhost): write this!

build-dep sh -c "sleep 3; echo aaa >&2"
build-dep sh -c "sleep 3; echo aaa >&2" # should be instant!

echo use ./strap for now :\( >&2
exit 2
