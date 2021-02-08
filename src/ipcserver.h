#ifndef INC_IPCSERVER_H
#define INC_IPCSERVER_H

#include <stdbool.h>

#include "ipc.h"

bool ipcserver_recv(int fd, struct ipc_req *msg);
bool ipcserver_send(int fd, const struct ipc_reply *msg);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
