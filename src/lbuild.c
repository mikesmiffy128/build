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

#include <stdlib.h>

#include LUA_HEADER
#include LUA_AUX_HEADER

#include <noreturn.h>

#include "../include/build.h"
#include "unreachable.h"

// lua api doesn't have noreturn annotations :(
static inline noreturn _wrap_error(lua_State *L, const char *s) {
	luaL_error(L, s);
	unreachable;
}
static inline noreturn _wrap_argerror(lua_State *L, int a, const char *s) {
	luaL_argerror(L, a, s);
	unreachable;
}
#define luaL_error(L, s) _wrap_error(L, s)
#define luaL_argerror(L, a, s) _wrap_argerror(L, a, s)

#if LUA_REVISION == 1
#define lua_rawlen lua_objlen
#endif

#define export __attribute__((visibility("default")))

int argcmax = 0;
const char **argv = 0;

static int f_dep(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int argc = lua_rawlen(L, 1);
	if (argc < 1) luaL_argerror(L, 1, "argv length must be at least 1");
	if (argc > argcmax) {
		const char **newargv = reallocarray(argv, argc + 1, sizeof(*argv));
		if (!newargv) luaL_error(L, "panic: out of memory");
		argcmax = argc;
		argv = newargv;
	}
	for (int i = 1; i <= argc; ++i) {
		lua_rawgeti(L, 1, i);
		argv[i - 1] = luaL_checkstring(L, -1);
		lua_pop(L, 1);
	}
	argv[argc] = 0;
	build_dep(argv, luaL_checkstring(L, 2));
	return 0;
}

static int f_dep_wait(lua_State *L) {
	lua_pushinteger(L, build_dep_wait());
	return 1;
}

static int f_infile(lua_State *L) {
	build_infile(luaL_checkstring(L, 1));
	return 0;
}

static int f_tasktitle(lua_State *L) {
	build_tasktitle(luaL_checkstring(L, 1));
	return 0;
}

#define ADDF(name) do { \
	lua_pushliteral(L, #name); \
	lua_pushcfunction(L, &f_##name); \
	lua_settable(L, -3); \
} while (0)

/*
 * API usage something along the lines of:
 *   local build = require("lbuild")
 *   build.dep({"./cmd", "arg"}, "")
 *   local status = build.dep_wait()
 *   if status ~= 0 then
 *       # oh no!
 *   endif
 */
export int luaopen_lbuild(lua_State *L) {
	lua_newtable(L);
	ADDF(dep); ADDF(dep_wait); ADDF(infile); ADDF(tasktitle);
	return 1;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
