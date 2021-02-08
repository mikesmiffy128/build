#ifndef INC_IPCCLIENT_H
#define INC_IPCCLIENT_H

#include <stdbool.h>

#include "ipc.h"

bool ipcclient_send(int fd, const struct ipc_req *msg);
bool ipcclient_recv(int fd, struct ipc_reply *msg);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
