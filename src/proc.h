#ifndef INC_PROC_H
#define INC_PROC_H

#include <stdbool.h>
#include <unistd.h>

#include <intdefs.h>

/* embeds into another struct (eg struct task) */
struct proc_info {
	pid_t _pid; // top-level pid; may have descendants
	int _errsock, ipcsock; // our end of each socket (ipcsock is "public")
};

enum {
	PROC_EV_STDERR,
	PROC_EV_EXIT,
	PROC_EV_UNBLOCK,
	PROC_EV_IPC,
	PROC_EV_ERROR
};
union proc_ev_param {
	struct { /* PROC_EV_STDERR */
		const char *buf;
		uint sz;
	};
	int status; /* if PROC_EV_EXIT */
	// if PROC_EV_UNBLOCK or PROC_EV_IPC, nothing
	// if PROC_EV_ERROR, nothing (errno will be set though)
};
typedef void (*proc_ev_cb)(int evtype, union proc_ev_param P,
		struct proc_info *proc);

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
 * Indicates to the process scheduler that a specific blocked task ought to
 * be unblocked again. Once this task is allowed to continue on (i.e. another
 * has finished or become blocked) the event handler will receive a
 * PROC_EV_UNBLOCK event.
 */
void proc_unblock(struct proc_info *proc);

/*
 * Kills all the task process groups that were created; call when build is about
 * to give up/crash.
 */
void proc_killall(int sig);

// XXX spaghetti variables for tui, factor these out in some nicer way later
extern uint qlen;
extern int nactive;

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
