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
#include "xcred.h"

struct proc {
	pid_t pid;
#ifndef SO_PASSCRED
	int _outsock[2], _errsock[2];
#endif
	proc_ev_cb ev_cb;
	void *ev_ctxt;
};
DEF_FREELIST(proc, struct proc, 1024)

#ifdef SO_PASSCRED
static int _outsock[2], _errsock[2];
static inline int *outsock(struct proc *p) { return _outsock; }
static inline int *errsock(struct proc *p) { return _errsock; }
#else
static inline int *outsock(struct proc *p) { return p->_outsock; }
static inline int *errsock(struct proc *p) { return p->_errsock; }
#endif

static struct q {
	enum {
		Q_START,
		Q_UNBLOCK
	} goal;
	/* --fill this padding space-- */
	union {
		struct { // if Q_START
			const char *const *argv; // if Q_START
			const char *workdir;
			proc_ev_cb ev_cb;
			void *ev_ctxt;
		};
		struct proc *proc; // if Q_UNBLOCK
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
static inline pid_t proc_hash_memb(struct proc **p) { return (*p)->pid; }

DECL_TABLE(static, pid_proc, pid_t, struct proc *)
DEF_TABLE(static, pid_proc, hash_pid, table_ideq, proc_hash_memb)
static struct table_pid_proc by_pid = {0};

static void proc_out(int fd, short revents, void *ctxt, int procev) {
	char buf[4096];
	int nread;
#ifdef SO_PASSCRED
	// TODO(basic-core) stuff...
	char xcredbuf[CMSG_SPACE(sizeof(struct xcred))];
	struct iovec iov = {buf, sizeof(buf)};
	struct msghdr h = {.msg_iov = &iov, .msg_iovlen = 1,
			.msg_control = xcredbuf, .msg_controllen = sizeof(xcredbuf)};
	while ((nread = recvmsg(fd, &h, MSG_DONTWAIT)) > 0) {
		struct cmsghdr *m = CMSG_FIRSTHDR(&h);
		pid_t pid = ((struct xcred *)CMSG_DATA(m))->xc_pid;
		struct proc *p = *table_get_pid_proc(&by_pid, pid);
		p->ev_cb(procev, (union proc_ev_param){buf, nread}, p->ev_ctxt);
	}
#else
	struct proc *p = ctxt;
	// TODO(basic-core) stuff...
	while ((nread = recv(fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
		p->ev_cb(procev, (union proc_ev_param){buf, nread}, p->ev_ctxt);
	}
#endif
}

static void cb_out(int fd, short revents, void *ctxt) {
	proc_out(fd, revents, ctxt, PROC_EV_STDOUT);
}
static void cb_err(int fd, short revents, void *ctxt) {
	proc_out(fd, revents, ctxt, PROC_EV_STDERR);
}

static void do_start(const char *const *argv, const char *workdir,
		proc_ev_cb cb, void *ctxt) {
	struct proc *p = freelist_alloc_proc();
	if (!p) return; // FIXME call error cb
	p->ev_cb = cb; p->ev_ctxt = ctxt;
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
#ifndef SO_PASSCRED
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, outsock(p)) == -1) {
		errmsg_warn(msg_error, "couldn't create stdout socket for process");
		goto e;
	}
	if (!evloop_onfd(outsock(p)[0], EV_IN, cb_out, p)) {
		errmsg_warn(msg_error, "couldn't handle stdout socket events");
		goto e1;
	}
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, errsock(p)) == -1) {
		errmsg_warn(msg_error, "couldn't create stderr socket for process");
		goto e2;
	}
	if (!evloop_onfd(errsock(p)[0], EV_IN, cb_err, p)) {
		errmsg_warn(msg_error, "couldn't handle stderr socket events");
		goto e3;
	}
#endif
	p->pid = vfork();
	if (p->pid == -1) {
		errmsg_warn(msg_error, "couldn't fork new process");
		goto e4;
	}
	if (!p->pid) {
		// unblock all signals - NOTE! this may cause handlers to run in the
		// child, for now this is _assumed_ not to be an issue
		sigprocmask(SIG_SETMASK, &(sigset_t){0}, 0);
		if (chdir(workdir) == -1) {
			errmsg_warn("child: ", msg_fatal, "couldn't enter directory ",
					workdir);
			_exit(200);
		}
		dup2(outsock(p)[1], 1);
		dup2(errsock(p)[1], 2);
		// TODO(basic-core) put build fds in env
		execve(prog, (char *const *)argv, environ);
		errmsg_warn("child: ", msg_fatal, "couldn't exec ", prog);
		_exit(100);
	}
	++nactive;
	// path search allocates a new string, so free only if != what was passed
	if (prog != argv[0]) free((char *)prog);
	struct proc **ent = table_put_pid_proc(&by_pid, p->pid);
	// FIXME work out how to gracefully recover here
	if (!ent) errmsg_die(200, msg_fatal, "couldn't store process information");
	*ent = p;

#ifndef SO_PASSCRED
	close(errsock(p)[1]);
	close(outsock(p)[1]);
#endif
	return;
e4:
#ifndef SO_PASSCRED
	evloop_onfd_remove(errsock(p)[0]);
e3:	close(errsock(p)[0]); close(errsock(p)[1]);
e2:	evloop_onfd_remove(outsock(p)[0]);
e1:	close(outsock(p)[0]); close(outsock(p)[1]);
#endif
e:;	return; // XXX FIXME call the callback!
}

