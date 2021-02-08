#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <intdefs.h>
#include <iobuf.h>
#include <str.h>
#include <table.h>

#include "build.h"
#include "db.h"
#include "unreachable.h"

static int update(const char *path, struct db_infile *i) {
	struct stat s;
	if (stat(path, &s) == -1) {
		if (errno != ENOENT && errno != EACCES) return -1;
		if (i->len == -1ull) return 0; // no change
		i->len = -1;
		return 1;
	}
	int diff = 0;
	if (s.st_size != i->len  ) { diff = 1; i->len   = s.st_size; }
	if (s.st_ino  != i->inode) { diff = 1; i->inode = s.st_ino;  }
	if (s.st_mode != i->mode ) { diff = 1; i->mode  = s.st_mode; }
	if (s.st_uid  != i->uid  ) { diff = 1; i->uid   = s.st_uid;  }
	if (s.st_gid  != i->gid  ) { diff = 1; i->gid   = s.st_gid;  }
	return diff;
}

bool infile_ensure(const char *path) {
	struct db_infile *i = db_getinfile(path);
	if (!i) return false;
	if (!i->newness) db_commitinfile(i); // if totally new, ensure saved to disk
	return true;
}

uint infile_query(const char *path, uint tgtnewness) {
	struct db_infile *i = db_getinfile(path);
	if (i->newness >= tgtnewness) return i->newness;
	switch (update(path, i)) {
		case -1: return -1u;
		case 1: i->newness = db_newness;
		case 0: db_commitinfile(i);
				return i->newness;
	}
	unreachable;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
