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
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include <errmsg.h>
#include <intdefs.h>
#include <noreturn.h>
#include <skiplist.h>

#include "evloop.h"
#include "time.h"

#define MAXFDS 8192 // hopefully this is enough!

static int nfds = 0; // upper bound for poll() call
static struct pollfd pfds[MAXFDS] = {0};
static struct fd_cb {
	void (*f)(int, short, void *);
	void *ctxt;
} fd_cbs[MAXFDS] = {0};

#define timer_comp(x, y) ((x)->deadline - y)
#define timer_hdr(x) (&(x)->_hdr)
DEF_SKIPLIST(static, _evloop_timer, timer_comp, timer_hdr)
struct skiplist_hdr__evloop_timer timers = {0};

#define MAXSIGCB 3 // NOTE: increase as needed for the program
static sigset_t gotsigs = {0};
static void onsig(int sig) { sigaddset((sigset_t *)&gotsigs, sig); }
static struct sig_cb {
	int sig;
	void (*cb)(void);
} sig_cbs[MAXSIGCB];
static struct sig_cb *sig_cbs_tail = sig_cbs;

void evloop_init(void) { for (int i = 0; i < MAXFDS; ++i) pfds[i].fd = -1; }

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

void evloop_sched(struct evloop_timer *t) {
	skiplist_insert__evloop_timer(&timers, t->deadline, t);
}

noreturn evloop_run(void) {
	for (;;) {
		struct timespec ts;
		struct timespec *timeout = 0;
		struct evloop_timer *nexttimer = timers.x[0]; // XXX add skiplist_peek!
		if (nexttimer) {
			vlong now = time_now();
			if (nexttimer->deadline <= now) {
				// deadline passed within the time it took to get here, don't
				// wait any longer
				skiplist_pop__evloop_timer(&timers);
				nexttimer->cb(nexttimer);
				continue;
			}
			ts = (struct timespec){(nexttimer->deadline - now) / 1000,
					(nexttimer->deadline - now) % 1000 * 1000000};
			timeout = &ts;
		}
		int polled = ppoll(pfds, nfds, timeout, &(sigset_t){0});
		if (polled == -1) {
			if (errno != EINTR) {
				// XXX we just *hope* it's a temp error, otherwise we're in some
				// serious trouble
				errmsg_warn("evloop: ", msg_temp, "couldn't poll events");
				errmsg_warnx("evloop: ", msg_note, "sleeping for a moment");
				sleep(1);
			}
			for (struct sig_cb *p = sig_cbs; p != sig_cbs_tail; ++p) {
				if (sigismember(&gotsigs, p->sig)) p->cb();
			}
			sigemptyset((sigset_t *)&gotsigs);
		}
		else if (polled == 0) {
			// nexttimer _has to_ be set, no need to null check again
			skiplist_pop__evloop_timer(&timers);
			nexttimer->cb(nexttimer);
		}
		else for (int i = 0; polled; ++i) {
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
