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

#ifndef INC_IPC_H
#define INC_IPC_H

#include <intdefs.h>

#include "defs.h"

/* this header is common to ipcserver/ipcclient - just include one of those */

enum ipc_req_type {
	IPC_REQ_DEP,
	IPC_REQ_WAIT,
	IPC_REQ_INFILE,
	IPC_REQ_TASKTITLE, // note: NOT interned on server, unlike most strings
};

struct ipc_req {
	enum ipc_req_type type;
	union {
		struct task_desc dep; // IPC_REQ_DEP
		const char *infile; // IPC_REQ_INFILE
		char *title; // IPC_REQ_TASKTITLE
	};
};

struct ipc_reply {
	uchar maxstatus; // yeah... this is it
};

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
