#ifndef INC_DEFS_H
#define INC_DEFS_H

/* various constants that are part of the build interface */

#define BUILDDB_DIR ".builddb"

#define ENV_ROOT_DIR "BUILD_ROOT_DIR"
#define ENV_SOCKFD "_BUILD_SOCK_FD" /* var name should not be relied upon! */

/* and random general structs that don't belong anywhere else */

struct task_desc {
	const char *const *argv;
	const char *workdir;
};

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
