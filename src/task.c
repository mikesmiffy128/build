#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <alloc.h>
#include <basichashes.h>
#include <errmsg.h>
#include <fmt.h>
#include <intdefs.h>
#include <str.h>
#include <table.h>
#include <vec.h>

#include "blake2b.h"
#include "build.h"
#include "defs.h"
#include "fd.h"
#include "fpath.h"
#include "infile.h"
#include "proc.h"
#include "sigstr.h"
#include "strpool.h"
#include "tui.h"

// there's a bunch of initially weird-looking stuff in this file - if you're
// curious, there's a few insights in DevDocs/taskdb.txt

// there's *also* a bunch of ugly hacks that don't have any explanation beyond
// I'm bad and I'm sorry and I'll clean it up later

#define DBVER 1 // increase if something gets broken!

#define HASHLEN 24
#define IDLEN (HASHLEN * 2)

struct task_desc {
	const char *const *argv;
	const char *workdir;
};

DECL_TABLE(static, infile, const char *, const char *)
DEF_TABLE(static, infile, hash_ptr, table_ideq, table_scalarmemb)

struct vec_int VEC(int);
struct task {
	struct proc_info base; // must be first member; pointer is casted
						   // (if moved, would need something like container_of)
	struct taskresult *resultout; // will write to here when totally done
	struct task_desc desc;
	char id[IDLEN]; // base for filenames and stuff
	bool haderr; // whether to write "* error output from task `blah blah`"
	int fd_out, fd_err; // on-disk files to store output
	struct vec_int fds_sendout; // fds from active dependents receiving output
	uint nblockers;
	/* TODO(basic-core) put task deps in here */
	struct table_infile infiles;
};
DEF_FREELIST(task, struct task, 2048)

struct vec_str VEC(const char *);
struct taskresult {
	struct task_desc desc;
	struct task *task; // 0 if not currently active
	uint newness;
	int fd_out, fd_err;
	uchar status;
	bool haderr;
	/* TODO(basic-core) put task deps in here too */
	struct vec_str infiles;
};
DEF_PERMALLOC(taskresult, struct taskresult, 4096)

DECL_TABLE(static, task_desc_result, const struct task_desc *,
		struct taskresult *)
static uint hash_task_desc(const struct task_desc *d) {
	uint h = HASH_ITER_INIT;
	// NOTE: hashing the actual pointers, since strings are all interned
	for (const char *const *argv = d->argv; *argv; ++argv) {
		h = hash_iter_bytes(h, (const char *)argv, sizeof(*argv));
	}
	return hash_iter_bytes(h, (const char *)&d->workdir, 4);
}
static inline const struct task_desc *task_desc_result_kmemb(
		struct taskresult **r) {
	return &(*r)->desc;
}
DEF_TABLE(static, task_desc_result, hash_task_desc, table_ideq,
		task_desc_result_kmemb)
static struct table_task_desc_result results;

static struct task *goal = 0; // HACK: *super* clumsy way of doing this
static int goalstatus;
static int nstarted = 0;

static void taskid(char out[IDLEN], struct task_desc *d) {
	static const char hextab[16] = "0123456789ABCDEF";
	struct blake2b_state s;
	char buf[HASHLEN];
	blake2b_init(&s, sizeof(buf));
	for (const char *const *argv = d->argv; *argv; ++argv) {
		blake2b_update(&s, *argv, strlen(*argv) + 1); // +1 -> use null as sep.
	}
	blake2b_update(&s, d->workdir, strlen(d->workdir));
	blake2b_final(&s, buf, BLAKE2B_OUTBYTES);
	for (const char *p = buf; p - buf < sizeof(buf); ++p) {
		*out++ = hextab[*(uchar *)p >> 4];
		*out++ = hextab[*p & 15];
	}
}

static struct task *opentask(struct task_desc d) {
	struct task *t = freelist_alloc_task();
	if (t) {
		t->fds_sendout = (struct vec_int){0};
		if (!table_init_infile(&t->infiles)) {
			freelist_free_task(t);
			return 0;
		}
		t->haderr = false;
		t->desc = d; // FIXME (TEMP) ownership of strings...???
		// compute the hash once and store it; we'll need it a few times later
		taskid(t->id, &t->desc);
		char buf[IDLEN + 3];
		memcpy(buf, t->id, IDLEN);
		buf[IDLEN] = ':';
		buf[IDLEN + 1] = 'o';
		buf[IDLEN + 2] = '\0';
		t->fd_out = openat(fd_builddb, buf, O_RDWR | O_CREAT | O_TRUNC |
				O_CLOEXEC, 0644);
		if (t->fd_out == -1) goto e;
		buf[IDLEN + 1] = 'e';
		t->fd_err = openat(fd_builddb, buf, O_RDWR | O_CREAT | O_TRUNC |
				O_CLOEXEC, 0644);
		if (t->fd_err == -1) goto e1;
	}
	return t;

e1:	close(t->fd_out);
e:	free(t->infiles.data); free(t->infiles.flags);
	freelist_free_task(t);
	return 0;
}

