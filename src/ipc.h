#ifndef INC_IPC_H
#define INC_IPC_H

#include <intdefs.h>

#include "defs.h"

/* this header is common to ipcserver/ipcclient - just include one of those */

enum ipc_req_type {
	IPC_REQ_DEP,
	IPC_REQ_WAIT,
	IPC_REQ_INFILE
};

struct ipc_req {
	enum ipc_req_type type;
	union {
		struct task_desc dep; // IPC_REQ_DEP
		const char *infile; // IPC_REQ_INFILE
	};
};

struct ipc_reply {
	uchar maxstatus; // yeah... this is it
};

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
