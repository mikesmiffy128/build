#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <alloc.h>
#include <basichashes.h>
#include <errmsg.h>
#include <intdefs.h>
#include <iobuf.h>
#include <str.h>
#include <table.h>

#include "build.h"
#include "strpool.h"
#include "unreachable.h"

// this code is pretty tightly bound to task.c, really, it's just split out to
// keep things a tiny bit more neat. check out both that file and the docs in
// DevDocs/{taskdb,infile}.txt for insights.

struct infile {
	uvlong len; // if nonexistent, this is -1 and the rest is undefined
	// otherwise:
	uvlong inode;
	u32 mode;
	uid_t uid; gid_t gid;
	uint newness; // down here for padding reasons
};

struct infile_lookup {
	const char *fname;
	struct infile *infile;
	// XXX: save 7 bytes here via some pointer bithacking
	bool checked;
};

DEF_PERMALLOC(infile, struct infile, 1024)

DECL_TABLE(static, path_infile, const char *, struct infile_lookup)
static inline const char *infile_lookup_kmemb(const struct infile_lookup *ifl) {
	return ifl->fname;
}
DEF_TABLE(static, path_infile, hash_ptr, table_ideq, infile_lookup_kmemb)
struct table_path_infile infiles;

void infile_init(void) {
	if (!table_init_path_infile(&infiles)) {
		errmsg_die(100, msg_fatal, "couldn't allocate infiles table");
	}
	int fd = openat(fd_builddb, "infiles", O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		if (errno == ENOENT) return; // first run, it's fine
		errmsg_die(100, msg_fatal, "couldn't open infiles list");
	}
	struct str s;
	struct ibuf *b = IBUF(fd, 65536);
	for (;;) {
		s.data = 0;
		s.max = 0;
		if (!str_clear(&s)) {
			errmsg_die(100, msg_fatal, "couldn't allocate memory");
		}
		int nread = ibuf_getstr(b, &s, '\0');
		if (nread == -1) goto e;
		if (nread == 0) break; // clean EOF!
		if (s.data[str_len(&s) - 1] != '\0') {
			errmsg_diex(1, msg_fatal, "invalid infiles list: ",
					"unexpected EOF in place of string");
		}
		--s.sz; // we've read in an extra null, so just pull back length
		if (str_len(&s) == 0) {
			errmsg_diex(1, msg_fatal, "invalid infiles list: ",
					"unexpected empty string");
		}
		struct infile *i = permalloc_infile();
		if (!i) errmsg_die(100, msg_fatal, "couldn't allocate memory");
		nread = ibuf_getbytes(b, i, sizeof(*i));
		if (nread == -1) {
e:			errmsg_die(100, msg_fatal, "couldn't read infiles list");
		}
		if (nread == 0) {
			errmsg_diex(1, msg_fatal, "invalid infiles list: ",
					"unexpected EOF in place of infile data");
		}
		const char *intern = strpool_move(s.data);
		if (!intern) errmsg_die(100, msg_fatal, "couldn't intern string");
		struct infile_lookup *ifl = table_put_path_infile(&infiles, intern);
		if (!ifl) errmsg_die(100, msg_fatal, "couldn't create table entry");
		ifl->fname = intern;
		ifl->infile = i;
	}
}

void infile_done(void) {
	int fd = openat(fd_builddb, "newinfiles", O_RDWR | O_CREAT | O_TRUNC |
			O_CLOEXEC, 0644);
	if (fd == -1) {
		errmsg_warn(msg_crit, "couldn't create new infiles list");
		goto e;
	}
	struct obuf *b = OBUF(fd, 65536);
	for (uint i = 0; i < infiles.sz; ++i) {
		// XXX table.h should have an iteration abstraction, doing internal
		// hackery for now
		if (_table_ispresent(infiles.flags, i)) {
			struct infile_lookup *ifl = infiles.data + i;
			if (!obuf_put0t(b, ifl->fname) || !obuf_putc(b, '\0') ||
					!obuf_putbytes(b, (char *)ifl->infile,
						sizeof(*ifl->infile))) {
				errmsg_warn(msg_crit, "couldn't write to infiles list");
				goto e;
			}
		}
	}
	if (!obuf_flush(b)) {
		errmsg_warn(msg_crit, "couldn't finish writing infiles list");
	}
	if (renameat(fd_builddb, "newinfiles", fd_builddb, "infiles") == -1) {
		errmsg_warn(msg_crit, "couldn't commit infiles list");
		goto e;
	}
	goto r;

e:	errmsg_warnx(msg_note, "there may be unnecessary reruns in the future");
r:	close(fd);
	return;
}

static int lookup(const char *path, struct infile *data) {
	struct stat s;
	if (stat(path, &s) == -1) {
		if (errno != ENOENT || errno == EACCES) return -1;
		if (data->len == -1ull) return 0; // no change
		data->len = -1;
		return 1;
	}
	bool diff = false;
	if (s.st_size != data->len  ) { diff = true; data->len   = s.st_size; }
	if (s.st_ino  != data->inode) { diff = true; data->inode = s.st_ino;  }
	if (s.st_mode != data->mode ) { diff = true; data->mode  = s.st_mode; }
	if (s.st_uid  != data->uid  ) { diff = true; data->uid   = s.st_uid;  }
	if (s.st_gid  != data->gid  ) { diff = true; data->gid   = s.st_gid;  }
	return diff;
}

bool infile_ensure(const char *path) {
	bool isnew;
	struct infile_lookup *ifl = table_putget_path_infile(&infiles, path, &isnew);
	if (!ifl) return false;
	if (isnew) {
		ifl->fname = path; // FIXME string ownership again (strpool...)
		// first ever lookup, create a reference entry for later comparisons
		struct infile *i = permalloc_infile();
		i->newness = 0;
		if (!i) {
			// XXX table.h could also use a delete-by-pointer option, I guess?
			table_del_path_infile(&infiles, path);
			return false;
		}
		ifl->infile = i;
		if (lookup(path, i) == -1) {
			// TODO(basic-core)/FIXME: handle error! crucially important!
			errmsg_die(100, msg_fatal, "couldn't create infile entry");
		}
	}
	return true;
}

uint infile_query(const char *path, uint tgtnewness) {
	struct infile_lookup *ifl = table_get_path_infile(&infiles, path);
	if (!ifl) { errno = EINVAL; return -1u; } // should never happen
	if (ifl->checked || ifl->infile->newness > tgtnewness) {
		return ifl->infile->newness;
	}
	switch (lookup(path, ifl->infile)) {
		case -1: return -1u;
		case 1: ifl->infile->newness = newness;
		case 0: return ifl->infile->newness;
	}
	unreachable;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
