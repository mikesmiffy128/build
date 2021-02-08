#ifndef INC_TABLESHARED_H
#define INC_TABLESHARED_H

#include <stdbool.h>

#include <basichashes.h>
#include <intdefs.h>

#include "defs.h"

/* a couple of hashtable-related functions used in more than one place */

static inline uint hash_task_desc(struct task_desc d) {
	uint h = HASH_ITER_INIT;
	for (const char *const *argv = d.argv; *argv; ++argv) {
		// only hash the pointers, not the strings themselves (they're interned)
		h = hash_iter_bytes(h, (const char *)argv, sizeof(*argv));
	}
	return hash_iter_bytes(h, (const char *)&d.workdir, sizeof(d.workdir));
}

static inline bool eq_task_desc(struct task_desc d1, struct task_desc d2) {
	for (const char *const *av1 = d1.argv, *const *av2 = d2.argv;;
			++av1, ++av2) {
		if (*av1 != *av2) return false;
		if (!*av1) break; // implies && !*av2
	}
	return d1.workdir == d2.workdir;
}

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
