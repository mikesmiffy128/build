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

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <alloc.h>
#include <basichashes.h>
#include <errmsg.h>
#include <fmt.h>
#include <intdefs.h>
#include <iobuf.h>
#include <noreturn.h>
#include <table.h>

#include "db.h"
#include "defs.h"
#include "tableshared.h"

// TODO(db-opt): this whole file :)

// from db-strpool.c (no header because who cares)
void strpool_init(void);
uint strpool_getidx(const char *s);
const char *strpool_fromidx(uint idx);

int db_dirfd;
uint db_newness = 1; // start at 1 (as 0 is used for newly-created entries)
static uint nexttaskid = 0; // just a serial number

#define DBVER 1 // increase if something gets broken!

static bool savesymstr(const char *name, const char *val) {
	// create a new symlink and rename it to atomically replace the old one
	// this could break if someone is poking around .builddb at the same time.
	// adequate solution: don't do that.
	unlinkat(db_dirfd, "symnew", 0); // in case it was left due to a crash
	if (symlinkat(val, db_dirfd, "symnew") == -1) return false;
	if (renameat(db_dirfd, "symnew", db_dirfd, name) == -1) {
		unlinkat(db_dirfd, "symnew", 0);
		return false;
	}
	return true;
}

static bool loadsymstr(const char *name, char *buf, uint sz) {
	long ret = readlinkat(db_dirfd, name, buf, sz);
	if (ret == -1) return false;
	buf[ret] = '\0';
	return true;
}

static bool savesymnum(const char *name, vlong val) {
	char buf[21];
	buf[fmt_fixed_s64(buf, val)] = '\0';
	return savesymstr(name, buf);
}

static vlong loadsymnum(const char *name, const char **errstr) {
	char buf[21];
	if (!loadsymstr(name, buf, sizeof(buf))) return 0;
	vlong ret = strtonum(buf, LLONG_MIN, LLONG_MAX, errstr);
	return ret;
}

DEF_PERMALLOC(infile, struct db_infile, 4096)
DEF_PERMALLOC(taskresult, struct db_taskresult, 4096)

struct lookup_infile {
	const char *path;
	struct db_infile *i;
};
DECL_TABLE(static, lookup_infile, const char *, struct lookup_infile)
static inline const char *kmemb_infile(struct lookup_infile *e) {
	return e->path;
}
DEF_TABLE(static, lookup_infile, hash_ptr, table_ideq, kmemb_infile)

struct lookup_taskresult {
	struct task_desc desc;
	struct db_taskresult *r;
};
static inline struct task_desc kmemb_taskresult(struct lookup_taskresult *e) {
	return e->desc;
}
DECL_TABLE(static, lookup_taskresult, struct task_desc, struct lookup_taskresult)
DEF_TABLE(static, lookup_taskresult, hash_task_desc, eq_task_desc,
		kmemb_taskresult)

static struct table_lookup_infile infiles;
static struct table_lookup_taskresult results;

static void checkread(int nread, int len) {
	if (nread == -1) errmsg_die(100, msg_fatal, "couldn't read database file");
	if (nread != len) {
		errmsg_diex(100, msg_fatal, "invalid or corrupt database file");
	}
}
static noreturn diemem(void) {
	errmsg_die(100, msg_fatal, "couldn't allocate memory for task database");
}

static void unlock(void) { unlinkat(db_dirfd, "lock", 0); }

