#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <alloc.h>
#include <basichashes.h>
#include <errmsg.h>
#include <fmt.h>
#include <intdefs.h>
#include <path.h>
#include <noreturn.h>
#include <table.h>

#include "build.h"
#include "defs.h"
#include "evloop.h"
#include "fpath.h"
#include "ipcserver.h"
#include "proc.h"

static struct q {
	// struct proc_info *proc;
	ulong procaddr; // lower bit: 0 for start, 1 for unblock (saving 8 bytes!!)
	const char *const *argv; // if start
	const char *workdir; // " if start
	struct q *next;
} *q_head, **q_tail = &q_head;
DEF_FREELIST(q, struct q, 1024)
int qlen;

int nactive, nblocked;
static char **procenv;
static proc_ev_cb ev_cb;

static inline uint hash_pid(pid_t x) {
	if (sizeof(pid_t) <= 4) return hash_int(x);
	return hash_vlong(x);
}
static inline pid_t proc_hash_memb(struct proc_info **p) { return (*p)->_pid; }

DECL_TABLE(static, pid_proc, pid_t, struct proc_info *)
DEF_TABLE(static, pid_proc, hash_pid, table_ideq, proc_hash_memb)
static struct table_pid_proc by_pid = {0};

static void doerrio(int fd, struct proc_info *p) {
	char buf[65536];
	int nread;
	while ((nread = recv(fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
		ev_cb(PROC_EV_STDERR, (union proc_ev_param){.buf = buf, .sz = nread}, p);
	}
}

static void cb_err(int fd, short revents, void *ctxt) {
	if (revents & POLLHUP) {
		evloop_onfd_remove(fd);
		// note: don't close() yet; that gets taken care of when the process
		// exits and we don't wanna double-close and clobber something else
		return;
	} // else assume POLLIN
	doerrio(fd, (struct proc_info *)ctxt);
}

static void cb_ipc(int fd, short revents, void *ctxt) {
	if (revents & POLLHUP) {
		evloop_onfd_remove(fd);
		// note: don't close() yet; that gets taken care of when the process
		// exits and we don't wanna double-close and clobber something else
		return;
	} // else assume POLLIN
	ev_cb(PROC_EV_IPC, (union proc_ev_param){0}, (struct proc_info *)ctxt);
}

static char rootdirvar[sizeof(ENV_ROOT_DIR "=") - 1 + PATH_MAX] =
		ENV_ROOT_DIR "=";
static void setrootdirvar(const char *dir) {
	char *p = rootdirvar + sizeof(ENV_ROOT_DIR "=") - 1;
	if (dir[0] == '.' && dir[1] == '\0') {
		p[0] = '.';
		p[1] = '\0';
		return;
	}
	fpath_leavesubdir(dir, p, PATH_MAX); // FIXME check error!!
}

static char sockfdvar[sizeof(ENV_SOCKFD "=") - 1 + 11] = ENV_SOCKFD "=";
static void setsockfdvar(int fd) {
	sockfdvar[sizeof(ENV_SOCKFD "=") - 1 +
			fmt_fixed_u32(sockfdvar + sizeof(ENV_SOCKFD "=") - 1, fd)] = '\0';
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
	int errsock[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, errsock) == -1) {
		errmsg_warn(msg_error, "couldn't create stderr socket for task");
		goto e;
	}
	if (!evloop_onfd(errsock[0], EV_IN, &cb_err, proc)) {
		errmsg_warn(msg_error, "couldn't handle stderr socket events");
		goto e1;
	}
	proc->_errsock = errsock[0];
	int ipcsock[2];
	// we have to preserve boundaries since we do blocking, buffered IO
	// (if boundaries aren't preserved, messages bleed into the buffer and get
	// dropped as the buffer is cleared between reads)
	// note: could also use SOCK_SEQPACKET, it doesn't really matter here, but
	// DGRAM *might* be more portable, so using that one
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, ipcsock) == -1) {
		errmsg_warn(msg_error, "couldn't create IPC socket for task");
		goto e2;
	}
	if (!evloop_onfd(ipcsock[0], EV_IN, &cb_ipc, proc)) {
		errmsg_warn(msg_error, "couldn't handle IPC socket events");
		goto e3;
	}
	proc->ipcsock = ipcsock[0];
	setrootdirvar(workdir);
	setsockfdvar(ipcsock[1]);
	proc->_pid = vfork();
	if (proc->_pid == -1) {
		errmsg_warn(msg_error, "couldn't fork new process");
		goto e4;
	}
	if (!proc->_pid) {
		setpgid(0, 0); // see proc_killall() below
		// unblock all signals - NOTE! this may cause handlers to run in the
		// child, for now this is _assumed_ not to be an issue
		sigprocmask(SIG_SETMASK, &(sigset_t){0}, 0);
		if (chdir(workdir) == -1) {
			errmsg_warn("child: ", msg_fatal, "couldn't enter directory ",
					workdir);
			goto ce;
		}
		close(ipcsock[0]);
		dup2(errsock[1], 2);
		execve(prog, (char *const *)argv, procenv);
		errmsg_warn("child: ", msg_fatal, "couldn't exec ", prog);
ce:		if (errno == ENOENT || errno == EACCES) {
			// we can actually cache this result, there's still an infile!
			_exit(2);
		}
		// otherwise it's an abnormal error, we should try again later
		_exit(100);
	}
	++nactive;
	// path search allocates a new string, so free only if != what was passed
	if (prog != argv[0]) free((char *)prog);
	struct proc_info **ent = table_put_pid_proc(&by_pid, proc->_pid);
	// FIXME work out how to gracefully recover here
	if (!ent) errmsg_die(200, msg_fatal, "couldn't store process information");
	*ent = proc;
	close(errsock[1]);
	return;

