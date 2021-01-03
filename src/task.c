#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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
#include "defs.h"
#include "proc.h"
#include "sigstr.h"

// there's a bunch of initially weird-looking stuff in this file - if you're
// curious, there's a few insights in DevDocs/taskdb.txt

static uint newness = 0;

#define DBVER 1 // increase if something gets broken!

static char pathbuf[PATH_MAX] = BUILDDB_DIR "/";
static char pathbuf2[PATH_MAX] = BUILDDB_DIR "/";

static void symcat(const char *name) {
	char *p = pathbuf + sizeof(BUILDDB_DIR "/") - 1;
	for (;;) {
		*p = *name;
		if (!*name) break;
		++name; ++p;
		if (p - pathbuf == sizeof(pathbuf) - 1) {
			errmsg_diex(210, "task: symstr key is too long (this is a bug!)");
		}
	}
}

static bool savesymstr(const char *name, const char *val) {
	// We create a new symlink and rename it to atomically replace the old one.
	// NOTE: this doesn't bother picking a random new name as it assumes nobody
	// else is poking around .builddb in parallel with build. Doing that could
	// break all kinds of things.
	unlink(BUILDDB_DIR "/symnew"); // unlink in case it was left due to a crash
	if (symlink(val, BUILDDB_DIR "/symnew") == -1) return false;
	symcat(name);
	if (rename(BUILDDB_DIR "/symnew", pathbuf) == -1) {
		unlink(BUILDDB_DIR "/symnew");
		return false;
	}
	return true;
}

static bool loadsymstr(const char *name, char *buf, uint sz) {
	symcat(name);
	// note: if symlink manages to somehow be longer than PATH_MAX, it'll get
	// truncated - this should never happen though since we're limited to
	// PATH_MAX for symlink()
	long ret = readlink(pathbuf, buf, sz);
	if (ret == -1) return false;
	buf[ret] = '\0';
	return true;
}

static vlong loadsymnum(const char *name, const char **errstr) {
	char buf[PATH_MAX];
	if (!loadsymstr(name, buf, sizeof(buf))) {
		*errstr = ""; // XXX this is really really stupid and makes me mad
		return 0;
	}
	vlong ret = strtonum(buf, LLONG_MIN, LLONG_MAX, errstr);
	return ret;
}

static bool savesymnum(const char *name, vlong val) {
	char buf[21];
	buf[fmt_fixed_s64(buf, val)] = '\0';
	return savesymstr(name, buf);
}

struct task_desc {
	const char *const *argv;
	const char *workdir;
};

#define IDLEN 128

DECL_TABLE(static, infile, const char *, const char *)
static inline bool infile_eq(const char *a, const char *b) {
	return !strcmp(a, b);
}
DEF_TABLE(static, infile, hash_str, infile_eq, table_scalarmemb)

struct vec_int VEC(int);
/* a currently-running task */
struct task {
	struct proc_info base; // must be first member; pointer is casted
						   // (if moved, would need something like container_of)
	struct task_desc desc;
	char id[IDLEN]; // base for filenames and stuff
	bool haderr; // whether to write "* error output from task `blah blah`"
	int fd_out, fd_err; // on-disk files to store output
	struct vec_int fds_sendout; // fds from active dependents receiving output
	uint nblockers;
	/* TODO(basic-core) put task deps in here */
	struct table_infile infiles;
};

DEF_FREELIST(task, struct task, 1024)

static void taskid(char out[static IDLEN], struct task_desc *d) {
	static const char hextab[16] = "0123456789ABCDEF";
	struct blake2b_state s;
	char buf[BLAKE2B_OUTBYTES];
	blake2b_init(&s, BLAKE2B_OUTBYTES);
	for (const char *const *argv = d->argv; *argv; ++argv) {
		blake2b_update(&s, *argv, strlen(*argv) + 1); // +1 -> use null as sep.
	}
	blake2b_update(&s, d->workdir, strlen(d->workdir));
	blake2b_final(&s, buf, BLAKE2B_OUTBYTES);
	for (const char *p = buf; p - buf < BLAKE2B_OUTBYTES; ++p) {
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
		// XXX we assume PATH_MAX is *vaguely* reasonable here; if not, bad news
		memcpy(pathbuf + sizeof(BUILDDB_DIR "/") - 1, t->id, IDLEN);
		pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN] = ':';
		pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 1] = 'o';
		pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 2] = '\0';
		t->fd_out = open(pathbuf, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (t->fd_out == -1) goto e;
		pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 1] = 'e';
		t->fd_err = open(pathbuf, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
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
}