void db_init(void) {
	if (mkdir(BUILDDB_DIR, 0755) == -1 && errno != EEXIST) {
		errmsg_die(100, msg_fatal, "couldn't create "BUILDDB_DIR" directory");
	}
	db_dirfd = open(BUILDDB_DIR, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	if (db_dirfd == -1) {
		errmsg_die(100, msg_fatal, "couldn't open "BUILDDB_DIR" directory");
	}
	// do some idiot proofing
	pid_t p = getpid();
	char buf[sizeof(p) <= 4 ? 11 : 21];
	if (sizeof(p) <= 4) buf[fmt_fixed_u32(buf, p)] = '\0';
	else buf[fmt_fixed_u64(buf + 0, p)] = '\0'; // buf + 0 dodges bogus warning
	for (int attempt = 0; attempt < 5; ++attempt) {
		if (symlinkat(buf, db_dirfd, "lock") == -1) {
			if (errno != EEXIST) {
				errmsg_die(100, msg_fatal, "couldn't create lock file");
			}
			const char *errstr = 0;
			errno = 0;
			vlong oldpid = loadsymnum("lock", &errstr);
			if (errno == ENOENT) {
				// old process just finished, start the next race!
				continue;
			}
			else if (errno == EINVAL) {
				errmsg_diex(2, msg_fatal, "lock file PID is ", errstr);
			}
			else if (errno) {
				errmsg_die(100, msg_fatal, "couldn't read lock file");
			}
			if (getpgid(oldpid) == -1) {
				if (errno == ESRCH) {
					// build must have died/crashed, start the next race!
					// XXX this is actually technically a race right here,
					// but not clear how to do better (this is probably good
					// enough for basic idiot proofing anyway)
					unlinkat(db_dirfd, "lock", 0);
					continue;
				}
				errmsg_warn(msg_warn, "couldn't check lock file PID");
				errmsg_diex(100, msg_note, "refusing to run until "BUILDDB_DIR
						"/lock is manually deleted, just to be safe");
			}
			else {
				errmsg_diex(1, msg_error, "build is already running!");
			}
		}
		goto ok;
	}
	// inform the user of their severe mishandling of their tools
	errmsg_diex(2, msg_fatal, "user is grossly incompetent");
ok:	atexit(&unlock);
	// XXX all this error handling is far from perfect in terms of simplicity
	const char *errstr = 0;
	errno = 0;
	int dbversion = loadsymnum("version", &errstr); // ignore overflow, shrug
	if (errno == ENOENT) {
		if (!savesymnum("version", DBVER)) {
			errmsg_die(100, msg_fatal, "couldn't create task database");
		}
	}
	else if (errno == EINVAL) {
		// status 2: user has messed with something, not our fault!
		errmsg_diex(2, msg_fatal, "task database version is ", errstr);
	}
	else if (errno) {
		errmsg_die(100, msg_fatal, "couldn't read task database version");
	}
	else if (dbversion != DBVER) {
		// can add migration or something later if we actually bump the
		// version, but this is fine for now
		errmsg_diex(1, msg_fatal, "unsupported task database version; "
				"try rm -rf "BUILDDB_DIR"/");
	}
	errstr = 0;
	errno = 0;
	uint newness = loadsymnum("newness", &errstr); // ignore overflow again
	if (errno == EINVAL) {
		errmsg_diex(1, msg_fatal, "global task newness value is ", errstr);
	}
	else if (errno) {
		if (errno != ENOENT) {
			errmsg_die(100, msg_fatal, "couldn't read task database version");
		}
	}
	else {
		db_newness = newness;
	}
	// bump newness immediately: if we crash, later rebuilds won't be prevented
	if (!savesymnum("newness", db_newness + 1)) {
		errmsg_die(100, msg_fatal, "couldn't update task database");
	}
	strpool_init();
	int fd = openat(db_dirfd, "tables", O_RDONLY, 0644);
	if (fd == -1) {
		if (errno == ENOENT) {
			// setup default tables instead
			if (!table_init_lookup_infile(&infiles) ||
					!table_init_lookup_taskresult(&results)) {
				errmsg_die(100, msg_fatal, "couldn't allocate hashtable");
			}
			return;
		}
		errmsg_die(200, msg_fatal, "couldn't open "BUILDDB_DIR"/strings");
	}
	union {
		struct ibuf b;
		char x[sizeof(struct ibuf) + 65536];
	} _b;
	struct ibuf *b = &_b.b;
	*b = (struct ibuf){fd, 65536};
	// recreate the hashtable at its previous size; otherwise we pay a big
	// rehashing penalty a bunch of times in a row for big tables
	int n = ibuf_getbytes(b, &infiles.sz, sizeof(infiles.sz));
	checkread(n, sizeof(infiles.sz));
	infiles.data = malloc(infiles.sz * sizeof(*infiles.data));
	if (!infiles.data) diemem();
	uint flagsz = infiles.sz / 8;
	if (flagsz == 4) flagsz = 8;
	infiles.flags = calloc(flagsz, 1);
	if (!infiles.flags) diemem();
	uint count;
	n = ibuf_getbytes(b, &count, sizeof(count));
	checkread(n, sizeof(count));
	for (uint i = 0; i < count; ++i) {
		uint idx;
		n = ibuf_getbytes(b, &idx, sizeof(idx));
		checkread(n, sizeof(idx));
		const char *path = strpool_fromidx(idx);
		struct lookup_infile *p = table_put_lookup_infile(&infiles, path);
		if (!p) diemem(); // rehash could happen (and fail) if (very) unlucky
		p->path = path;
		p->i = permalloc_infile();
		if (!p->i) diemem();
		n = ibuf_getbytes(b, p->i, sizeof(*p->i));
		checkread(n, sizeof(*p->i));
	}
	// same again for task results
	n = ibuf_getbytes(b, &results.sz, sizeof(results.sz));
	checkread(n, sizeof(results.sz));
	results.data = malloc(results.sz * sizeof(*results.data));
	if (!results.data) diemem();
	flagsz = results.sz / 8;
	if (flagsz == 4) flagsz = 8;
	results.flags = calloc(flagsz, 1);
	if (!results.flags) diemem();
	n = ibuf_getbytes(b, &count, sizeof(count));
	checkread(n, sizeof(count));
	for (uint i = 0; i < count; ++i) {
		uint idx;
		struct VEC(const char *) argv = {0};
		for (;;) {
			n = ibuf_getbytes(b, &idx, sizeof(idx));
			checkread(n, sizeof(idx));
			if (idx == -1u) { // using -1 for null; no way there's 4bn strings!
				if (!vec_push(&argv, 0)) diemem();
				break;
			}
			else {
				if (!vec_push(&argv, strpool_fromidx(idx))) diemem();
			}
		}
		struct task_desc d;
		d.argv = argv.data;
		n = ibuf_getbytes(b, &idx, sizeof(idx));
		checkread(n, sizeof(idx));
		d.workdir = strpool_fromidx(idx);
		struct lookup_taskresult *p = table_put_lookup_taskresult(&results, d);
		if (!p) diemem();
		p->desc = d;
		p->r = permalloc_taskresult();
		if (!p->r) diemem();
		// NOTE: relies on layout of db_taskresult (2 pointers at the end)
		// also note: could leave out 4 bytes of padding too, but then we're
		// making more assumptions about padding so... meh
		n = ibuf_getbytes(b, p->r, sizeof(*p->r) - sizeof(void *) * 2);
		checkread(n, sizeof(*p->r) - sizeof(void *) * 2);
		if (p->r->id >= nexttaskid) nexttaskid = p->r->id + 1;
		const char **infiles = malloc(p->r->ninfiles * sizeof(*p->r->infiles));
		if (!infiles) diemem();
		for (uint i = 0; i < p->r->ninfiles; ++i) {
			n = ibuf_getbytes(b, &idx, sizeof(idx));
			checkread(n, sizeof(idx));
			infiles[i] = strpool_fromidx(idx);
		}
		p->r->infiles = infiles;
		struct task_desc *deps = malloc(p->r->ndeps * sizeof(*deps));
		if (!deps) diemem();
		for (uint i = 0; i < p->r->ndeps; ++i) {
			struct task_desc d;
			struct VEC(const char *) argv = {0};
			for (;;) {
				n = ibuf_getbytes(b, &idx, sizeof(idx));
				checkread(n, sizeof(idx));
				if (idx == -1u) {
					if (!vec_push(&argv, 0)) diemem();
					break;
				}
				else {
					if (!vec_push(&argv, strpool_fromidx(idx))) diemem();
				}
			}
			d.argv = argv.data;
			n = ibuf_getbytes(b, &idx, sizeof(idx));
			checkread(n, sizeof(idx));
			d.workdir = strpool_fromidx(idx);
			deps[i] = d;
		}
		p->r->deps = deps;
	}
	close(fd);
}

struct db_infile *db_getinfile(const char *path) {
	bool isnew;
	struct lookup_infile *l = table_putget_transact_lookup_infile(&infiles,
			path, &isnew);
	if (!l) return 0;
	if (isnew) {
		struct db_infile *i = permalloc_infile();
		if (!i) return 0;
		i->newness = 0;
		// hack(?): other db_infile initialisation is unnecessary -
		// infile_ensure will call update which will fill everything in
		// technically this involves undefined branches but oh well, only
		// valgrind cares
		l->path = path;
		l->i = i;
		table_transactcommit_lookup_infile(&infiles);
	}
	return l->i;
}

struct db_taskresult *db_gettaskresult(struct task_desc desc) {
	bool isnew;
	struct lookup_taskresult *l = table_putget_transact_lookup_taskresult(
			&results, desc, &isnew);
	if (!l) return 0;
	if (isnew) {
		struct db_taskresult *r = permalloc_taskresult();
		if (!r) return 0;
		r->newness = 0;
		r->checked = false;
		r->id = nexttaskid++;
		r->ninfiles = 0;
		r->ndeps = 0;
		r->infiles = 0;
		r->deps = 0;
		l->desc = desc;
		l->r = r;
		table_transactcommit_lookup_taskresult(&results);
	}
	return l->r;
}

static bool needwrite = false;

// these are currently almost no-ops (see TODO(db-opt))
void db_commitinfile(struct db_infile *i) { needwrite = true; }
void db_committaskresult(struct db_taskresult *t) { needwrite = true; }

// NOTE: this function doesn't bother closing files since we're about to exit!
void db_finalise(void) {
	if (!needwrite) return;
	int fd = openat(db_dirfd, "newtables", O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		errmsg_warn(msg_crit, "couldn't open "BUILDDB_DIR"/newtables");
		goto e;
	}
	union {
		struct obuf b;
		char x[sizeof(struct obuf) + 65536];
	} _b;
	struct obuf *b = &_b.b;
	*b = (struct obuf){fd, 65536};
	obuf_putbytes(b, (char *)&infiles.sz, sizeof(infiles.sz));
	uint count = 0;
	// the compiler *should* just turn this into a popcount of each u64
	TABLE_FOREACH_IDX(_, &infiles) ++count;
	obuf_putbytes(b, (char *)&count, sizeof(count));
	TABLE_FOREACH_PTR(p, lookup_infile, &infiles) {
		uint idx = strpool_getidx(p->path);
		if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
		p->i->checked = false; // slightly awkward hack but never mind
		if (!obuf_putbytes(b, (char *)p->i, sizeof(*p->i))) goto e;
	}
	// aand same again for task results
	obuf_putbytes(b, (char *)&results.sz, sizeof(results.sz));
	count = 0;
	TABLE_FOREACH_IDX(_, &results) ++count;
	obuf_putbytes(b, (char *)&count, sizeof(count));
	TABLE_FOREACH_PTR(p, lookup_taskresult, &results) {
		uint idx;
		for (const char *const *pp = p->desc.argv; *pp; ++pp) {
			idx = strpool_getidx(*pp);
			if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
		}
		idx = -1;
		if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
		idx = strpool_getidx(p->desc.workdir);
		if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
		p->r->checked = false;
		// same thing with the - 2 pointers
		if (!obuf_putbytes(b, (char *)p->r, sizeof(*p->r) -
				sizeof(void *) * 2)) {
			goto e;
		}
		for (const char *const *pp = p->r->infiles;
				pp - p->r->infiles < p->r->ninfiles; ++pp) {
			idx = strpool_getidx(*pp);
			if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
		}
		for (const struct task_desc *d = p->r->deps;
				d - p->r->deps < p->r->ndeps; ++d) {
			for (const char *const *pp = d->argv; *pp; ++pp) {
				idx = strpool_getidx(*pp);
				if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
			}
			idx = -1;
			if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
			idx = strpool_getidx(d->workdir);
			if (!obuf_putbytes(b, (char *)&idx, sizeof(idx))) goto e;
		}
	}
	if (!obuf_flush(b)) {
		errmsg_warn(msg_crit, "couldn't write out database file");
		goto e;
	}
	if (renameat(db_dirfd, "newtables", db_dirfd, "tables") == -1) {
		errmsg_warn(msg_crit, "couldn't commit saved database file");
		goto e;
	}

	return;
e:	errmsg_warnx("unnecessary reruns will happen in the future!");
	unlinkat(db_dirfd, "newtables", 0);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
