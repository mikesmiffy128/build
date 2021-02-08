#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errmsg.h>
#include <intdefs.h>
#include <iobuf.h>
#include <opt.h>

#include "db.h"
#include "defs.h"
#include "evloop.h"
#include "fpath.h"
#include "task.h"
#include "tui.h"

#include "infile.h"

USAGE("[-j tasks_at_once] [-C workdir] [-B] [command...]");

// XXX: generally we want to be running one thing at a time per CPU thread, plus
// all the blocked ones, and then each running job has file descriptors
// attached, and we only have so many file descriptors (and so much memory)!
// with all that in mind, make the hard limit 256; if computers end up having
// more CPU threads than that then this number can get bumped up (along with
// kernel FD limits, ulimits etc.)
#define MAX_JOBS_AT_ONCE 256

// spaghetti variables (build.h)
int maxpar = 0;

int main(int argc, char *argv[]) {
	bool cleanbuild = false;
	const char *default_command[] = {"./Buildfile", 0};
	const char **command = default_command;
	const char *workdir = ".";

	FOR_OPTS(argc, argv, {
		case 'j':;
			const char *errstr;
			maxpar = strtonum(OPTARG(argc, argv), 1, MAX_JOBS_AT_ONCE, &errstr);
			if (errstr) {
				errmsg_warnx(msg_error, "-j value is ", errstr);
				if (errno == EINVAL) usage();
				else exit(1); // out of range isn't really bad usage
			}
			break;
		case 'B': cleanbuild = true; break;
		case 'C': workdir = OPTARG(argc, argv);
	});
	
	if (cleanbuild) {} // TODO(force-rebuild) put that here :)

	if (!maxpar) maxpar = sysconf(_SC_NPROCESSORS_ONLN);
	if (maxpar > MAX_JOBS_AT_ONCE) {
		// this is a crappy error message, but unlikely to be seen by, like,
		// anyone, so whatever.
		errmsg_warnx(msg_crit, "machine has unreasonably many CPU threads; "
				"capping at 256! build will not utilise all your cores!");
		errmsg_warnx(msg_note, "increase MAX_JOBS_AT_ONCE in build.c to fix!");
		maxpar = 256;
	}
	if (argc) command = (const char **)argv;

	// replace stdin and stdout with /dev/null so people don't use build wrong.
	// any user can have a build system painted any colour that he wants so long
	// as it is black.
	close(0); close(1);
	if (open("/dev/null", O_RDWR) == -1 || dup(0) == -1) {
		errmsg_die(100, msg_fatal, "couldnt't open /dev/null");
	}

	evloop_init();
	db_init();
	for (const char **pp = command; *pp; ++pp) {
		const char *s = db_intern(*pp);
		if (!s) errmsg_die(100, msg_fatal, "couldn't intern string");
		*pp = s; // just reuse the argv space - why not? deps that come in later
				 // will get malloced of course
	}
	char canonworkdir[PATH_MAX];
	enum fpath_err e = fpath_canon(workdir, canonworkdir, 0);
	if (e != FPATH_OK) {
		errmsg_diex(2, msg_fatal, "invalid working directory (-C) given: ",
				fpath_errorstring(e));
	}
	workdir = db_intern(canonworkdir);
	if (!workdir) errmsg_die(100, msg_fatal, "couldn't intern string");
	if (isatty(2)) {
		tui_init(2);
	}
	else {
		int fd = open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY);
		if (fd != -1) tui_init(fd);
	}
	task_init();
	task_goal(command, workdir);
	evloop_run();
}

// vi: sw=4 ts=4 noet tw=80 cc=80
