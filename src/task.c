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
#include <noreturn.h>
#include <str.h>
#include <table.h>
#include <vec.h>

#include "blake2b.h"
#include "build.h"
#include "db.h"
#include "defs.h"
#include "fd.h"
#include "fmt.h"
#include "fpath.h"
#include "infile.h"
#include "ipcserver.h"
#include "proc.h"
#include "sigstr.h"
#include "tableshared.h"
#include "tui.h"

DECL_TABLE(static, infile, const char *, const char *)
DEF_TABLE(static, infile, hash_ptr, table_ideq, table_scalarmemb)

DECL_TABLE(static, taskdesc, struct task_desc, struct task_desc)
DEF_TABLE(static, taskdesc, hash_task_desc, eq_task_desc, table_scalarmemb)
struct vec_int VEC(int);

struct task;
struct vec_taskp VEC(struct task *);
struct vec_task_desc VEC(struct task_desc);
struct vec_str VEC(const char *);

struct task {
	struct proc_info base; // must be first member; pointer is casted
						   // (if moved, would need something like container_of)
	uchar maxdepstatus; // highest exit code from exited deps
	// char padding[3];
	struct task_desc desc;
	struct db_taskresult *outresult; // write to here when done
	// on-disk file to store error output (note: fd is positive; top bit unused)
	bool haderr : 1; uint fd_err : 31;
	uint nblockers; // how many tasks we're waiting for before we can unblock
					// (0 if not currently blocked)
	struct vec_task_desc newdeps; // deps that will be blocked on next wait
	struct vec_taskp blockees; // tasks that are blocked waiting for this task
	struct table_taskdesc deps; // recorded deps from this run
	struct table_infile infiles; // recorded infiles from this run
};
DEF_FREELIST(task, struct task, 512)

static inline struct task_desc kmemb_activetask(struct task *const *t) {
	return (*t)->desc;
}
DECL_TABLE(static, activetask, struct task_desc, struct task *)
DEF_TABLE(static, activetask, hash_task_desc, eq_task_desc, kmemb_activetask)
static struct table_activetask activetasks;

struct task *goal; // HACK :(
static int goalstatus;
static int nstarted = 0;

// XXX this function only does part of the work - should just get inlined
static struct task *opentask(struct task_desc d, uint id) {
	struct task *t = freelist_alloc_task();
	if (t) {
		t->desc = d;
		t->haderr = false;
		t->nblockers = 0;
		t->blockees = (struct vec_taskp){0};
		if (!table_init_taskdesc(&t->deps)) goto e;
		if (!table_init_infile(&t->infiles)) goto e1;
		char buf[12];
		buf[0] = 'e';
		buf[fmt_fixed_u32(buf + 1, id)] = '\0';
		t->fd_err = openat(db_dirfd, buf, O_RDWR | O_CREAT | O_TRUNC |
				O_CLOEXEC, 0644);
		if (t->fd_err == -1) goto e2;
	}
	return t;

e2:	free(t->infiles.data); free(t->infiles.flags);
e1: free(t->deps.data); free(t->deps.flags);
e:	freelist_free_task(t);
	return 0;
}

static void closetask(struct task *t) {
	close(t->fd_err);
	free(t->blockees.data);
	free(t->deps.data); free(t->deps.flags);
	free(t->infiles.data); free(t->infiles.flags);
	freelist_free_task(t);
}

