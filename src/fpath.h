#ifndef INC_FPATH_H
#define INC_FPATH_H

#include <stdbool.h>
#include <intdefs.h>

/* this just stores the result of getcwd() to avoid repeatedly calling it */
void fpath_setcwd(void);

/*
 * NOTE: dir MUST be canonicalised and MUST go into an actual subdirectory
 * (not just `.`!).
 */
bool fpath_leavesubdir(const char *dir, char *wayout, uint outsz);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