/* a finished task in the database */
struct task_result {
	struct task_desc desc;
	uchar status;
	/* TODO(basic-core) put task deps in here too */
	struct VEC(const char *) infiles;
	int fd_out, fd_err;
};

DECL_TABLE(static, task_desc_result, const struct task_desc *,
		struct task_result *)
static uint hash_task_desc(const struct task_desc *d) {
	uint h = HASH_ITER_INIT;
	for (const char *const *argv = d->argv; *argv; ++argv) {
		h = hash_iter_str(h, *argv);
		h = hash_iter(h, '\0');
	}
	return hash_iter_str(h, d->workdir);
}
static inline const struct task_desc *task_result_kmemb(
		struct task_result **r) {
	return &(*r)->desc;
}
DEF_TABLE(static, task_desc_result, hash_task_desc, table_ideq,
		task_result_kmemb)

static bool writeall(int fd, const char *buf, uint sz) {
	uint off = 0;
	while (sz) {
		long nwritten = write(fd, buf + off, sz);
		if (nwritten == -1) {
			if (errno == EINTR) continue;
			return false;
		}
		off += nwritten; sz -= nwritten;
	}
	return true;
}

static bool transferall(int diskf, int to) {
	char buf[16386];
	uint off = 0, foff = 0;
	long nread;
	while (nread = pread(diskf, buf + off, sizeof(buf) - off, foff)) {
		if (nread == -1) {
			if (errno == EINTR) continue;
			return false;
		}
		off += nread; foff += nread;
		if (off == sizeof(buf)) {
			if (!writeall(to, buf, off)) return false;
			off = 0;
		}
	}
	return !off || writeall(to, buf, off);
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
	struct str s = {0};
	if (!str_clear(&s)) return 0;
	if (!doshellesc(&s, *t->argv)) goto e;
	for (const char *const *argv = t->argv + 1; *argv; ++argv) {
		if (!str_appendc(&s, ' ')) goto e;
		if (!doshellesc(&s, *argv)) goto e;
	}
	if (t->workdir[0] != '.' || t->workdir[1]) { // not in base dir
		if (!str_append0t(&s, " in `")) goto e;
		if (!str_append0t(&s, t->workdir)) goto e;
		if (!str_appendc(&s, '`')) goto e;
	}
	return s.data;
e:	free(s.data);
	return 0;
}

static void req(struct task_desc d, int fd_sendout) {
	// TODO(basic-core): lookup DB, only continue to run if not present/UTD!
	struct task *t = opentask(d);
	if (!t) {
		// TODO(basic-core)/FIXME: appropriate error handling here
		errmsg_warn("couldn't setup task");
		return;
	}
	proc_start(&t->base, t->desc.argv, t->desc.workdir);
}

static inline void block(struct task *t) {
	if (!t->nblockers++) proc_block();
}
static inline void unblock(struct task *t) {
	if (!--(t->nblockers)) proc_unblock(&t->base);
}

static void handle_failure(struct task *t) {
	// TODO(basic-core): propagate failure to dependents, whatever
	// FIXME REALLY BAD: tell proc to ignore this, or we'll get UAF on cb
	if (t->haderr) {
		obuf_put0t(buf_err, "* before failing, the task gave error output:");
		obuf_flush(buf_err);
		obuf_reset(buf_err);
		transferall(t->fd_err, 2);
	}
	closetask(t);
}

static void fail_err(struct task *t) {
	int e = errno;
	char *s = desctostr(&t->desc);
	errno = e; // if describe fails, welp, too bad
	errmsg_warn(msg_error, "couldn't process task `", s, "`");
	free(s);
	handle_failure(t);
}

static void fail_sig(struct task *t, int sig) {
	char *s = desctostr(&t->desc);
	errmsg_warnx(msg_error, "task `", s, "` was killed by SIG", sigstr(sig));
	free(s);
	handle_failure(t);
}

static void fail_badstatus(struct task *t, int status) {
	char *s = desctostr(&t->desc);
	char buf[4];
	// buf + 0 to dodge the buffer size warning (status can only have 3 digits)
	buf[fmt_fixed_u32(buf + 0, status)] = '\0';
	errmsg_warnx(msg_error, "task `", s, "` failed with abnormal status ", buf);
	free(s);
	handle_failure(t);
}

