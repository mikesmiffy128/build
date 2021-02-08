#include <stdlib.h>
#include <unistd.h>

#include <errmsg.h>

#include "defs.h"
#include "ipcclient.h"

#define export __attribute__((visibility("default")))

static int sockfd = -1;

static void init(const char *forfunc) {
	const char *sockfd_var = getenv(ENV_SOCKFD);
	if (!sockfd_var) {
		errmsg_diex(50, "libbuild: ", msg_fatal, "tried to call ", forfunc,
				" but program wasn't launched by build(1)");
	}
	sockfd = atoi(sockfd_var); // eh, just assume this is correct
}

export void build_dep(const char *const *argv, const char *workdir) {
	if (sockfd == -1) init("build_dep");
	struct ipc_req req;
	req.type = IPC_REQ_DEP;
	req.dep.argv = argv;
	req.dep.workdir = workdir;
	if (!ipcclient_send(sockfd, &req)) {
		errmsg_die(100, "libbuild: ", msg_fatal, "couldn't send IPC request");
	}
}

export int build_dep_wait(void) {
	if (sockfd == -1) init("build_dep_wait");
	struct ipc_reply reply;
	if (!ipcclient_recv(sockfd, &reply)) {
		errmsg_die(100, "libbuild: ", msg_fatal, "couldn't read IPC reply");
	}
	return reply.maxstatus;
}

export void build_infile(const char *path) {
	if (sockfd == -1) init("build_infile");
	struct ipc_req req;
	req.type = IPC_REQ_INFILE;
	req.infile = path;
	if (!ipcclient_send(sockfd, &req)) {
		errmsg_die(100, "libbuild: ", msg_fatal, "couldn't send IPC request");
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
