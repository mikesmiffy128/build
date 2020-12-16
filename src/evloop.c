#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include <alloc.h>
#include <errmsg.h>
#include <intdefs.h>
#include <noreturn.h>
#include <skiplist.h>

#include "time.h"

#define MAXFDS 4096 // hopefully this is enough!

static int nfds = 0; // upper bound for poll call
static struct pollfd pfds[MAXFDS] = {0};
static struct fd_cb {
	void (*f)(int, short, void *);
	void *ctxt;
} fd_cbs[MAXFDS] = {0};

DECL_SKIPLIST(static, timer, struct timer, vlong, 4)
struct timer {
	vlong deadline;
	void (*cb)(void *);
	void *ctxt;
	struct skiplist_hdr_timer hdr;
};
#define timer_comp(x, y) ((x)->deadline - y)
#define timer_hdr(x) (&(x)->hdr)
DEF_SKIPLIST(static, timer, timer_comp, timer_hdr)
struct skiplist_hdr_timer timers = {0};
DEF_FREELIST(timer, struct timer, 1024)

#define MAXSIGCB 2 // NOTE increase as needed for the program
static sigset_t gotsigs = {0};
static void onsig(int sig) { sigaddset((sigset_t *)&gotsigs, sig); }
static struct sig_cb {
	int sig;
	void (*cb)(void);
} sig_cbs[MAXSIGCB];
static struct sig_cb *sig_cbs_tail = sig_cbs;

void evloop_init(void) { for (nfds_t i = 0; i < MAXFDS; ++i) pfds[i].fd = -1; }

bool evloop_onfd(int fd, short events, void (*cb)(int, short, void *),
		void *ctxt) {
	if (fd >= MAXFDS) { errno = ENOMEM; return false; } // correct errno?
	pfds[fd].fd = fd;
	pfds[fd].events = events;
	fd_cbs[fd] = (struct fd_cb){cb, ctxt};
	if (fd >= nfds) nfds = fd + 1;
	return true;
}

void evloop_onfd_remove(int fd) {
	pfds[fd].fd = -1;
	if (fd == nfds - 1) for (; nfds && pfds[nfds - 1].fd == -1; --nfds);
}

void evloop_onsig(int sig, void (*cb)(void)) {
	// this won't happen if I'm competent, it's just to avoid a segfault
	if (sig_cbs_tail - sig_cbs >= MAXSIGCB) _exit(69);
	struct sigaction sa = {.sa_handler = &onsig};
	sigaddset(&sa.sa_mask, sig);
	// block the signal so it's *only* handled when interrupting ppoll() in
	// order to avoid races
	sigprocmask(SIG_BLOCK, &sa.sa_mask, 0);
	sigfillset(&sa.sa_mask);
	sigaction(sig, &sa, 0);
	*sig_cbs_tail++ = (struct sig_cb){sig, cb};
}

bool evloop_sched(vlong deadline, void (*cb)(void *ctxt), void *ctxt) {
	struct timer *t = freelist_alloc_timer();
	if (!t) return false;
	t->cb = cb;
	t->ctxt = ctxt;
	skiplist_insert_timer(&timers, deadline, t);
	return true;
}

noreturn evloop_run(void) {
	for (;;) {
		struct timespec ts;
		struct timespec *timeout = 0;
		struct timer *nexttimer = skiplist_pop_timer(&timers);
		if (nexttimer) {
			vlong now = time_now();
			ts = (struct timespec){(nexttimer->deadline - now) / 1000,
					(nexttimer->deadline - now) % 1000 * 1000000};
			timeout = &ts;
		}
		int polled = ppoll(pfds, nfds, timeout, &(sigset_t){0});
		if (polled == -1) {
			// XXX possible robustness issue (what happens to build db, child
			// processes, etc?)
			if (errno != EINTR) errmsg_die(200, "couldn't poll events");
			for (struct sig_cb *p = sig_cbs; p != sig_cbs_tail; ++p) {
				if (sigismember(&gotsigs, p->sig)) p->cb();
			}
			sigemptyset((sigset_t *)&gotsigs);
			continue;
		}
		if (polled == 0) {
			// nexttimer _has to_ be set, no need to null check again
			nexttimer->cb(nexttimer->ctxt);
			freelist_free_timer(nexttimer);
		}
		for (int i = 0; polled; ++i) {
			if (pfds[i].revents) {
				--polled;
				if (pfds[i].revents & POLLNVAL) {
					// also shouldn't happen if I'm competent - gotta remove on
					// close!
					errmsg_warnx("tried to poll an invalid fd; this is a bug!");
				}
				else {
					fd_cbs[i].f(i, pfds[i].revents, fd_cbs[i].ctxt);
				}
			}
		}
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
