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

#ifndef INC_EVLOOP_H
#define INC_EVLOOP_H

// somewhat leaky abstraction, but oh well:
#include <poll.h>
#include <stdbool.h>

#include <intdefs.h>
#include <skiplist.h>

#define EV_IN (POLLIN | POLLPRI)
#define EV_OUT POLLOUT
#define EV_HUP POLLHUP
#define EV_ERR POLLERR

DECL_SKIPLIST_TYPE(_evloop_timer, struct evloop_timer, vlong, 4)

/*
 * embeds into another struct, deadline and cb should be set before calling
 * evloop_sched()
 */
struct evloop_timer {
	vlong deadline;
	void (*cb)(struct evloop_timer *this);
	struct skiplist_hdr__evloop_timer _hdr;
};

void evloop_init(void);

bool evloop_onfd(int fd, short events, void (*cb)(int fd, short revents,
		void *ctxt), void *ctxt);
void evloop_onfd_remove(int fd);
void evloop_onsig(int sig, void (*cb)(void));

void evloop_sched(struct evloop_timer *t);

_Noreturn void evloop_run(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
