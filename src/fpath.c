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
	for (;; ++path) {
noinc:	switch (*path) {
			case '\0': goto r;
			case '/': goto slash;
			case '.':
				switch (*++path) {
					case '/': goto nosl; // skip ./, it does nothing
					case '.':
						switch (*++path) {
							case '/':
								if (--depth < 0) return FPATH_OUTSIDE;
								for (--canon; canon != start &&
										*canon-- != '/';);
								--canon;
								goto nosl;
							case '\0':
								if (--depth < 0) return FPATH_OUTSIDE;
								for (--canon; canon != start &&
										*canon-- != '/';);
								--canon;
								goto r;
							default:
								*canon++ = '.'; *canon++ = '.';
								*canon++ = *path;
								continue;
						}
					case '\0':
						// special case: project root/base dir
						if (canon == start) *canon++ = '.';
						else --canon; // otherwise same as ./
						goto r;
					default:
						*canon++ = '.';
						*canon++ = *path;
						continue;
				}
			default:
				*canon++ = *path;
				continue;
		}
slash:	++depth;
		*canon++ = '/';
nosl:	while (*++path == '/');
		if (!*path) return FPATH_TRAILSLASH; // TODO(basic-core): consider this
		goto noinc;
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
	*wayout = '\0';
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