e4:	evloop_onfd_remove(ipcsock[0]);
e3:	close(ipcsock[0]); close(ipcsock[1]);
e2:	evloop_onfd_remove(errsock[0]);
e1:	close(errsock[0]); close(errsock[1]);
e:	ev_cb(PROC_EV_ERROR, (union proc_ev_param){0}, proc);
}

static void do_unblock(struct proc_info *proc) {
	++nactive;
	ev_cb(PROC_EV_UNBLOCK, (union proc_ev_param){0}, proc);
}

static void qpop(void) {
	struct q *q = q_head;
	if (!q) return; // nothing left to start doing!
	--qlen;
	q_head = q->next;
	if (!q_head) q_tail = &q_head; // last &next is gone, reset tail to head
	if (q->procaddr & 1) do_unblock((struct proc_info *)(q->procaddr & -2ul));
	else do_start(q->argv, q->workdir, (struct proc_info *)q->procaddr);
	freelist_free_q(q);
}

static void onchld(void) {
	pid_t pid; int status;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		struct proc_info *proc = *table_del_pid_proc(&by_pid, pid);
		// in case we got SIGCHLD right as IO happened, flush out the stderr
		// socket a final time before closing
		doerrio(proc->_errsock, proc);
		close(proc->_errsock);
		evloop_onfd_remove(proc->_errsock);
		close(proc->ipcsock);
		evloop_onfd_remove(proc->ipcsock);
		ev_cb(PROC_EV_EXIT, (union proc_ev_param){.status = status}, proc);
		--nactive;
		qpop();
	}
}

void proc_killall(int sig) {
	// every task has its own group so we can kill that group;
	// amazingly, whoever designed kill(2) decided that kill(0) should kill the
	// caller too, otherwise this would be way easier and less annoying!
	TABLE_FOREACH_PTR(p, pid_proc, &by_pid) kill(-(*p)->_pid, sig);
}

static void onterm(void) {
	errmsg_warnx("got a SIGTERM; killing tasks and giving up");
	proc_killall(SIGTERM);
	sigaction(SIGTERM, &(struct sigaction){.sa_handler = SIG_DFL}, 0);
	raise(SIGTERM); // die, as the user intended
	sigset_t s = {0};
	sigaddset(&s, SIGTERM);
	sigprocmask(SIG_UNBLOCK, &s, 0);
}

static void onint(void) {
	errmsg_warnx("got a SIGINT; killing tasks and giving up");
	proc_killall(SIGINT);
	sigaction(SIGINT, &(struct sigaction){.sa_handler = SIG_DFL}, 0);
	raise(SIGINT);
	sigset_t s = {0};
	sigaddset(&s, SIGINT);
	sigprocmask(SIG_UNBLOCK, &s, 0);
}

void proc_init(proc_ev_cb cb) {
	ev_cb = cb;
	long envsz = 0;
	for (char **pp = environ; *pp; ++pp) ++envsz;
	procenv = malloc((envsz + 3) * sizeof(*environ));
	if (!procenv) errmsg_die(100, msg_fatal, "couldn't allocate environment");
	memcpy(procenv, environ, sizeof(*environ) * envsz);
	procenv[envsz] = rootdirvar;
	procenv[envsz + 1] = sockfdvar;
	procenv[envsz + 2] = 0;
	evloop_onsig(SIGCHLD, &onchld);
	evloop_onsig(SIGTERM, &onterm);
	evloop_onsig(SIGINT, &onint);
	if (!table_init_pid_proc(&by_pid)) {
		errmsg_die(100, msg_fatal, "couldn't allocate process table");
	}
}

void proc_start(struct proc_info *proc, const char *const *argv,
		const char *workdir) {
	// FIXME canonicalise workdir (it is *assumed* to be canonical for now)
	if (nactive < maxpar) {
		do_start(argv, workdir, proc);
	}
	else {
		struct q *q = freelist_alloc_q();
		if (!q) {
			ev_cb(PROC_EV_ERROR, (union proc_ev_param){0}, proc);
			return;
		}
		q->procaddr = (ulong)proc;
		q->argv = argv; q->workdir = workdir;
		q->next = 0; *q_tail = q; q_tail = &q->next;
		++qlen;
	}
}

void proc_block(void) {
	++nblocked;
	--nactive;
	qpop();
}

void proc_unblock(struct proc_info *proc) {
	--nblocked; // NOTE: this is *added* to qlen in tui to get "waiting" count
	if (nactive < maxpar) {
		do_unblock(proc);
	}
	else {
		struct q *q = freelist_alloc_q();
		if (!q) {
			ev_cb(PROC_EV_ERROR, (union proc_ev_param){0}, proc);
			return;
		}
		q->procaddr = (ulong)proc + 1;
		q->next = 0; *q_tail = q; q_tail = &q->next;
		++qlen;
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
