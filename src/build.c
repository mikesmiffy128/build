#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errmsg.h>
#include <intdefs.h>
#include <iobuf.h>
#include <opt.h>

#include "evloop.h"
#include "proc.h"

USAGE("[-j jobs_at_once] [-C workdir] [-B] [command...]");

// FIXME temp test code; remove obviously
static const char *const *TEMP_task1 = (const char *const[]){"sh", "-c", "sleep 1; echo 1", 0};
static const char *const *TEMP_task2 = (const char *const[]){"sh", "-c", "sleep 1; echo 2", 0};
static const char *const *TEMP_task3 = (const char *const[]){"sh", "-c", "sleep 1; echo 3", 0};
static const char *const *TEMP_task4 = (const char *const[]){"sh", "-c", "sleep 1; echo 4", 0};
static int tasksdone = 0;

static void handle_ev(int evtype, union proc_ev_param P, void *ctxt) {
	char *name = ctxt;
	if (evtype == PROC_EV_STDOUT) {
		errmsg_warnx(name, " stdout:");
		write(2, P.buf, P.sz);
	}
	else if (evtype == PROC_EV_STDERR) {
		errmsg_warnx(name, " stderr:");
		write(2, P.buf, P.sz);
	}
	else if (evtype == PROC_EV_EXIT) {
		errmsg_warnx(name, " exited");
		if (++tasksdone == 4) exit(0);
	}
}

// XXX: generally we want to be running one thing at a time per CPU thread, plus
// all the blocked ones, and then each running job has file descriptors
// attached, and we only have so many file descriptors (and so much memory)!
// with all that in mind, make the hard limit 256; if computers end up having
// more CPU threads than that then this number can get bumped up (along with
// kernel FD limits, ulimits etc.)
#define MAX_JOBS_AT_ONCE 256

int main(int argc, char *argv[]) {
	int joblim = 0;
	bool rebuild = false;

	const char *const default_command[] = {"./Buildfile", 0};
	const char *const *command = default_command;
	const char *workdir = ".";

	FOR_OPTS(argc, argv, {
		case 'j':;
			const char *errstr;
			joblim = strtonum(OPTARG(argc, argv), 1, MAX_JOBS_AT_ONCE, &errstr);
			if (errstr) {
				errmsg_warnx(msg_error, "-j value is ", errstr);
				if (errno == EINVAL) usage();
				else exit(1); // out of range isn't really bad usage
			}
			break;
		case 'B': rebuild = true; break;
		case 'C': workdir = OPTARG(argc, argv);
	});
	
	if (rebuild) {} // TODO(force-rebuild) put that here :)

	if (!joblim) joblim = sysconf(_SC_NPROCESSORS_ONLN);
	if (joblim > MAX_JOBS_AT_ONCE) {
		// this is a crappy error message, but unlikely to be seen by, like,
		// anyone, so whatever.
		errmsg_warnx(msg_crit, "machine has unreasonably many CPU threads; "
				"capping at 256! build will not utilise all your cores!");
		errmsg_warnx(msg_note, "increase MAX_JOBS_AT_ONCE in build.c to fix!");
	}

	if (argc) command = (const char *const *)argv;

	close(0);
	// /dev/null will always take the place of fd 0; don't care about clobbering
	// old stdin since we never use it anyway (and also die on error anyway)
	if (open("/dev/null", O_RDWR) == -1) {
		errmsg_die(100, msg_fatal, "can't open /dev/null");
	}

	evloop_init();
	if (mkdir(".builddb", 0755) == -1 && errno != EEXIST) {
		errmsg_die(100, msg_error, "couldn't create .builddb directory");
	}
	proc_init(joblim);
	proc_start(TEMP_task1, ".", &handle_ev, "task1");
	proc_start(TEMP_task2, ".", &handle_ev, "task2");
	proc_start(TEMP_task3, ".", &handle_ev, "task3");
	proc_start(TEMP_task4, ".", &handle_ev, "task4");
	// TODO(basic-core) do actual proper setup work here
	evloop_run();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