static void closetask(struct task *t) {
	for (const int *p = t->fds_sendout.data;
			p - t->fds_sendout.data < t->fds_sendout.sz; ++p) {
		close(*p);
	}
	close(t->fd_out); close(t->fd_err);
	free(t->fds_sendout.data);
	free(t->infiles.data); free(t->infiles.flags);
	freelist_free_task(t);
	if (!--nstarted) {
		// BLEGH. XXX make this less AWFUL.
		infile_done();
		exit(goalstatus);
	}
}

// FIXME this somehow breaks somewhere, figure out why and fix it!
static inline bool doshellesc(struct str *s, const char *p) {
	// if there's shell characters, wrap in single quotes, escaping single
	// quotes specially
	if (strpbrk(p, " \t\n\"'`<>&#~*$|(){};?!\\")) { // FIXME incomplete?
		if (!str_appendc(s, '\'')) return false;
		for (; *p; ++p) {
			if (*p == '\'') {
				if (!str_appendbytes(s, "'\\''", 4)) return false;
			}
			else {
				if (!str_appendc(s, *p)) return false;
			}
		}
		return str_appendc(s, '\'');
	}
	// if there's no special chars just use the string itself directly
	return str_append0t(s, p);
}

static char *desctostr(const struct task_desc *t) {
	int e = errno;
	struct str s = {0};
	if (!str_clear(&s)) return 0;
	if (!doshellesc(&s, *t->argv)) goto e;
	for (const char *const *argv = t->argv + 1; *argv; ++argv) {
		if (!str_appendc(&s, ' ')) goto e;
		if (!doshellesc(&s, *argv)) goto e;
	}
	if (t->workdir[0] != '.' || t->workdir[1]) { // not in base dir
		if (!str_append0t(&s, "` in `")) goto e;
		if (!str_append0t(&s, t->workdir)) goto e;
		if (!str_appendc(&s, '`')) goto e;
	}
	return s.data;
e:	free(s.data);
	errno = e;
	return 0;
}

static inline void block(struct task *t) {
	if (!t->nblockers++) proc_block();
}
static inline void unblock(struct task *t) {
	if (!--(t->nblockers)) proc_unblock(&t->base);
}

static void handle_failure(struct task *t, int status) {
	// TODO(basic-core): propagate failure to dependents, whatever
	// FIXME REALLY BAD: tell proc to ignore this, or we'll get UAF next event
	if (t->haderr) {
		obuf_put0t(buf_err, "* task's error output prior to failing:\n");
		obuf_flush(buf_err);
		obuf_reset(buf_err);
		fd_transferall(t->fd_err, 2);
	}
	if (t == goal) goalstatus = status; // yuck
	closetask(t);
	++tui_nfailed;
}