// FIXME this somehow breaks somewhere, figure out why and fix it!
static inline bool shellesc(struct str *s, const char *p) {
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
	if (!shellesc(&s, *t->argv)) goto e;
	for (const char *const *argv = t->argv + 1; *argv; ++argv) {
		if (!str_appendc(&s, ' ')) goto e;
		if (!shellesc(&s, *argv)) goto e;
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

static void showerr(const char *taskcmd, int fd_err) {
	obuf_put0t(buf_err, "* error output from task `");
	obuf_put0t(buf_err, taskcmd);
	obuf_put0t(buf_err, "`:\n");
	obuf_flush(buf_err);
	obuf_reset(buf_err);
	fd_transferall(fd_err, 2);
}

static noreturn exit_clean(int status) {
	db_finalise(); // XXX eh... should global cleanup happen somewhere else?
	exit(status);
}

static noreturn exit_failure(int status) {
	// ???
	errmsg_warnx("killing tasks and giving up");
	proc_killall(SIGTERM);
	exit(status);
}

static noreturn handle_failure(struct task *t, int status) {
	if (t->haderr) {
		obuf_put0t(buf_err, "* task's error output prior to failing:\n");
		obuf_flush(buf_err);
		obuf_reset(buf_err);
		fd_transferall(t->fd_err, 2);
	}
	closetask(t);
	exit_failure(status);
}

static void handle_success(struct task *t, int status) {
	char *s = desctostr(&t->desc);
	if (t->nblockers) {
		errmsg_warnx(msg_crit, "task `", s, "` exited while supposedly "
				"blocked - fix your code!");
		handle_failure(t, 2);
	}

	for (struct task **pp = t->blockees.data;
			pp - t->blockees.data < t->blockees.sz; ++pp) {
		if (status > (*pp)->maxdepstatus) (*pp)->maxdepstatus = status;
		if (!--(*pp)->nblockers) proc_unblock(&(*pp)->base);
	}

	struct db_taskresult *r = t->outresult;
	// XXX should really do some kinda ordering for deterministic error output
	struct vec_task_desc deplist = {0};
	TABLE_FOREACH_PTR(p, taskdesc, &t->deps) {
		if (!vec_push(&deplist, *p)) goto e;
	}
	struct vec_str infilelist = {0};
	TABLE_FOREACH_PTR(p, infile, &t->infiles) {
		if (!vec_push(&infilelist, *p)) goto e;
	}
	char buf[12];
	char buf2[12];
	buf[0] = 'e';
	buf2[0] = 'E';
	int n = fmt_fixed_u32(buf + 1, r->id);
	memcpy(buf2 + 1, buf + 1, n);
	buf[n] = '\0';
	buf2[n] = '\0';
	if (t->haderr) {
		if (renameat(db_dirfd, buf, db_dirfd, buf2) == -1) goto e;
	}
	else {
		// if no error output, delete; avoids read()/close() syscalls later
		unlinkat(db_dirfd, buf, 0);
		unlinkat(db_dirfd, buf2, 0);
	}
	free((void *)r->deps);
	r->deps = deplist.data; r->ndeps = deplist.sz;
	free((void *)r->infiles);
	r->infiles = infilelist.data; r->ndeps = infilelist.sz;
	db_committaskresult(r);
	goto r;

e:	free(deplist.data); free(infilelist.data);
	errmsg_warn(msg_warn, "couldn't save result of task `", s);
	errmsg_warnx(msg_note, "redundant reruns will happen later");
r:	if (t->haderr) showerr(s, t->fd_err);
	if (t == goal) goalstatus = status; // XXX stupid
	if (!--nstarted) exit_clean(status); // "
	closetask(t);
	free(s);
}

static bool reqinfile(struct task *t, const char *infile) {
	bool isnew;
	const char **pp = table_putget_transact_infile(&t->infiles, infile, &isnew);
	if (!pp) return false;
	if (isnew) {
		*pp = infile;
		if (!infile_ensure(infile)) return false;
		table_transactcommit_infile(&t->infiles);
	}
	return true;
}

static bool reqdep(struct task *req, struct task_desc dep, bool isgoal) {
	struct db_taskresult *r = db_gettaskresult(dep);
	if (!r) goto e;
	if (r->newness == db_newness) return false; // it's already done AND checked
	bool activeisnew;
	struct task **active = table_putget_transact_activetask(&activetasks, dep,
			&activeisnew);
	if (!active) goto e;
	if (!activeisnew) { // it's already running
		// FIXME: put the cycle check in here!
		return true;
	}
	if (r->newness == 0) goto r; // it's newly created!
	// if haven't checked up-to-date-ness in this run, do a depth first search
	for (const struct task_desc *dep = r->deps; dep - r->deps < r->ndeps;
			++dep) {
		// currently rerunning all deps preemptively/concurrently
		// it's up for debate/testing whether this is the universally
		// best-performing approach
		if (reqdep(0, *dep, false)) goto r;
	}
	// only check infiles if wouldn't already rerun - avoid the stat() calls!
	for (const char *const *pp = r->infiles; pp - r->infiles < r->ninfiles;
			++pp) {
		uint newness = infile_query(*pp, r->newness);
		if (newness == -1u) {
			errmsg_warn(msg_warn, "couldn't query infile ", *pp);
			errmsg_warnx(msg_note, "resorting to a maybe-redundant task rerun");
			goto r;
		}
		if (newness > r->newness) goto r;
	}
	r->newness = db_newness; // mark this as fully up to date for the future!
	// no point committing newness change to db; won't affect next run!
	//db_committaskresult(t);
	// we only get here once per up-to-date run - stick the error output here!
	char *s = desctostr(&dep);
	char buf[12];
	buf[0] = 'E';
	buf[fmt_fixed_u32(buf + 1, r->id)] = '\0';
	int fd = openat(db_dirfd, buf, O_RDONLY);
	if (fd != -1) {
		showerr(s, fd);
		close(fd);
	}
	else if (errno != ENOENT) {
		errmsg_warn(msg_warn, "can't display error output from task `", s, "`");
	}
	if (isgoal) exit_clean(r->status); // XXX this is stupid
	free(s);
	return false;

r:	*active = opentask(dep, r->id); // this has to be new; see above
	if (!*active) goto e;
	if (isgoal) goal = *active; // XXX also stupid
	(*active)->outresult = r;
	table_transactcommit_activetask(&activetasks);
	proc_start(&(*active)->base, (*active)->desc.argv, (*active)->desc.workdir);
	++nstarted;
	return false;

e:	errmsg_warn("couldn't handle dependency request");
	exit_failure(100);
}

static void reqwait(struct task *t) {
	t->maxdepstatus = 0;
	for (struct task_desc *d = t->newdeps.data;
			d - t->newdeps.data < t->newdeps.sz; ++d) {
		struct task **active = table_get_activetask(&activetasks, *d);
		if (active) {
			if (!vec_push(&(*active)->blockees, t)) goto e;
		}
		else {
			// assume the result is already stored at this point
			struct db_taskresult *r = db_gettaskresult(*d);
			if (r->status > t->maxdepstatus) t->maxdepstatus = r->status;
		}
	}
	t->nblockers = t->newdeps.sz;
	t->newdeps.sz = 0; // clear that list out now
	return;

e:	errmsg_warn("couldn't handle dependency wait request");
	exit_failure(100);
}

static void proc_cb(int evtype, union proc_ev_param P, struct proc_info *proc) {
	struct task *t = (struct task *)proc;
	switch (evtype) {
		case PROC_EV_STDERR:
			t->haderr = true;
			if (!fd_writeall(t->fd_err, P.buf, P.sz)) goto fail;
			break;
		case PROC_EV_EXIT:
			if (WIFEXITED(P.status)) {
				if (WEXITSTATUS(P.status) < 100) {
					handle_success(t, WEXITSTATUS(P.status));
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
			else /* WIFSIGNALED(P.status) */ {
				char *s = desctostr(&t->desc);
				errmsg_warnx(msg_error, "task `", s, "` was killed by SIG",
						sigstr(WTERMSIG(P.status)));
				free(s);
				handle_failure(t, 128 + WTERMSIG(P.status));
			}
			break;
		case PROC_EV_IPC:;
			struct ipc_req req;
			if (!ipcserver_recv(t->base.ipcsock, &req)) goto fail;
			switch (req.type) {
				case IPC_REQ_DEP: reqdep(t, req.dep, false); break;
				case IPC_REQ_WAIT: reqwait(t); break;
				case IPC_REQ_INFILE: reqinfile(t, req.infile);
			}
		case PROC_EV_UNBLOCK:
			if (!ipcserver_send(t->base.ipcsock, &(struct ipc_reply){
					t->maxdepstatus})) {
				goto fail;
			}
			break;
		case PROC_EV_ERROR:
fail:;		char *s = desctostr(&t->desc);
			errmsg_warn(msg_error, "couldn't handle event from task `", s, "`");
			free(s);
			handle_failure(t, 100);
	}
}

void task_init(void) {
	proc_init(&proc_cb);
	if (!table_init_activetask(&activetasks)) {
		errmsg_die(100, msg_fatal, "couldn't allocate task table");
	}
}
	
void task_goal(const char *const *argv, const char *workdir) {
	reqdep(0, (struct task_desc){argv, workdir}, true);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