static void do_unblock(const struct proc *p) {
	++nactive;
	p->ev_cb(PROC_EV_UNBLOCK, (union proc_ev_param){0}, p->ev_ctxt);
}

static void qpop(void) {
	struct q *q = q_head;
	if (!q) return; // there could well be nothing left to do!
	q_head = q->next;
	if (!q_head) q_tail = &q_head; // last &next is gone, reset head
	if (q->goal == Q_START) do_start(q->argv, q->workdir, q->ev_cb, q->ev_ctxt);
	else /* q-> goal == Q_UNBLOCK */ do_unblock(q->proc);
	freelist_free_q(q);
}

static void onchld(void) {
	pid_t pid; int status;
	// FIXME flush all io before closing, or something (use brain)
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		struct proc *p = *table_del_pid_proc(&by_pid, pid);
#ifndef SO_PASSCRED
		close(outsock(p)[0]);
		evloop_onfd_remove(outsock(p)[0]);
		close(errsock(p)[0]);
		evloop_onfd_remove(errsock(p)[0]);
#endif
		p->ev_cb(PROC_EV_EXIT, (union proc_ev_param){.status = status},
				p->ev_ctxt);
		freelist_free_proc(p);
		--nactive;
		qpop();
	}
}

void proc_init(int maxparallel_) {
	maxparallel = maxparallel_;
	evloop_onsig(SIGCHLD, &onchld);
	if (!table_init_pid_proc(&by_pid)) {
		errmsg_die(100, msg_error, "couldn't allocate hashtable");
	}

#ifdef SO_PASSCRED
	// if we have cred support we can share sockets between tasks, otherwise we
	// need 2 pairs of FDs per task (in addition to the control sockets we
	// already have)
	// btw, using socket rather than pipe even when not doing cred stuff because
	// it has MSG_DONTWAIT which avoids various O_NONBLOCK faff
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, _outsock) == -1 ||
			socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, _errsock) == -1) {
		errmsg_die(100, msg_fatal, "couldn't create socket");
	}
	if (setsockopt(_outsock[0], SOL_SOCKET, SO_PASSCRED, &(int){1},
				sizeof(int)) == -1 ||
			setsockopt(_errsock[0], SOL_SOCKET, SO_PASSCRED, &(int){1},
					sizeof(int)) == -1) { // XXX this if is really ugly
		errmsg_die(100, msg_fatal, "couldn't set socket options");
	}
	if (!evloop_onfd(_outsock[0], EV_IN, &cb_out, 0) ||
			!evloop_onfd(_errsock[0], EV_IN, &cb_err, 0)) {
		errmsg_die(100, msg_fatal, "couldn't handle socket events");
	}
#else
	// let's take the opportunity to complain here, because I like complaining
	#warning Your OS has no SO_PASSCRED equivalent; build will use many more FDs
#endif
}

bool proc_start(const char *const *argv, const char *workdir, proc_ev_cb cb,
		void *ctxt) {
	if (nactive < maxparallel) {
		do_start(argv, workdir, cb, ctxt);
	}
	else {
		struct q *q = freelist_alloc_q();
		if (!q) return false;
		q->goal = Q_START;
		q->argv = argv; q->workdir = workdir; // FIXME (TEMP) ownership issues?
		q->ev_cb = cb; q->ev_ctxt = ctxt;
		q->next = 0; *q_tail = q; q_tail = &q->next;
	}
	return true;
}

void proc_block(struct proc *p) {
	--nactive;
	qpop();
}

bool proc_unblock(struct proc *p) {
	if (nactive < maxparallel) {
		do_unblock(p);
	}
	else {
		struct q *q = freelist_alloc_q();
		if (!q) return false;
		q->goal = Q_UNBLOCK;
		q->proc = p;
		q->next = 0; *q_tail = q; q_tail = &q->next;
	}
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
