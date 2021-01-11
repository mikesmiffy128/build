#include <string.h>

#include <basichashes.h>
#include <errmsg.h>
#include <intdefs.h>
#include <table.h>

// String pool (interning) allowing for O(1) string comparisons and hashing, and
// also saving some memory and making allocation lifetimes easier (everything
// just hangs around forever, or gets freed if it's a dupe).

// Important note: this is based on the idea that FNV1a + a length should never
// collide. If they ever do, we'll need a bigger hash (blake2 maybe??) to avoid
// disaster

struct ent {
	uvlong hash_and_len; // precomputed!
	const char *s;
};
DECL_TABLE(static, strpool, uvlong, struct ent)
#define hash_precomp(x) x
static inline uvlong kmemb_precomp(struct ent *e) {
	return e->hash_and_len;
}
DEF_TABLE(static, strpool, hash_precomp, table_ideq, kmemb_precomp)
static struct table_strpool tab;

static uvlong strlen_and_hash(const char *s) {
	uint len = 0;
	uint h = HASH_ITER_INIT;
	for (; *s ; ++s) { ++len; h = hash_iter(h, *s); }
	return (uvlong)h | (uvlong)len << 32;
}

void strpool_init(void) {
	if (!table_init_strpool(&tab)) {
		errmsg_die(100, msg_fatal, "couldn't create string pool");
	}
}

const char *strpool_copy(const char *tmpstr) {
	bool isnew;
	uvlong hl = strlen_and_hash(tmpstr);
	struct ent *e = table_putget_strpool(&tab, hl, &isnew);
	if (!e) return 0;
	if (isnew) {
		char *new = strdup(tmpstr);
		if (!new) {
			table_del_strpool(&tab, hl);
			return false;
		}
		e->hash_and_len = hl;
		e->s = new;
	}
	return e->s;
}

const char *strpool_move(char *mystr) {
	bool isnew;
	uvlong hl = strlen_and_hash(mystr);
	struct ent *e = table_putget_strpool(&tab, hl, &isnew);
	if (!e) return 0;
	if (isnew) {
		e->hash_and_len = hl;
		e->s = mystr;
		e->s = mystr;
	}
	else {
		free(mystr);
	}
	return e->s;
}

const char *strpool_putstatic(const char *conststr) {
	bool isnew;
	uvlong hl = strlen_and_hash(conststr);
	struct ent *e = table_putget_strpool(&tab, hl, &isnew);
	if (!e) return 0;
	if (isnew) {
		e->hash_and_len = hl;
		e->s = conststr;
	}
	return e->s;
}
