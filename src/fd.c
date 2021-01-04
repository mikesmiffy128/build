#include <stdbool.h>
#include <unistd.h>

#include <intdefs.h>

bool fd_writeall(int fd, const char *buf, uint sz) {
	uint off = 0;
	while (sz) {
		long nwritten = write(fd, buf + off, sz);
		if (nwritten == -1) return false;
		off += nwritten; sz -= nwritten;
	}
	return true;
}

bool fd_transferall(int diskf, int to) {
	char buf[16386];
	uint off = 0, foff = 0;
	long nread;
	while (nread = pread(diskf, buf + off, sizeof(buf) - off, foff)) {
		if (nread == -1) return false;
		off += nread; foff += nread;
		if (off == sizeof(buf)) {
			if (!fd_writeall(to, buf, off)) return false;
			off = 0;
		}
	}
	return !off || fd_writeall(to, buf, off);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
