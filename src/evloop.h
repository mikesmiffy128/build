#ifndef INC_EVLOOP_H
#define INC_EVLOOP_H

#include <poll.h> // XXX leaky abstraction :(
#include <stdbool.h>

#include <intdefs.h>

#define EV_IN (POLLIN | POLLPRI)
#define EV_OUT POLLOUT
#define EV_HUP POLLHUP
#define EV_ERR POLLERR

void evloop_init(void);

bool evloop_onfd(int fd, short events, void (*cb)(int fd, short revents,
		void *ctxt), void *ctxt);
void evloop_onfd_remove(int fd);
bool evloop_onsig(int sig, void (*cb)(void));
bool evloop_sched(vlong deadline, void (*cb)(void *ctxt), void *ctxt);

_Noreturn void evloop_run(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
