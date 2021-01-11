#ifndef INC_FPATH_H
#define INC_FPATH_H

#include <stdbool.h>
#include <intdefs.h>

enum fpath_err {
	FPATH_OK,
	FPATH_EMPTY,
	FPATH_ABSOLUTE,
	FPATH_OUTSIDE,
	FPATH_TRAILSLASH
};

/*
 * This function is a bit mental. It will:
 * 1) canonicalise `path` into `canon`, removing redundant slashes, simplifying
 *    ./ ../ etc (although result is only valid if FPATH_OK is returned!)
 * 2) if the path is not valid in the context of the build system, return an
 *    error code
 * 4) populate `*reldepth` *if* it's not null, indicating how much deeper into
 *    directories we are relative to the build base path (ie working dir)
 *
 * NOTE! canon must be at least as large as path, but needed be larger.
 */
enum fpath_err fpath_canon(const char *path, char *canon, int *reldepth);

char *fpath_errorstring(enum fpath_err e);

/*
 * NOTE: dir MUST be canonicalised and MUST go into an actual subdirectory
 * (not just `.`!).
 */
bool fpath_leavesubdir(const char *dir, char *wayout, uint outsz);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
