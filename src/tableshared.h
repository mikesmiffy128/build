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
