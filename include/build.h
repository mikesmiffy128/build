#ifndef INC_BUILD_H
#define INC_BUILD_H

// TODO(basic-core)

struct task_result {
};

void build_dep(const char *const *argv, int outfd);

void build_dep_wait(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
