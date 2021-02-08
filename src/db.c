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
#include <table.h>

#include "db.h"
#include "defs.h"
#include "tableshared.h"

// TODO(db-opt): this whole file :)

// from db-strpool.c (no header because who cares)
void strpool_init(void);
const ulong strpool_fileid(const char *s);

int db_dirfd;
uint db_newness = 1; // start at 1 (as 0 is used for newly-created entries)
static uint nexttaskid = 0; // just a serial number

#define DBVER 1 // increase if something gets broken!

static bool savesymstr(const char *name, const char *val) {
	// create a new symlink and rename it to atomically replace the old one
	// NOTE: this could break if someone is poking around .builddb at the same
	// time. adequate solution: don't do that.
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
	char buf[PATH_MAX];
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

void db_init(void) {
	if (!table_init_lookup_infile(&infiles) ||
			!table_init_lookup_taskresult(&results)) {
		errmsg_die(100, msg_fatal, "couldn't allocate hashtable");
	}
	if (mkdir(BUILDDB_DIR, 0755) == -1 && errno != EEXIST) {
		errmsg_die(100, msg_fatal, "couldn't create .builddb directory");
	}
	db_dirfd = open(BUILDDB_DIR, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	if (db_dirfd == -1) {
		errmsg_die(100, msg_fatal, "couldn't open .builddb directory");
	}
	// XXX this error handling is far from perfect in terms of simplicity
	const char *errstr = 0;
	errno = 0;
	int dbversion = loadsymnum("version", &errstr); // ignore overflow, shrug
	if (errno == ENOENT) {
		if (!savesymnum("version", DBVER)) {
			errmsg_die(100, msg_fatal, "couldn't create task database");
		}
	}
	else if (errno == EINVAL) {
		// status 1: user has messed with something, not our fault!
		errmsg_diex(1, msg_fatal, "task database version is ", errstr);
	}
	else if (errno) {
		errmsg_die(100, msg_fatal, "couldn't read task database version");
	}
	else if (dbversion != DBVER) {
		// can add migration or something later if we actually bump the
		// version, but this is fine for now
		errmsg_diex(1, msg_fatal, "unsupported task database version; "
				"try rm -rf .builddb/");
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

	// -- FIXME: LOAD STUFF HERE --
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
		r->id = nexttaskid++;
		l->desc = desc;
		l->r = r;
		table_transactcommit_lookup_taskresult(&results);
	}
	return l->r;
}

// these are currently no-ops (see TODO(db-opt))
void db_commitinfile(struct db_infile *i) {}
void db_committaskresult(struct db_taskresult *t) {}

void db_finalise(void) {
	// -- FIXME: SAVE STUFF HERE --
}

// vi: sw=4 ts=4 noet tw=80 cc=80
