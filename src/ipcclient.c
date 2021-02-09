#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include <iobuf.h>

#include "ipc.h"

static struct obuf *O = OBUF(-1, 16384);

bool ipcclient_send(int fd, const struct ipc_req *msg) {
	O->fd = fd; O->n = 0;

	if (!obuf_putc(O, msg->type)) return false;
	switch (msg->type) {
		case IPC_REQ_DEP:;
			int argc = 0;
			const char *const *argv = msg->dep.argv;
			for (const char *const *pp = argv; *pp; ++pp) ++argc;
			if (!obuf_putbytes(O, (char *)&argc, sizeof(argc))) return false;
			for (const char *const *pp = argv; *pp; ++pp) {
				if (!obuf_put0t(O, *pp) || !obuf_putc(O, '\0')) return false;
			}
			if (!obuf_put0t(O, msg->dep.workdir) || !obuf_putc(O, '\0')) {
				return false;
			}
			break;
		case IPC_REQ_WAIT: break; // nothing else!
		case IPC_REQ_INFILE:
			if (!obuf_put0t(O, msg->infile) || !obuf_putc(O, '\0')) {
				return false;
			}
	}
	return obuf_flush(O);
}

bool ipcclient_recv(int fd, struct ipc_reply *msg) {
	do {
		int nread = read(fd, &msg->maxstatus, 1);
		if (nread == 1) return true;
		if (nread == 0) return false;
	} while (errno == EINTR); // assuming -1
	return false;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
