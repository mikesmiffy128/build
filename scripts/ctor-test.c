/* This file is dedicated to the public domain. */

// see check-constructor.build - we want to make sure constructors fire properly
// since they're technically nonstandard and someone might find a compiler that
// doesn't handle them well
static int a = 1;
__attribute__((constructor)) static void ctor(void) { a = 0; }
int main(void) { return a; }

// vi: sw=4 ts=4 noet tw=80 cc=80
