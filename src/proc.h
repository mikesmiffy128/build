#ifndef INC_PROC_H
#define INC_PROC_H

#include <stdbool.h>

#include <intdefs.h>

enum {
	PROC_EV_STDOUT,
	PROC_EV_STDERR,
	PROC_EV_EXIT,
	PROC_EV_UNBLOCK,
	PROC_EV_ERROR
};
union proc_ev_param {
	struct { /* if PROC_EV_STDOUT or PROC_EV_STDERR */
		const char *buf;
		uint sz;
	};
	int status; /* if PROC_EV_EXIT */
	// if PROC_EV_UNBLOCK, nothing
	struct { /* if PROC_EV_ERROR */
		int XXX; // ???
	};
};
typedef void (*proc_ev_cb)(int evtype, union proc_ev_param P, void *ctxt);

void proc_init(int maxparallel);
bool proc_start(const char *const *argv, const char *workdir, proc_ev_cb cb,
		void *ctxt);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
