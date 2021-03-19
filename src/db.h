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

#ifndef INC_DB_H
#define INC_DB_H

#include <stdbool.h>
#include <sys/types.h>

#include <intdefs.h>

#include "defs.h"

struct db_infile {
	uint newness;
	// there shouldn't be more than 16 mode bits, but mode_t is 32-bit!?
	// here we assume the topmost bit is never used, for the sake of packing
	uint mode : 31;
	bool checked : 1; // did we check in this current run? (NOT saved to disk!)
	uid_t uid; gid_t gid;
	uvlong len; // if nonexistent, this is -1 and the rest is undefined
	uvlong inode;
};

struct db_taskresult {
	uint newness;
	uchar status;
	bool checked; // also not saved to disk
	// char padding[2]; :(
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
 * db_commit*(). Newly created entries get a "special" newness value of 0.
 */
struct db_infile *db_getinfile(const char *path);
struct db_taskresult *db_gettaskresult(struct task_desc desc);

void db_commitinfile(struct db_infile *i);
void db_committaskresult(struct db_taskresult *t);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
