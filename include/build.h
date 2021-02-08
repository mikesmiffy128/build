#ifndef INC_BUILD_H
#define INC_BUILD_H

/*
 * Tells build that the currently-running task depends on the task specified by
 * `argv` and `workdir`, and causes that task to either begin running once the
 * necessary resources are available, or have its existing result used if
 * already up-to-date.
 *
 * The build-dep program with the -n flag essentially calls this function and
 * then exits. The build-dep program with no flags calls this and then the
 * build_dep_wait() function, and exits with the status returned from the
 * latter.
 */
void build_dep(const char *const *argv, const char *workdir);

/*
 * Causes the current process to block until every requested dependency so far
 * has finished running, and returns the *highest* exit status code from all of
 * those (if no tasks have been requested, returns 0; a no-op will always be
 * successful).
 *
 * It is okay to call this once and then queue up more dependencies, and then
 * call this again, and so on.
 *
 * It is *not* okay for a task to exit without calling this; a task should
 * always finish after its dependencies.
 *
 * The build-dep program with the -w flag essentially calls this function and
 * exits with whatever status is returned.
 */
int build_dep_wait(void);

/*
 * Tells build that the currently-running task depends on access to and/or the
 * contents of the file at `path`. This information will be used to determine
 * whether or not the task needs to be rerun in the future (or whether its
 * cached result can be reused).
 */
void build_infile(const char *path);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
