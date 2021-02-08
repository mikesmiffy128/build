#include <stdbool.h>

#include <opt.h>

#include "../include/build.h"

USAGE("[[-n] [-C workdir] command...] [-w]");

int main(int argc, char *argv[]) {
	const char *workdir = ".";
	bool nonblock = false;
	bool waitall = false;
	FOR_OPTS(argc, argv, {
		case 'C': workdir = OPTARG(argc, argv);
		// doesn't really make any sense to have both these flags set...
		case 'n': nonblock = true; waitall = false; break;
		case 'w': waitall = true; nonblock = false;
	});
	if (waitall) {
		if (argc) usage();
		return build_dep_wait();
	}
	if (!argc) usage();
	build_dep((const char *const *)argv, workdir);
	if (!nonblock) return build_dep_wait();
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
