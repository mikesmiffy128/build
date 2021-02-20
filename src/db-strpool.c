#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <basichashes.h>
#include <errmsg.h>
#include <intdefs.h>
#include <iobuf.h>
#include <table.h>
#include <vec.h>

#include "db.h"
#include "fd.h"

// String pool (interning) allowing for O(1) string comparisons and hashing, and
// also saving some memory and making allocation lifetimes easier (everything
// just hangs around forever, or gets freed if it's a dupe).
// Strings are additionally appended to a file disk upon initial interning and
// reloaded later so the dependency database can reference strings using
// fixed-length IDs, saving space.

static int fd;

// Important note: this table is based on the idea that FNV1a + a length should
// never collide. If they ever do, we'll need a bigger hash (blake2 maybe??) to
// avoid disaster.
struct ent {
	uvlong hash_and_len; // precomputed!
	uint idx; // position in ordered list (below)
	// char padding[4];
	const char *s;
};
DECL_TABLE(static, strpool, uvlong, struct ent)
#define hash_precomp(x) x
static inline uvlong kmemb_precomp(struct ent *e) { return e->hash_and_len; }
DEF_TABLE(static, strpool, hash_precomp, table_ideq, kmemb_precomp)
static struct table_strpool tab;

static uvlong strlen_and_hash(const char *s) {
	uint len = 0;
	uint h = HASH_ITER_INIT;
	for (; *s ; ++s) { ++len; h = hash_iter(h, *s); }
	return (uvlong)h | (uvlong)len << 32;
}

struct list VEC(const char *);
static struct list indexed = {0};

void strpool_init(void) {
	if (!table_init_strpool(&tab)) {
		errmsg_die(100, msg_fatal, "couldn't create string pool table");
	}
	fd = openat(db_dirfd, "strings", O_CREAT | O_RDWR | O_CLOEXEC, 0644);
	if (fd == -1) errmsg_die(100, "couldn't open .builddb/strings");
	union {
		struct ibuf b;
		char x[sizeof(struct ibuf) + 65536];
	} _b;
	struct ibuf *b = &_b.b;
	*b = (struct ibuf){fd, 65536};
	for (;;) {
		uvlong hl;
		int nread = ibuf_getbytes(b, &hl, sizeof(hl));
		if (nread == -1) goto e;
		if (nread == 0) break; // EOF here is fine! we're done!
		if (nread != sizeof(hl)) goto eof;
		uint len = hl >> 32;
		char *s = malloc(len + 1);
		if (!s) goto e;
		s[len] = '\0';
		// XXX/FIXME: getbytes len can't be this big, potential overflow
		// (although in practice no string should be this big)
		nread = ibuf_getbytes(b, s, len);
		if (nread == -1) goto e;
		if (nread != len) goto eof;
		// vec_pop(&s); // remove the extra \0 - doesn't actually matter here
		struct ent *e = table_put_strpool(&tab, hl);
		if (!e) goto e;
		e->hash_and_len = hl;
		e->s = s;
		if (!vec_push(&indexed, s)) goto e;
		e->idx = indexed.sz - 1;
	}

	return;
eof:errmsg_diex(2, msg_fatal, "invalid strings file: unexpected EOF");
e:	errmsg_die(100, msg_fatal, "couldn't load strings database");
}

const char *db_intern(const char *s) {
	bool isnew;
	uvlong hl = strlen_and_hash(s);
	struct ent *e = table_putget_transact_strpool(&tab, hl, &isnew);
	if (!e) return 0;
	if (isnew) {
		if (!vec_push(&indexed, s)) return 0;
		uint len = hl >> 32;
		vlong pos = lseek(fd, 0, SEEK_CUR);
		// XXX this isn't power-fail-safe, must decide whether I care about that
		if (!fd_writeall(fd, &hl, sizeof(hl)) || !fd_writeall(fd, s, len)) {
			// attempt to roll back - this *shouldn't* fail; if it does there
			// are probably bigger fish to fry
			lseek(fd, pos, SEEK_SET);
			ftruncate(fd, pos);
			return 0;
		}
		e->hash_and_len = hl;
		e->s = s;
		e->idx = indexed.sz - 1;
		table_transactcommit_strpool(&tab);
	}
	return e->s;
}

const char *db_intern_free(char *s) {
	const char *ret = db_intern(s);
	if (ret && ret != s) free(s);
	return ret;
}

// assumes the string is actually in there
uint strpool_getidx(const char *s) {
	return table_get_strpool(&tab, strlen_and_hash(s))->idx;
}

// assumes the index is actually in there
const char *strpool_fromidx(uint idx) {
	return indexed.data[idx];
}
