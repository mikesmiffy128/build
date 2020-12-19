#ifndef INC_XCRED_H
#define INC_XCRED_H

#include <sys/socket.h>

/*
 * We want to be able to distinguish sender PIDs to multiplex all the tasks'
 * output over the same handful of FDs, to avoid creating a crazy number of FDs.
 * Unfortunately, currently only Linux actually supports the particular
 * behaviour we want. See DevDocs/passcred.txt for the "fun" juicy details.
 *
 * Basically, this is a very sad header. Possibly a slightly angry header.
 */

#if defined(__linux__)
// this doesn't even get defined without _GNU_SOURCE, which we do not define on
// principle - so define the struct manually >:(
struct ucred { unsigned int pid, uid, gid; };
// SO_PASSCRED is defined, we just use that as our flag name
#define xcred ucred // the struct name (different in literally every OS)
#define xc_pid pid // the only field we need (*also* different in every OS)
#elif defined(__FreeBSD__) && defined(LOCAL_CREDS_PERSISTENT)
// NetBSD has struct sockcred and LOCAL_CREDS, but it only sends once, rather
// than every message, so it's not what we want.
// XXX revisit one day when NetBSD has an update.
// FreeBSD has struct sockcred as well *but* it doesn't have a pid field.
// -CURRENT adds a sockcred2 struct (sure, why not!?) along with
// LOCAL_CREDS_PERSISTENT for the desired semantics, so we can support that now
// but most people won't be able to use it until the next -RELEASE.
// XXX revisit when -RELEASE comes out so we can depend on this properly
#define SO_PASSCRED LOCAL_CREDS_PERSISTENT // alias for simplicity
#define xcred sockcred2
#define xc_pid sc_pid
// Additional notes: OpenBSD has struct sockpeercred but it only supports
// sending explictly, and illumos has its own totally weird and different cred
// stuff which also doesn't support implict/automatic sending. Oh, and macOS
// appears to have... something? - but macOS support isn't a priority in general
// since it's broken in myriad other ways anyway, and harder to test without
// owning the actual hardware.
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
