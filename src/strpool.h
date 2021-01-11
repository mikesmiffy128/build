#ifndef INC_STRPOOL_H
#define INC_STRPOOL_H

void strpool_init(void);

/*
 * copies a string into the global string pool, or uses the matching copy that's
 * already there.
 */
const char *strpool_copy(const char *tmpstr);

/*
 * claims a malloc()'d string into the global string pool, or free()s it and
 * returns what's already there.
 */
const char *strpool_move(char *mystr);

/*
 * populates the string pool with a static string constant, or returns what's
 * already there, without ever freeing anything
 */
const char *strpool_putstatic(const char *conststr);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