static void done(struct task *t, int status) {
	char *s = desctostr(&t->desc);
	if (t->nblockers) {
		errmsg_warnx(msg_crit, "task `", s, "` finished while supposedly "
				"blocked - fix your code!");
	}
	memcpy(pathbuf + sizeof(BUILDDB_DIR "/") - 1, t->id, IDLEN);
	pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN] = ':';
	pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 1] = 'o';
	pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 2] = '\0';
	memcpy(pathbuf2 + sizeof(BUILDDB_DIR "/") - 1, t->id, IDLEN);
	pathbuf2[sizeof(BUILDDB_DIR "/") - 1 + IDLEN] = ':';
	pathbuf2[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 1] = 'O';
	pathbuf2[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 2] = '\0';
	if (rename(pathbuf, pathbuf2) == -1) goto badfs;
	pathbuf[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 1] = 'e';
	pathbuf2[sizeof(BUILDDB_DIR "/") - 1 + IDLEN + 1] = 'E';
	if (rename(pathbuf, pathbuf2) == -1) {
		// XXX *apparently* this can only fail in absolutely catastrophic
		// circumstances (i.e. the filesystem implementation is missing), but
		// a less lazy me could *still* try to make it more robust *just in
		// case* - it's a question of *how* though
badfs:	errmsg_die(200, msg_fatal, "could not commit task result");
	}
	if (t->haderr) {
		obuf_put0t(buf_err, "* error output from task `");
		obuf_put0t(buf_err, s);
		obuf_put0t(buf_err, "`:");
		obuf_flush(buf_err);
		obuf_reset(buf_err);
		transferall(t->fd_err, 2);
	}
	// TODO(basic-core): send out the status, unblock things, whatever
	free(s);
	closetask(t);
}

static void proc_cb(int evtype, union proc_ev_param P, struct proc_info *proc) {
	struct task *t = (struct task *)proc;
	switch (evtype) {
		case PROC_EV_STDOUT:
			if (!writeall(t->fd_out, P.buf, P.sz)) { fail_err(t); return; }
			for (const int *p = t->fds_sendout.data;
					p - t->fds_sendout.data < t->fds_sendout.sz; ++p) {
				if (!writeall(*p, P.buf, P.sz)) { fail_err(t); return; }
			}
			break;
		case PROC_EV_STDERR:
			t->haderr = true;
			if (!writeall(t->fd_err, P.buf, P.sz)) fail_err(t);
			break;
		case PROC_EV_EXIT:
			if (WIFEXITED(P.status)) {
				if (WEXITSTATUS(P.status) < 100) done(t, WEXITSTATUS(P.status));
				else fail_badstatus(t, WEXITSTATUS(P.status));
			}
			else if (WIFSIGNALED(P.status)) {
				fail_sig(t, WTERMSIG(P.status));
			}
			break;
		case PROC_EV_UNBLOCK:
			// TODO(basic-core): IPC stuff here
			break;
		case PROC_EV_ERROR:
			fail_err(t);
			break;
	}
}

void task_init(int maxparallel) {
	const char *errstr = 0;
	// this can overflow, but who cares
	int dbversion = loadsymnum("version", &errstr);
	// XXX this error handling is a convoluted spider web full of spiders
	if (errstr) {
		if (errno == ENOENT) {
			dbversion = DBVER;
			if (!savesymnum("version", DBVER)) {
				errmsg_die(100, msg_fatal, "couldn't create task database");
			}
		}
		else if (errno == ERANGE || errno == EINVAL) {
			// this can only actually happen if the user has boundary issues
			// and messes with stuff for no reason
			errmsg_diex(1, msg_fatal, "task database version is ", errstr);
		}
		else {
			errmsg_die(100, msg_fatal, "couldn't read task database version");
		}
	}
	else if (dbversion != DBVER) {
		// can add migration or something later if we actually bump the
		// version, but this is fine for now
		errmsg_diex(1, msg_fatal, "unsupported task database version; "
				"try rm -rf .builddb/");
	}
	errstr = 0;
	// this can overflow again, but again just assume it's not an issue
	// (nobody's gonna run 4 billion builds. surely. right?)
	newness = loadsymnum("newness", &errstr);
	if (errstr) {
		if (errno == ERANGE || errno == EINVAL) {
			errmsg_diex(1, msg_fatal, "global task newness value is ", errstr);
		}
		else if (errno != ENOENT) {
			errmsg_die(100, "couldn't read task database version");
		}
	}
	// we want to *immediately* increase newness before building anything, this
	// way if there's a crash or something it won't wreck the build state
	if (!savesymnum("newness", newness + 1)) {
		errmsg_die(100, msg_fatal, "couldn't update task database");
	}
	proc_init(maxparallel, &proc_cb);
}

void task_goal(const char *const *argv, const char *workdir) {
	req((struct task_desc){argv, workdir}, 1);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
