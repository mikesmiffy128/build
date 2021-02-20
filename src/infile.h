#ifndef INC_INFILE_H
#define INC_INFILE_H

#include <stdbool.h>

#include <intdefs.h>

bool infile_ensure(const char *path);

/* returns 1 if changed, 0 if not, or -1 on error */
int infile_query(const char *path, uint tgtnewness);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