// this is split out from proc_cb below, which got slightly very unwieldy
// XXX that function still needs a significant cleanup
static bool writeresult(const char *fname, uchar status, const struct task *t) {
	int fd = openat(fd_builddb, fname, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	struct obuf *b = OBUF(fd, 65536); // FIXME: another lazy and slow BUF()
	if (!obuf_putc(b, status)) goto e;
	if (!obuf_putbytes(b, (const char *)&newness, sizeof(newness))) goto e;
	for (const char *const *pp = t->resultout->infiles.data;
			pp - t->resultout->infiles.data < t->resultout->infiles.sz; ++pp) {
		if (!obuf_put0t(b, *pp) || !obuf_putc(b, '\0')) goto e;
	}
	if (!obuf_flush(b)) goto e;
	close(fd);
	return true;
e:	close(fd);
	return false;
}

static void proc_cb(int evtype, union proc_ev_param P, struct proc_info *proc) {
	struct task *t = (struct task *)proc;
	switch (evtype) {
		case PROC_EV_STDOUT:
			if (!fd_writeall(t->fd_out, P.buf, P.sz)) goto fail;
			for (const int *p = t->fds_sendout.data;
					p - t->fds_sendout.data < t->fds_sendout.sz; ++p) {
				if (!fd_writeall(*p, P.buf, P.sz)) goto fail;
			}
			break;
		case PROC_EV_STDERR:
			t->haderr = true;
			if (!fd_writeall(t->fd_err, P.buf, P.sz)) goto fail;
			break;
		case PROC_EV_EXIT:
			if (WIFEXITED(P.status)) {
				if (WEXITSTATUS(P.status) < 100) {
					char *s = desctostr(&t->desc);
					if (t->nblockers) {
						errmsg_warnx(msg_crit, "task `", s, "` finished while "
								"supposedly blocked - fix your code!");
					}
					char buf[IDLEN + 3];
					char buf2[IDLEN + 3];
					memcpy(buf, t->id, IDLEN);
					buf[IDLEN] = ':';
					buf[IDLEN + 1] = 'R';
					buf[IDLEN + 2] = '\0';
					unlink(buf); // can't make this fully atomic, oh well
					buf[IDLEN + 1] = 'o';
					memcpy(buf2, t->id, IDLEN);
					buf2[IDLEN] = ':';
					buf2[IDLEN + 1] = 'O';
					buf2[IDLEN + 2] = '\0';
					if (renameat(fd_builddb, buf, fd_builddb, buf2) == -1) {
						goto badfs;
					}
					buf[IDLEN + 1] = 'e';
					buf2[IDLEN + 1] = 'E';
					if (renameat(fd_builddb, buf, fd_builddb, buf2) == -1) {
						// XXX this should basically NEVER fail but wouldn't
						// hurt to still try to be more robust
badfs:					errmsg_die(200, msg_fatal, "couldn't save task result");
					}
					if (t->haderr) {
						obuf_put0t(buf_err, "* error output from task `");
						obuf_put0t(buf_err, s);
						obuf_put0t(buf_err, "`:\n");
						obuf_flush(buf_err);
						obuf_reset(buf_err);
						fd_transferall(t->fd_err, 2);
					}
					// TODO(basic-core): send out the status, unblock things, whatever
					close(t->resultout->fd_out);
					t->resultout->fd_out = t->fd_err;
					close(t->resultout->fd_err);
					t->resultout->fd_err = t->fd_err;
					t->resultout->status = WEXITSTATUS(P.status);
					t->resultout->newness = newness;
					TABLE_FOREACH_PTR(i, infile, &t->infiles) {
						if (!vec_push(&t->resultout->infiles, *i)) goto nosave;
					}
					buf[IDLEN + 1] = 'R';
					int fd = openat(fd_builddb, buf, O_RDWR | O_CREAT |
							O_CLOEXEC, 0644);
					if (!writeresult(buf, WEXITSTATUS(P.status), t)) {
						unlinkat(fd_builddb, buf, 0);
nosave:					errmsg_warn(msg_error,
								"couldn't save result of task `", s, "`");
						errmsg_warnx(msg_note,
								"unnecessary reruns will happen in the future");
					}
					free(s);
					if (t == goal) goalstatus = WEXITSTATUS(P.status); // yuck 2
					closetask(t);
					++tui_ndone;
				}
				else {
					char *s = desctostr(&t->desc);
					char buf[4];
					// buf + 0 to dodge the [static] - status has <= 3 digits
					buf[fmt_fixed_u32(buf + 0, WEXITSTATUS(P.status))] = '\0';
					errmsg_warnx(msg_error, "task `", s,
							"` failed with abnormal status ", buf);
					free(s);
					handle_failure(t, WEXITSTATUS(P.status));
				}
			}
			else if (WIFSIGNALED(P.status)) {
				char *s = desctostr(&t->desc);
				errmsg_warnx(msg_error, "task `", s, "` was killed by SIG",
						sigstr(WTERMSIG(P.status)));
				free(s);
				handle_failure(t, 128 + WTERMSIG(P.status));
			}
			// just ignore process suspension
			break;
		case PROC_EV_UNBLOCK:
			// TODO(basic-core): IPC stuff here
			break;
		case PROC_EV_ERROR:
fail:;		char *s = desctostr(&t->desc);
			errmsg_warn(msg_error, "couldn't process task `", s, "`");
			free(s);
			handle_failure(t, 100);
			break;
	}
}

static bool savesymstr(const char *name, const char *val) {
	// create a new symlink and rename it to atomically replace the old one
	// NOTE: this could break if someone is poking around .builddb at the same
	// time. adequate solution: don't do that.
	unlinkat(fd_builddb, "symnew", 0); // in case it was left due to a crash
	if (symlinkat(val, fd_builddb, "symnew") == -1) return false;
	if (renameat(fd_builddb, "symnew", fd_builddb, name) == -1) {
		unlinkat(fd_builddb, "symnew", 0);
		return false;
	}
	return true;
}

static bool loadsymstr(const char *name, char *buf, uint sz) {
	long ret = readlinkat(fd_builddb, name, buf, sz);
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

void task_init(void) {
	// XXX this error handling is far from perfect in terms of simplicity
	const char *errstr = 0;
	errno = 0;
	int dbversion = loadsymnum("version", &errstr); // ignore overflow, shrug
	if (errno == ENOENT) {
		dbversion = DBVER;
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
	newness = loadsymnum("newness", &errstr); // ignore overflow, shrug again
	if (errno == EINVAL) {
		errmsg_diex(1, msg_fatal, "global task newness value is ", errstr);
	}
	else if (errno && errno != ENOENT) {
		errmsg_die(100, msg_fatal, "couldn't read task database version");
	}
	// bump newness immediately: if we crash, later rebuilds won't be prevented
	if (!savesymnum("newness", newness + 1)) {
		errmsg_die(100, msg_fatal, "couldn't update task database");
	}
	if (!table_init_task_desc_result(&results)) {
		errmsg_die(100, msg_fatal, "couldn't allocate task result table");
	}
	infile_init();
	proc_init(&proc_cb);
}

static bool reqinfile(struct task *t, const char *infile) {
	bool isnew;
	const char **pp = table_putget_infile(&t->infiles, infile, &isnew);
	if (!pp) return false;
	if (isnew) {
		*pp = infile;
		if (!infile_ensure(infile)) {
			// FIXME: do we need to do anything else to back this out?
			table_delptr(&t->infiles, pp);
			return false;
		}
	}
	return true;
}

// this one is REALLY hacked together
static int loadresult(struct taskresult *r) {
	char buf[IDLEN + 3];
	taskid(buf, &r->desc);
	buf[IDLEN] = ':';
	buf[IDLEN + 1] = 'R';
	buf[IDLEN + 2] = '\0';
	int fd = openat(fd_builddb, buf, O_RDONLY | O_CLOEXEC);
	if (fd == -1) return errno == ENOENT ? 1 : -1;
	struct ibuf *b = IBUF(fd, 65536); // FIXME: IBUF() zeros out, which is slow!
	short status = ibuf_getc(b);
	if (status == -1) goto e;
	if (status == IOBUF_EOF) { errno = EINVAL; goto e; } // blegh
	r->status = status;
	int nread = ibuf_getbytes(b, &r->newness, sizeof(r->newness));
	if (nread == -1) goto e;
	if (nread != sizeof(r->newness)) { errno = EINVAL; goto e; }
	r->infiles = (struct vec_str){0};
	struct str s;
	for (;;) {
		s.data = 0;
		s.max = 0;
		if (!str_clear(&s)) goto e;
		nread = ibuf_getstr(b, &s, '\0');
		if (nread == -1) goto e;
		if (nread == 0) break; // clean EOF!
		if (s.data[str_len(&s) - 1] != '\0') { errno = EINVAL; goto e1; }
		--s.sz; // we've read in an extra null, so just pull back length
		if (str_len(&s) == 0) { errno = EINVAL; goto e1; }
		const char *intern = strpool_move(s.data);
		if (!intern) goto e1;
		if (!vec_push(&r->infiles, intern)) goto e1;
	}
	buf[IDLEN + 1] = 'O';
	// for now, O_CREAT should remove the need to deal with missing files
	r->fd_out = openat(fd_builddb, buf, O_RDONLY | O_CREAT | O_CLOEXEC, 0644);
	if (r->fd_out == -1) { close(fd); goto e; }
	buf[IDLEN + 1] = 'E';
	r->fd_err = openat(fd_builddb, buf, O_RDONLY | O_CREAT | O_CLOEXEC, 0644);
	if (r->fd_err == -1) { close(r->fd_out); close(fd); goto e; }
	struct stat st;
	fstat(r->fd_err, &st);
	r->haderr = st.st_size != 0;
	return 0;

e1:	free(s.data);
e:	free(r->infiles.data);
	close (fd);
	return -1;
}

static void requseold(struct task_desc d, const struct taskresult *r,
		int fd_sendout, bool isgoal) {
	fd_transferall(r->fd_out, fd_sendout); // FIXME handle error here
	if (r->haderr) {
		// XXX very bad, very smelly, code dupe from above
		char *s = desctostr(&d);
		obuf_put0t(buf_err, "* error output from task `");
		obuf_put0t(buf_err, s);
		free(s);
		obuf_put0t(buf_err, "`:\n");
		obuf_flush(buf_err);
		obuf_reset(buf_err);
		fd_transferall(r->fd_err, 2);
	}
	if (isgoal) exit(r->status); // UGH
}

static void reqredo(struct task_desc d, struct taskresult *r, int fd_sendout,
		bool isgoal) {
	struct task *t = opentask(d);
	char *s;
	if (!t) {
		s = desctostr(&d);
		errmsg_warn(msg_error, "couldn't set up task `", s, "` to run");
		goto er;
	}
	r->task = t;
	t->resultout = r;
	if (!vec_push(&t->fds_sendout, fd_sendout)) {
		errmsg_warn(msg_error, "couldn't connect task output");
		// TODO(basic-core)/FIXME: make the *requesting* task fail (??)
	}
	if (isgoal) goal = t;
	// TODO(basic-core): *else* block tasks etc
	if (strchr(d.argv[0], '/')) { // not in $PATH
		char basepath[PATH_MAX];
		char *p = memccpy(basepath, d.workdir, '\0', sizeof(basepath));
		if (!p) { errno = ENAMETOOLONG; goto ei; } // just in case
		p[-1] = '/';
		p = memccpy(p, d.argv[0], '\0', sizeof(basepath) - (p - basepath));
		if (!p) { errno = ENAMETOOLONG; goto ei; }
		char canon[PATH_MAX];
		int err = fpath_canon(basepath, canon, 0);
		if (err == FPATH_OK) { // XXX otherwise????
			const char *infile = strpool_copy(canon);
			if (!infile || !reqinfile(t, infile)) goto ei;
		}
	}
	proc_start(&t->base, t->desc.argv, t->desc.workdir);
	++nstarted;
	return;

ei:	s = desctostr(&t->desc);
	errmsg_warn(msg_error, "couldn't add infile to task `", s, "`");
er:	free(s);
	handle_failure(t, 100);
	return;
}

// TODO(basic-core): note: prolly wanna change bool to the dependent pointer and
// use null for goal
static void req(struct task_desc d, int fd_sendout, bool isgoal) {
	bool isnew;
	struct taskresult **p = table_putget_task_desc_result(&results, &d, &isnew);
	if (!p) {
		char *s = desctostr(&d);
		errmsg_warn(msg_error, "couldn't put task `", s, "` in table");
		free(s);
		if (isgoal) exit(100);
		return; // TODO(basic-core)/FIXME: send failure to other tasks
	}
	bool shouldredo = false;
	if (isnew) {
		struct taskresult *r = permalloc_taskresult();
		if (!r) {
			errmsg_warn(msg_error, "couldn't allocate task results entry");
			table_delptr(&results, p);
			if (isgoal) exit(100);
			return; // TODO(basic-core)/FIXME: same again
		}
		*p = r;
		r->desc = d;
		r->task = 0;
		int ret = loadresult(r); // -1 on error, 0 on load, 1 on no file
		if (ret == -1) {
			char *s = desctostr(&d);
			errmsg_warn(msg_error, "couldn't look up existing task `", s, "`");
			errmsg_warnx(msg_note,
					"potentially-unnecessary task reruns will occur");
			free(s);
			shouldredo = true;
		}
		else if (ret == 0) {
			// TODO(basic-core): check deps before infiles!
			for (const char *const *p = r->infiles.data;
					p - r->infiles.data < r->infiles.sz; ++p) {
				uint ifnewness = infile_query(*p, r->newness);
				if (ifnewness > r->newness) {
					shouldredo = true;
					break;
				}
			}
		}
		else {
			shouldredo = true;
		}
	}
	else if ((*p)->task) {
		// TODO(basic-core): block requesting task, hook up to request*ed* task
		return;
	}

	if (shouldredo) reqredo(d, *p, fd_sendout, isgoal);
	else requseold(d, *p, fd_sendout, isgoal);
}

void task_goal(const char *const *argv, const char *workdir) {
	req((struct task_desc){argv, workdir}, 1, true);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
