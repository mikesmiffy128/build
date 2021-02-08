#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <iobuf.h>
#include <str.h>

#include "db.h"
#include "ipc.h"

static struct ibuf *I = IBUF(-1, 16384);

bool ipcserver_recv(int fd, struct ipc_req *msg) {
	I->fd = fd; I->w = 0; I->r = 0;

	int type = ibuf_getc(I);
	if (type == -1 || type == IOBUF_EOF) return false;
	msg->type = type;

	struct str s;
	switch (msg->type) {
		case IPC_REQ_DEP:;
			int argc = 0;
			if (ibuf_getbytes(I, &argc, sizeof(argc)) != sizeof(argc)) {
				return false;
			}
			// FIXME this currently leaks the alloc for each and every request;
			// do we want to intern these like strings or just have logic to
			// free if already in the build db???
			const char **argv = malloc(sizeof(*argv) * (argc + 1));
			if (!argv) return false;
			for (const char **pp = argv; pp - argv < argc; ++pp) {
				s = (struct str){0};
				if (!str_clear(&s) || ibuf_getstr(I, &s, '\0') < 1 ||
						s.data[s.sz - 2] != '\0' || !(*pp = db_intern(s.data))) {
					goto freeav;
				}
			}
			argv[argc] = 0;
			s = (struct str){0};
			if (ibuf_getstr(I, &s, '\0') < 1) goto freeav;
			if (s.data[s.sz - 2] != '\0') goto freeav;
			const char *workdir = db_intern(s.data);
			if (!workdir) goto freeav;
			msg->dep.argv = argv;
			msg->dep.workdir = workdir;
			break;
freeav:		free(argv);
			goto e;
		case IPC_REQ_WAIT: break; // nothing else!
		case IPC_REQ_INFILE:;
			s = (struct str){0};
			if (!str_clear(&s)) return false;
			if (ibuf_getstr(I, &s, '\0') < 1) goto e;
			if (s.data[s.sz - 2] != '\0') goto e;
			const char *infile = db_intern(s.data);
			if (!infile) goto e;
			msg->infile = infile;
	}
	return true;

e:	free(s.data);
	return false;
}

bool ipcserver_send(int fd, const struct ipc_reply *msg) {
	return write(fd, msg, 1) != -1; // lol
}

// vi: sw=4 ts=4 noet tw=80 cc=80
