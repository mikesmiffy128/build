/*
 * Copyright © 2021 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdbool.h>

#include <errmsg.h>
#include <opt.h>

#include "../include/build.h"

USAGE("[-n] [-C workdir] [command...] [-w]");

int main(int argc, char *argv[]) {
	const char *workdir = "."; bool hasworkdir = false;
	bool nonblock = false;
	bool waitall = false;
	FOR_OPTS(argc, argv, {
		case 'C': workdir = OPTARG(argc, argv); hasworkdir = true; break;
		// doesn't really make any sense to have both these flags set...
		case 'n': nonblock = true; waitall = false; break;
		case 'w': waitall = true; nonblock = false;
	});
	if (waitall) {
		if (argc) usage();
		return build_dep_wait();
	}
	if (!argc) {
		if (!hasworkdir) {
			errmsg_warnx("expected a workdir and/or command");
			usage();
		}
		*--argv = "./Buildfile"; // just cram that in...
	}
	build_dep((const char *const *)argv, workdir);
	if (!nonblock) return build_dep_wait();
	return 0;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
