#include <intdefs.h>
#include <opt.h>

USAGE("command...");

int main(int argc, char *argv[]) {
	FOR_OPTS(argc, argv, {});
	if (!argc) usage();
	return 200; // TODO(basic-core)
}

// vi: sw=4 ts=4 noet tw=80 cc=80
