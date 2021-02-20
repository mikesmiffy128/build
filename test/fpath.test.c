#include "test.h"
{.desc = "file path manipulation"};

#include <limits.h>
#include <string.h>

#include "../src/fpath.c"

TEST("canonicalisation should copy canonical paths as-is", 0) {
	char buf[PATH_MAX];
	fpath_canon("abc/def", buf, 0);
	if (strcmp(buf, "abc/def")) return false;
	fpath_canon(".", buf, 0);
	if (strcmp(buf, ".")) return false;
	fpath_canon("aaa", buf, 0);
	if (strcmp(buf, "aaa")) return false;
	return true;
}

TEST("canonicalisation should backtrack properly on /.. at end", 0) {
	char buf[PATH_MAX];
	fpath_canon("abc/def/..", buf, 0);
	if (strcmp(buf, "abc")) return false;
	fpath_canon("abc/def/../..", buf, 0);
	if (strcmp(buf, ".")) return false;
	return true;
}

TEST("canonicalisation should overwrite properly on /../x", 0) {
	char buf[PATH_MAX];
	fpath_canon("abc/def/../xyz", buf, 0);
	if (strcmp(buf, "abc/xyz")) return false;
	fpath_canon("abc/def/../../xyz", buf, 0);
	if (strcmp(buf, "xyz")) return false;
	fpath_canon("./aa/./../b/../c/d", buf, 0);
	if (strcmp(buf, "c/d")) return false;
	return true;
}

TEST("canonicalisation should properly report errors", 0) {
	char buf[PATH_MAX];
	return fpath_canon("/usr/bin/grep", buf, 0) == FPATH_ABSOLUTE;
	return fpath_canon("aa/../../b", buf, 0) == FPATH_OUTSIDE;
	return fpath_canon("aa/../b/", buf, 0) == FPATH_TRAILSLASH;
}

TEST("canonicalisation should deduplicate slashes", 0) {
	char buf[PATH_MAX];
	fpath_canon("hello/////world", buf, 0);
	return !strcmp(buf, "hello/world");
}

// vi: sw=4 ts=4 noet tw=80 cc=80
