/* This file is dedicated to the public domain. */

#ifndef INC_BLAKE2B_H
#define INC_BLAKE2B_H

#include <intdefs.h>

#define BLAKE2B_BLOCKBYTES		128
#define BLAKE2B_OUTBYTES		 64
#define BLAKE2B_KEYBYTES		 64
#define BLAKE2B_SALTBYTES		 16
#define BLAKE2B_PERSONALBYTES	 16

struct blake2b_state {
	u64		h[8];
	u64		t[2];
	u64		f[2];
	u8		buf[BLAKE2B_BLOCKBYTES];
	ulong	buflen;
	ulong	outlen;
	u8		last_node;
};

struct blake2b_param {
	u8	digest_length;
	u8	key_length;
	u8	fanout;
	u8	depth;
	u32 leaf_length;
	u32	node_offset;
	u32	xof_length;
	u8	node_depth;
	u8	inner_length;
	u8	reserved[14];
	u8	salt[BLAKE2B_SALTBYTES];
	u8	personal[BLAKE2B_PERSONALBYTES];
};

_Static_assert(sizeof(struct blake2b_param) == BLAKE2B_OUTBYTES,
		"yikes, something is wrong with your alignment!");

/* Streaming API */
void blake2b_init(struct blake2b_state *S, ulong outlen);
void blake2b_init_key(struct blake2b_state *S, ulong outlen, const void *key,
		ulong keylen);
void blake2b_init_param(struct blake2b_state *S, const struct blake2b_param *P);
void blake2b_update(struct blake2b_state *S, const void *in, ulong inlen);
void blake2b_final(struct blake2b_state *S, void *out, ulong outlen);

/* Simple API */
void blake2b(void *out, ulong outlen, const void *in, ulong inlen,
		const void *key, ulong keylen);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
