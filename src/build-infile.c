#include <opt.h>

#include "../include/build.h"

USAGE("filename1 [filename2 ...]");

int main(int argc, char *argv[]) {
	FOR_OPTS(argc, argv, {});
	if (!argc) usage();
	for (; *argv; ++argv) build_infile(*argv);
}

// vi: sw=4 ts=4 noet tw=80 cc=80
