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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
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
#include <path.h>
#include <str.h>
#include <table.h>
#include <vec.h>

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
	uint cyclecheck;
	uchar maxdepstatus; // highest exit code from exited deps
	// char padding[7]; // 7!!! :(
	char *title; // user-provided friendly description for tui/logs
	struct task_desc desc;
	struct db_taskresult *outresult; // write to here when done
	// on-disk file to store error output (note: fd is positive; top bit unused)
	bool haderr : 1; uint fd_err : 31;
	uint nblockers; // how many tasks we're waiting for before we can unblock
					// (0 if not currently blocked)
	struct vec_task_desc newdeps; // deps that will block this on next wait
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
static int cyclecheckid = 0;

// XXX this function only does part of the work - should just get inlined
static struct task *opentask(struct task_desc d, uint id) {
	struct task *t = freelist_alloc_task();
	if (t) {
		t->desc = d;
		t->haderr = false;
		t->nblockers = 0;
		t->blockees = (struct vec_taskp){0};
		t->cyclecheck = 0;
		t->title = 0;
		if (!table_init_taskdesc(&t->deps)) goto e;
		if (!table_init_infile(&t->infiles)) goto e1;
		char buf[12];
		buf[0] = 'e';
		buf[1 + fmt_fixed_u32(buf + 1, id)] = '\0';
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
	// don't free here, title gets assigned to tui_lastdone before closetask is
	// called; after that, tui_lastdone gets freed before next title is set
	// free(t->title);
	free(t->blockees.data);
	free(t->deps.data); free(t->deps.flags);
	free(t->infiles.data); free(t->infiles.flags);
	freelist_free_task(t);
}

// this function *should* be safe, although it's only actually used for printing
// user-friendly messages right now, not for actually doing anything shell-based
static inline bool shellesc(struct str *s, const char *p) {
	// if there's shell-looking characters, wrap in single quotes, escaping
	// single quotes specially, otherwise try to keep it simple
	if (strpbrk(p, " \t\n\"'`<>&#~*$|(){};?!\\")) {
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
	//closetask(t); pointless for now since we're just dying
	exit_failure(status);
}

static void handle_success(struct task *t, int status) {
	char *s = desctostr(&t->desc);
	if (t->nblockers) {
		errmsg_warnx(msg_fatal, "task `", s, "` exited while supposedly "
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
	buf[1 + n] = '\0';
	buf2[1 + n] = '\0';
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
	r->infiles = infilelist.data; r->ninfiles = infilelist.sz;
	r->newness = db_newness;
	r->status = status;
	db_committaskresult(r);
	r->checked = true;
	goto r;

e:	free(deplist.data); free(infilelist.data);
	errmsg_warn(msg_warn, "couldn't save result of task `", s);
	errmsg_warnx(msg_note, "redundant reruns will happen later");
r:	if (t->haderr) showerr(s, t->fd_err);
	if (t == goal) goalstatus = status; // XXX stupid
	if (!--nstarted) exit_clean(goalstatus); // "
	table_del_activetask(&activetasks, t->desc);
	if (t->title) {
		free(tui_lastdone);
		tui_lastdone = t->title;
	}
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

// returns true if requester would need to rerun
static bool reqdep(struct task *req, struct task_desc dep, bool isgoal,
		int reqnewness) {
	if (req) {
		bool isnew;
		struct task_desc *d = table_putget_taskdesc(&req->deps, dep, &isnew);
		if (!d || isnew && !vec_push(&req->newdeps, dep)) goto e;
		*d = dep;
	}
	// ideally we'd avoid doing a get and then a put later but transact doesn't
	// work recursively (that was a fun time debugging...) so this is simpler
	// than trying to put and then back out again later (also literally none of
	// this matters since forking a process probably takes much longer than a
	// typical hash insert, so literally who cares)
	if (table_get_activetask(&activetasks, dep)) return true;
	struct db_taskresult *r = db_gettaskresult(dep);
	if (!r) goto e;
	if (r->newness == 0) goto r; // it's newly created!
	if (r->checked) return r->newness > reqnewness;
	bool needrerun = cleanbuild; // usually false
	// if haven't checked up-to-date-ness in this run, do a depth first search
	// (even if cleanbuild, still start deps eagerly in parallel)
	for (const struct task_desc *pp = r->deps; pp - r->deps < r->ndeps; ++pp) {
		// currently rerunning all deps preemptively/concurrently
		// it's up for debate/testing whether this is the universally
		// best-performing approach, but it's the approach for now
		if (reqdep(0, *pp, false, r->newness)) needrerun = true;
	}
	// ideally we wouldn't check these if we know we already need to rerun, but
	// if we don't update the infiles themselves, they'll change later and
	// that'll cause yet another rebuild for no reason
	for (const char *const *pp = r->infiles; pp - r->infiles < r->ninfiles;
			++pp) {
		int ret = infile_query(*pp, r->newness);
		if (ret == -1 && !needrerun) {
			errmsg_warn(msg_warn, "couldn't query infile ", *pp);
			errmsg_warnx(msg_note, "resorting to a maybe-redundant task rerun");
		}
		if (ret) needrerun = true;
	}
	if (needrerun) {
r:;		struct task **tp = table_put_activetask(&activetasks, dep);
		if (!tp) goto e;
		*tp = opentask(dep, r->id);
		if (!*tp) goto e;
		// create the implicit infile, but only for in-tree executables
		if (path_isfull(dep.argv[0])) {
			// XXX should we just use a PATH_MAX array and avoid this malloc?
			// this was the first thing I did and it works; should use brain at
			// a later date
			struct str frombase = {0};
			if (!str_clear(&frombase) ||
					!str_append0t(&frombase, dep.workdir) ||
					!str_appendc(&frombase, '/') ||
					!str_append0t(&frombase, dep.argv[0])) {
				goto e;
			}
			char *canon = malloc(frombase.sz);
			if (!canon) goto e;
			if (fpath_canon(frombase.data, canon, 0) == FPATH_OK) {
				const char *infile = db_intern_free(canon);
				if (!infile || !infile_ensure(infile)) goto e;
				const char **pp = table_put_infile(&(*tp)->infiles, infile);
				if (!pp) goto e;
				*pp = infile;
			}
			free(frombase.data);
		}
		if (isgoal) goal = *tp; // XXX also stupid
		(*tp)->outresult = r;
		proc_start(&(*tp)->base, (*tp)->desc.argv, (*tp)->desc.workdir);
		++nstarted;
		return true;
	}
	// we only get here once per up-to-date run - stick the error output here!
	char buf[12];
	buf[0] = 'E';
	buf[1 + fmt_fixed_u32(buf + 1, r->id)] = '\0';
	int fd = openat(db_dirfd, buf, O_RDONLY);
	if (fd != -1) {
		char *s = desctostr(&dep);
		showerr(s, fd);
		close(fd);
		free(s);
	}
	else if (errno != ENOENT) {
		char *s = desctostr(&dep);
		errmsg_warn(msg_warn, "can't display error output from task `", s, "`");
		free(s);
	}
	if (isgoal) exit_clean(r->status); // XXX this is stupid
	r->checked = true;
	return r->newness > reqnewness;

	// XXX one day, make this more granular instead of just dying on the spot
e:	errmsg_warn("couldn't handle task dependency");
	exit_failure(100);
}

// cycle check algorithm:
// * only care about running tasks that are blocked, since non-blocked things
//   obviously aren't participating in any kind of dependency relationship, and
//   things in the db obviously got checked already
// * DFS from req, trying to find dep via blockees; if dep is eventually blocked
//   because of req then trying to block req on dep is a cycle, which is bad!
// * global cyclecheckid value is bumped every search (before calls to this; see
//   below); if a task is hit which equals that value then it's been seen
//   already in this search so don't traverse it again
// * if a cycle is found, the path through blockees is printed essentially by
//   means of stack unwinding (see below and you'll see what I mean)
//
// cool thing about this: it only happens once per wait, and generally only
// traverses a small subset of the graph, unlike most build system cycle checks
// which necessarily validate the entire thing every time!

static bool cyclecheck(struct task *req, struct task *dep) {
	if (req->cyclecheck == cyclecheckid) return false;
	req->cyclecheck = cyclecheckid;
	for (uint i = 0; i < req->blockees.sz; ++i) {
		if (req->blockees.data[i] == dep) {
			errmsg_warnx(msg_fatal, "blocked tasks would deadlock "
					"(dependency cycle)");
			obuf_put0t(buf_err, "  the full cycle looks something like this:\n");
			obuf_put0t(buf_err, "  ┌─► `");
			char *s = desctostr(&req->blockees.data[i]->desc);
			if (s) obuf_put0t(buf_err, s);
			obuf_put0t(buf_err, "`\n");
			goto yep;
		}
		if (cyclecheck(req->blockees.data[i], dep)) {
yep:		obuf_put0t(buf_err, "  │   `");
			char *s = desctostr(&req->desc);
			if (s) obuf_put0t(buf_err, s);
			obuf_put0t(buf_err, "`\n");
			free(s);
			return true;
		}
	}
	return false;
}

static void docyclecheck(struct task *req, struct task *dep) {
	if (cyclecheck(req, dep)) {
		// gotta do the tail end of the output
		obuf_put0t(buf_err, "  └────┘\n");
		obuf_flush(buf_err);
		exit_failure(100);
	}
}

static void reqwait(struct task *t) {
	t->maxdepstatus = 0;
	++cyclecheckid;
	for (struct task_desc *d = t->newdeps.data;
			d - t->newdeps.data < t->newdeps.sz; ++d) {
		struct task **active = table_get_activetask(&activetasks, *d);
		if (active) {
			docyclecheck(t, *active);
			if (!vec_push(&(*active)->blockees, t)) goto e;
			++t->nblockers;
		}
		else {
			// assume the result is already stored at this point
			struct db_taskresult *r = db_gettaskresult(*d);
			if (r->status > t->maxdepstatus) t->maxdepstatus = r->status;
		}
	}
	t->newdeps.sz = 0; // clear that list out now
	// if there weren't any deps, send unblock message immediately, otherwise
	// tell proc we're blocked
	if (t->nblockers) {
		proc_block();
	}
	else if (!ipcserver_send(t->base.ipcsock, &(struct ipc_reply){0})) {
		goto e;
	}
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
			if (!ipcserver_recv(t->base.ipcsock, &req, t->desc.workdir)) {
				if (errno == EINVAL) goto qfail; // error reported by ipcserver
				goto fail;
			}
			switch (req.type) {
				case IPC_REQ_DEP:
					reqdep(t, req.dep, false, t->outresult->newness); break;
				case IPC_REQ_WAIT: reqwait(t); break;
				case IPC_REQ_INFILE:
					if (!reqinfile(t, req.infile)) goto fail; break;
				case IPC_REQ_TASKTITLE: free(t->title); t->title = req.title;
			}
			break;
		case PROC_EV_UNBLOCK:
			if (!ipcserver_send(t->base.ipcsock,
					&(struct ipc_reply){t->maxdepstatus})) {
				goto fail;
			}
			break;
		case PROC_EV_ERROR:
fail:;		char *s = desctostr(&t->desc);
			errmsg_warn(msg_error, "couldn't handle event from task `", s, "`");
			free(s);
qfail:		handle_failure(t, 100);
	}
}

void task_init(void) {
	proc_init(&proc_cb);
	if (!table_init_activetask(&activetasks)) {
		errmsg_die(100, msg_fatal, "couldn't allocate task table");
	}
}
	
void task_goal(const char *const *argv, const char *workdir) {
	reqdep(0, (struct task_desc){argv, workdir}, true, 0);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
