/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <intdefs.h>

#include "fpath.h"
#include "unreachable.h"

enum fpath_err fpath_canon(const char *path, char *canon, int *reldepth) {
	if (*path == '\0') return FPATH_EMPTY;
	if (*path == '/') return FPATH_ABSOLUTE;
	int depth = 0;
	const char *start = canon;
	for (;;) {
		switch (*path) {
			case '\0': goto r;
			case '/': goto slash;
			case '.':
				switch (*++path) {
					case '/': goto nosl; // skip ./, it does nothing
					case '.':
						switch (*++path) {
							case '/':
								if (--depth < 0) return FPATH_OUTSIDE;
								canon -= 2; // point right before the slash
								// go back to the start of the component, to
								// overwrite with the next component of path
								while (canon != start && canon[-1] != '/') {
									--canon;
								}
								goto nosl;
							case '\0':
								if (--depth < 0) return FPATH_OUTSIDE;
								canon -= 2; // point right before the slash
								// go back to the slash _before_ the start of
								// the component
								while (canon != start && *canon != '/') --canon;
								// special case: project root/base dir
								if (canon == start) *canon++ = '.';
								goto r;
							default:
								*canon++ = '.'; *canon++ = '.';
								*canon++ = *path;
								goto mid;
						}
					case '\0':
						// special case: project root/base dir
						if (canon == start) *canon++ = '.';
						else --canon; // otherwise same as ./ (remove the /)
						goto r;
					default:
						*canon++ = '.';
						*canon++ = *path;
						goto mid;
				}
			default:
				*canon++ = *path;
				goto mid;
		}
mid:	for (;;) {
			switch (*++path) {
				case '/': goto slash;
				case '\0': goto r;
				default: *canon++ = *path;
			}
		}
slash:	++depth;
		*canon++ = '/';
nosl:	while (*++path == '/');
		if (!*path) return FPATH_TRAILSLASH; // TODO(basic-core): consider this
	}
r:	if (canon == start) return FPATH_EMPTY;
	*canon = '\0';
	if (reldepth) *reldepth = depth;
	return FPATH_OK;
}

const char *fpath_errorstring(enum fpath_err e) {
	switch (e) {
		case FPATH_OK: return 0;
		case FPATH_EMPTY: return "path is an empty string";
		case FPATH_ABSOLUTE: return "tried to use an absolute path";
		case FPATH_OUTSIDE: return "path points outside the build system";
		case FPATH_TRAILSLASH: return "path has an unexpected trailing slash";
	}
	unreachable; // the switch exhausts the enum but okay gcc cool whatever
}

bool fpath_leavesubdir(const char *dir, char *wayout, uint outsz) {
	char *p = wayout;
	if (p - wayout >= outsz - 3) { errno = ENAMETOOLONG; return false; }
	*p++ = '.';
	*p++ = '.';
	for (; *dir; ++dir) if (*dir == '/') {
		if (p - wayout >= outsz - 4) { errno = ENAMETOOLONG; return false; }
		*p++ = '/';
		*p++ = '.';
		*p++ = '.';
	}
	*p = '\0';
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
