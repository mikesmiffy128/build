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

static int update(const char *path, struct db_infile *i) {
	struct stat s;
	if (stat(path, &s) == -1) {
		if (errno != ENOENT && errno != EACCES) return -1;
		if (i->len == -1ull) return 0; // no change
		i->len = -1;
		return 1;
	}
	int diff = 0;
	if (i->len   != s.st_size ) { diff = 1; i->len   = s.st_size; }
	if (i->inode != s.st_ino  ) { diff = 1; i->inode = s.st_ino;  }
	if (i->mode  != s.st_mode ) { diff = 1; i->mode  = s.st_mode; }
	if (i->uid   != s.st_uid  ) { diff = 1; i->uid   = s.st_uid;  }
	if (i->gid   != s.st_gid  ) { diff = 1; i->gid   = s.st_gid;  }
	return diff;
}

bool infile_ensure(const char *path) {
	struct db_infile *i = db_getinfile(path);
	if (!i) return false;
	if (!i->newness) {
		if (update(path, i) == -1) return -1;
		i->newness = db_newness;
		i->checked = true;
		db_commitinfile(i);
	}
	return true;
}

int infile_query(const char *path, uint tgtnewness) {
	struct db_infile *i = db_getinfile(path);
	if (i->newness > tgtnewness) return true; // checked for some *other* goal!
	if (!i->checked) {
		int r = update(path, i);
		if (r == -1) return -1;
		i->checked = true;
		if (r) {
			i->newness = db_newness;
			db_commitinfile(i);
		}
	}
	return i->newness > tgtnewness;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
