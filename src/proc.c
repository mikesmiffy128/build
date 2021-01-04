#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <alloc.h>
#include <basichashes.h>
#include <errmsg.h>
#include <intdefs.h>
#include <path.h>
#include <noreturn.h>
#include <table.h>

#include "evloop.h"
#include "proc.h"

static struct q {
	enum {
		Q_START,
		Q_UNBLOCK
	} goal;
	/* --use this padding space-- */
	struct proc_info *proc;
	struct { // if Q_START
		const char *const *argv;
		const char *workdir;
	};
	struct q *next;
} *q_head, **q_tail = &q_head;
DEF_FREELIST(q, struct q, 1024)

static int maxparallel;
static int nactive;

static inline uint hash_pid(pid_t x) {
	if (sizeof(pid_t) <= 4) return hash_int(x);
	return hash_vlong(x);
}
static inline pid_t proc_hash_memb(struct proc_info **p) { return (*p)->_pid; }

DECL_TABLE(static, pid_proc, pid_t, struct proc_info *)
DEF_TABLE(static, pid_proc, hash_pid, table_ideq, proc_hash_memb)
static struct table_pid_proc by_pid = {0};

static proc_ev_cb ev_cb;

static void handle_out(int fd, short revents, void *ctxt, int procev) {
	if (revents & POLLHUP) {
		evloop_onfd_remove(fd);
		// note: don't close() yet; that gets taken care of when the process
		// exits and we don't wanna double-close and clobber something else
		return;
	} // else assume POLLIN
	// TODO(basic-core) stuff...
	char buf[16386];
	int nread;
	while ((nread = recv(fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
		ev_cb(procev, (union proc_ev_param){buf, nread},
				(struct proc_info *)ctxt);
	}
}

static void cb_out(int fd, short revents, void *ctxt) {
	handle_out(fd, revents, ctxt, PROC_EV_STDOUT);
}
static void cb_err(int fd, short revents, void *ctxt) {
	handle_out(fd, revents, ctxt, PROC_EV_STDERR);
}

static void do_start(const char *const *argv, const char *workdir,
		struct proc_info *proc) {
	const char *prog;
	if (path_isfull(argv[0])) {
		prog = argv[0];
	}
	else {
		const char *path = getenv("PATH");
		if (!path) {
			errmsg_warnx(msg_error, "couldn't find \"", argv[0],
					"\": no PATH variable set");
			goto e;
		}
		prog = path_search(path, argv[0]);
		if (!prog) {
			errmsg_warn(msg_error, "couldn't find \"", argv[0], "\"");
			goto e;
		}
	}
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
			proc->_outsock) == -1) {
		errmsg_warn(msg_error, "couldn't create stdout socket for process");
		goto e;
	}
	if (!evloop_onfd(proc->_outsock[0], EV_IN, cb_out, proc)) {
		errmsg_warn(msg_error, "couldn't handle stdout socket events");
		goto e1;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
			proc->_errsock) == -1) {
		errmsg_warn(msg_error, "couldn't create stderr socket for process");
		goto e2;
	}
	if (!evloop_onfd(proc->_errsock[0], EV_IN, cb_err, proc)) {
		errmsg_warn(msg_error, "couldn't handle stderr socket events");
		goto e3;
	}
	proc->_pid = vfork();
	if (proc->_pid == -1) {
		errmsg_warn(msg_error, "couldn't fork new process");
		goto e4;
	}
	if (!proc->_pid) {
		// unblock all signals - NOTE! this may cause handlers to run in the
		// child, for now this is _assumed_ not to be an issue
		sigprocmask(SIG_SETMASK, &(sigset_t){0}, 0);
		if (chdir(workdir) == -1) {
			errmsg_warn("child: ", msg_fatal, "couldn't enter directory ",
					workdir);
			_exit(200);
		}
		dup2(proc->_outsock[1], 1);
		dup2(proc->_errsock[1], 2);
		// TODO(basic-core) put build fds in env
		execve(prog, (char *const *)argv, environ);
		errmsg_warn("child: ", msg_fatal, "couldn't exec ", prog);
		_exit(100);
	}
	++nactive;
	// path search allocates a new string, so free only if != what was passed
	if (prog != argv[0]) free((char *)prog);
	struct proc_info **ent = table_put_pid_proc(&by_pid, proc->_pid);
	// FIXME work out how to gracefully recover here
	if (!ent) errmsg_die(200, msg_fatal, "couldn't store process information");
	*ent = proc;

	close(proc->_errsock[1]);
	close(proc->_outsock[1]);
	return;
e4:
	evloop_onfd_remove(proc->_errsock[0]);
e3:	close(proc->_errsock[0]); close(proc->_errsock[1]);
e2:	evloop_onfd_remove(proc->_outsock[0]);
e1:	close(proc->_outsock[0]); close(proc->_outsock[1]);
e:	ev_cb(PROC_EV_ERROR, (union proc_ev_param){0}, proc);
}

static void do_unblock(struct proc_info *proc) {
	++nactive;
	ev_cb(PROC_EV_UNBLOCK, (union proc_ev_param){0}, proc);
}

static void qpop(void) {
	struct q *q = q_head;
	if (!q) return; // there could well be nothing left to do!
	q_head = q->next;
	if (!q_head) q_tail = &q_head; // last &next is gone, reset tail to head
	if (q->goal == Q_START) do_start(q->argv, q->workdir, q->proc);
	else /* q-> goal == Q_UNBLOCK */ do_unblock(q->proc);
	freelist_free_q(q);
}

static void onchld(void) {
	pid_t pid; int status;
	// FIXME flush all io before closing, or something (use brain)
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		struct proc_info *proc = *table_del_pid_proc(&by_pid, pid);
		close(proc->_outsock[0]);
		evloop_onfd_remove(proc->_outsock[0]);
		close(proc->_errsock[0]);
		evloop_onfd_remove(proc->_errsock[0]);
		ev_cb(PROC_EV_EXIT, (union proc_ev_param){.status = status}, proc);
		--nactive;
		qpop();
	}
}

void proc_init(int maxparallel_, proc_ev_cb cb) {
	maxparallel = maxparallel_;
	ev_cb = cb;
	evloop_onsig(SIGCHLD, &onchld);
	if (!table_init_pid_proc(&by_pid)) {
		errmsg_die(100, msg_error, "couldn't allocate hashtable");
	}
}

void proc_start(struct proc_info *proc, const char *const *argv,
		const char *workdir) {
	if (nactive < maxparallel) {
		do_start(argv, workdir, proc);
	}
	else {
		struct q *q = freelist_alloc_q();
		if (!q) {
			ev_cb(PROC_EV_ERROR, (union proc_ev_param){0}, proc);
			return;
		}
		q->goal = Q_START;
		q->argv = argv; q->workdir = workdir; // FIXME (TEMP) ownership issues?
		q->next = 0; *q_tail = q; q_tail = &q->next;
	}
}

void proc_block(void) {
	--nactive;
	qpop();
}

void proc_unblock(struct proc_info *proc) {
	if (nactive < maxparallel) {
		do_unblock(proc);
	}
	else {
		struct q *q = freelist_alloc_q();
		if (!q) {
			ev_cb(PROC_EV_ERROR, (union proc_ev_param){0}, proc);
			return;
		}
		q->goal = Q_UNBLOCK;
		q->proc = proc;
		q->next = 0; *q_tail = q; q_tail = &q->next;
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
