#ifndef INC_DB_H
#define INC_DB_H

#include <stdbool.h>
#include <sys/types.h>

#include <intdefs.h>

#include "defs.h"

struct db_infile {
	uint newness;
	uint mode; // (mode, uid and gid are moved up for packing)
	uid_t uid; gid_t gid;
	uvlong len; // if nonexistent, this is -1 and the rest is undefined
	// otherwise:
	uvlong inode;
};

struct db_taskresult {
	uint newness;
	uchar status;
	// char padding[3]; :(
	uint id; // used for unique out/err filenames
	uint ninfiles, ndeps; // counts (together for packing)
	// char ugh_even_more_padding[4];
	const char *const *infiles;
	const struct task_desc *deps;
};

void db_init(void);
void db_finalise(void);

/*
 * moves a string into the string pool if necessary; the string should be in
 * static/global memory.
 * returns the interned string, or null on failure
 */
const char *db_intern(const char *s);

/*
 * moves a heap-allocated string into the string pool if necessary; calls free()
 * on the argument if it's already in there.
 * returns the interned string, or null on failure
 */
const char *db_intern_free(char *s);

extern int db_dirfd;
extern uint db_newness;

/*
 * These functions either return exising entries from the db or create new ones
 * in memory without committing - either way, changes have to be committed using
 * db_commit*(). Newly created entries get a special low newness value of 0.
 */
struct db_infile *db_getinfile(const char *path);
struct db_taskresult *db_gettaskresult(struct task_desc desc);

void db_commitinfile(struct db_infile *i);
void db_committaskresult(struct db_taskresult *t);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
