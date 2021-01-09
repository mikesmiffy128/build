#ifndef INC_PROC_H
#define INC_PROC_H

#include <stdbool.h>
#include <unistd.h>

#include <intdefs.h>

struct proc_info;

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
	// if PROC_EV_ERROR, nothing (errno will be set though)
};
typedef void (*proc_ev_cb)(int evtype, union proc_ev_param P,
		struct proc_info *proc);

/* embeds into another struct, should be treated as opaque/private */
struct proc_info {
	pid_t _pid;
	int _outsock[2], _errsock[2];
};

void proc_init(proc_ev_cb ev_cb);
void proc_start(struct proc_info *proc, const char *const *argv,
		const char *workdir);

/*
 * Indicates to the process scheduler that one currently-running process has
 * stopped doing work for the time being, allowing another process to
 * potentially be started in its place.
 */
void proc_block(void);

/*
 * Indicates to the process scheduler that a specific blocked process would like
 * to start doing work again; the event callback will be given PROC_EV_UNBLOCK
 * when this is permitted to happen.
 */
void proc_unblock(struct proc_info *proc);

// XXX spaghetti variables for tui, factor these out in some nicer way later */
extern uint qlen;
extern int nactive;

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
