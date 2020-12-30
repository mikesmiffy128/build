#ifndef INC_TASK_H
#define INC_TASK_H

/*
 * note: also responsible for calling proc_init; the task API is essentially a
 * layer on top of proc stuff
 */
void task_init(int maxparallel);

/* this one is called *once* with the main task to kick everything off */
void task_goal(const char *const *argv, const char *workdir);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
