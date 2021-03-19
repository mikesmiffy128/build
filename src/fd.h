/* This file is dedicated to the public domain. */

#ifndef INC_FD_H
#define INC_FD_H

#include <stdbool.h>

#include <intdefs.h>

bool fd_writeall(int fd, const void *buf, uint sz);
bool fd_transferall(int diskf, int to);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
