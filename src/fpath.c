#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <errmsg.h>
#include <intdefs.h>

// FIXME: this should be dynamically allocated, since it's technically coming
// *from* the kernel so there's no reason it couldn't be arbitrarily long
static char cwd[PATH_MAX * 2];

void fpath_setcwd(void) {
	if (!getcwd(cwd, sizeof(cwd))) {
		errmsg_die(100, "couldn't get working directory");
	}
}

bool fpath_leavesubdir(const char *dir, char *wayout, uint outsz) {
	char *p = wayout;
	if (p - wayout >= outsz - 3) { errno = ENOMEM; return false; }
	*p++ = '.';
	*p++ = '.';
	for (; *dir; ++dir) if (*dir == '/') {
		if (p - wayout >= outsz - 4) { errno = ENOMEM; return false; }
		*p++ = '/';
		*p++ = '.';
		*p++ = '.';
	}
	*wayout = '\0';
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
