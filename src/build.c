#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <intdefs.h>
#include <opt.h>

USAGE("[-j jobs_at_once] [-B] [command...]");

// XXX: generally we want to be running one thing at a time per CPU thread, plus
// all the blocked ones, and then each running job has file descriptors
// attached, and of course the Make jobserver involves like 1 byte of pipe
// buffer space per job. with all that in mind, make the hard limit 256; if
// computers end up having more CPU threads than that then this number can get
// bumped up (along with kernel FD limits etc.)
#define MAX_JOBS_AT_ONCE 256

int main(int argc, char *argv[]) {
	int joblim = -1;
	bool rebuild = false;

	const char *const command[] = {"./Buildfile", 0};

	FOR_OPTS(arg, argv, {
		case 'j':;
			const char *errstr;
			joblim = strtonum(OPTARG(argc, argv), 1, MAX_JOBS_AT_ONCE, &errstr);
			errmsg_warnx(msg_error, "-j value is ", errstr);
			if (errstr) {
				if (errno == EINVAL) usage();
				else exit(1) // out of range isn't really bad usage
			}
		case 'B': rebuild = true;
	});
	
	if (rebuild); // TODO(force-rebuild) put that here :)

	if (!joblim) joblim = sysconf(_SC_NPROCESSORS_ONLN);
	if (joblim > MAX_JOBS_AT_ONCE) {
		// this is a crappy error message, but unlikely to be seen by, like,
		// anyone, so whatever.
		errmsg_warnx(msg_crit, "machine has unreasonably many CPU threads; "
				"capping at 256! build will not utilise all your cores!");
		errmsg_warnx(msg_note, "increase MAX_JOBS_AT_ONCE in build.c to fix!");
	}

	if (argc) command = argv;

	return 200; // TODO(basic-core)
}

// vi: sw=4 ts=4 noet tw=80 cc=80
