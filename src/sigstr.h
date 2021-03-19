/* This file is dedicated to the public domain. */

#ifndef INC_SIGSTR_H
#define INC_SIGSTR_H

// XXX consider moving this or something like it into cbits if it proves useful
// elsewhere

/*
 * Returns a symbolic name for the given signal (not including the SIG prefix).
 * RT signals use a static buffer, so calling this twice may clobber the old
 * result.
 */
const char *sigstr(int sig);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
