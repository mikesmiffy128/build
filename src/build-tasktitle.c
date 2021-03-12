#include <opt.h>

#include "../include/build.h"

USAGE("text");

int main(int argc, char *argv[]) {
	FOR_OPTS(argc, argv, {});
	if (argc != 1) usage();
	build_tasktitle(*argv);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
