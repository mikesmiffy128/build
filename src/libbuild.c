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
	// validate strings: the IPC server code is lazy and just says
	// "invalid message" and crashes the whole build, whereas it's easier to put
	// this check in the context of each task (ie here) and then error messages
	// and return codes and whatnot will just make their way through the build
	// system as intended
	if (!workdir[0]) {
		errmsg_diex(2, "libbuild: ", msg_fatal,
				"working directory cannot be empty");
	}
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
	struct ipc_req req;
	req.type = IPC_REQ_WAIT;
	if (!ipcclient_send(sockfd, &req)) {
		errmsg_die(100, "libbuild: ", msg_fatal, "couldn't send IPC request");
	}
	// this one actually takes a reply!
	struct ipc_reply reply;
	if (!ipcclient_recv(sockfd, &reply)) {
		errmsg_die(100, "libbuild: ", msg_fatal, "couldn't read IPC reply");
	}
	return reply.maxstatus;
}

export void build_infile(const char *path) {
	if (sockfd == -1) init("build_infile");
	if (!path[0]) {
		errmsg_diex(2, "libbuild: ", msg_fatal, "infile path cannot be empty");
	}
	struct ipc_req req;
	req.type = IPC_REQ_INFILE;
	req.infile = path;
	if (!ipcclient_send(sockfd, &req)) {
		errmsg_die(100, "libbuild: ", msg_fatal, "couldn't send IPC request");
	}
}

// vi: sw=4 ts=4 noet tw=80 cc=80
