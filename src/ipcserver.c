#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <errmsg.h>
#include <iobuf.h>
#include <str.h>

#include "db.h"
#include "fpath.h"
#include "ipc.h"

static struct ibuf *I = IBUF(-1, 16384);

// this is a bit crude but it covers "should never happen" cases, so who cares
#define INVAL(cond) \
	(!!(cond) && (errmsg_warnx(msg_error, "invalid IPC request"), \
			errno = EINVAL, 1))

static void warn_fpath(const char *msg, const char *path, enum fpath_err err) {
	if (err == FPATH_EMPTY) {
		errmsg_warnx(msg_fatal, msg, ": ", fpath_errorstring(err));
	}
	else {
		errmsg_warnx(msg_fatal, msg, " ", path, ": ", fpath_errorstring(err));
	}
}

bool ipcserver_recv(int fd, struct ipc_req *msg, const char *taskworkdir) {
	I->fd = fd; I->w = 0; I->r = 0;

	short type = ibuf_getc(I);
	if (type == -1 || type == IOBUF_EOF) return false;
	msg->type = type;

	struct str s;
	switch (msg->type) {
		case IPC_REQ_DEP:;
			int argc = 0;
			int n = ibuf_getbytes(I, &argc, sizeof(argc));
			if (n == -1 || INVAL(n != sizeof(argc))) return false;
			// FIXME this currently leaks the alloc for each and every request;
			// do we want to intern these like strings or just have logic to
			// free if already in the build db??? how would that logic work???
			const char **argv = malloc(sizeof(*argv) * (argc + 1));
			if (!argv) return false;
			for (const char **pp = argv; pp - argv < argc; ++pp) {
				s = (struct str){0};
				if (!str_clear(&s)) goto freeav;
				n = ibuf_getstr(I, &s, '\0');
				if (n == -1 || INVAL(n == 0) || !(*pp = db_intern_free(s.data))) {
					goto freeav;
				}
			}
			argv[argc] = 0;
			s = (struct str){0};
			// the workdir specified over IPC is *relative to* the task's dir
			if (!str_clear(&s) || !str_append0t(&s, taskworkdir) ||
					!str_appendc(&s, '/')) {
				goto freeav;
			}
			n = ibuf_getstr(I, &s, '\0');
			if (n == -1 || INVAL(n < 2)) goto freeav;
			// joining paths as above is pretty much guaranteed to introduce
			// silliness, but we canonicalise regardless so it's fine
			char *canon = malloc(s.sz - 1);
			if (!canon) goto freeav;
			enum fpath_err err = fpath_canon(s.data, canon, 0);
			if (err != FPATH_OK) {
				warn_fpath("invalid dependency working directory", s.data, err);
				free(canon);
				errno = EINVAL;
				goto freeav;
			}
			const char *workdir = db_intern_free(canon);
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
			// same deal, append paths and then canonicalise
			if (!str_append0t(&s, taskworkdir) || !str_appendc(&s, '/')) goto e;
			n = ibuf_getstr(I, &s, '\0');
			if (n == -1 || INVAL(n < 0)) goto e;
			canon = malloc(s.sz - 1);
			if (!canon) goto e;
			err = fpath_canon(s.data, canon, 0);
			if (err != FPATH_OK) {
				warn_fpath("invalid infile path", s.data, err);
				free(canon);
				errno = EINVAL;
				goto e;
			}
			const char *infile = db_intern_free(canon);
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
