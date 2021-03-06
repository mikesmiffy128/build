#include <signal.h>

#include <fmt.h>

const char *sigstr(int sig) {
	static char rtbuf[10] = "RT";
	if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
		fmt_fixed_u32(rtbuf + 2, sig - SIGRTMAX + 1);
		return rtbuf;
	}
	switch (sig) {
		case SIGABRT: return "ABRT";
		case SIGALRM: return "ALRM";
#ifdef SIGALRM1
		case SIGALRM1: return "ALRM1";
#endif
		case SIGBUS: return "BUS";
#ifdef SIGCANCEL
		case SIGCANCEL: return "CANCEL";
#endif
		case SIGCHLD: return "CHLD";
		case SIGCONT: return "CONT";
#ifdef SIGDANGER
		case SIGDANGER: return "DANGER";
#endif
#ifdef SIGDIL
		case SIGDIL: return "DIL";
#endif
#ifdef SIGEMT
		case SIGEMT: return "EMT";
#endif
		case SIGFPE: return "FPE";
#ifdef SIGFREEZE
		case SIGFREEZE: return "FREEZE";
#endif
#ifdef SIGGRANT
		case SIGGRANT: return "GRANT";
#endif
		case SIGHUP: return "HUP";
		case SIGILL: return "ILL";
		case SIGINT: return "INT";
#if defined(SIGIO) && SIGIO != SIGPOLL
		case SIGIO: return "IO";
#endif
#if defined(SIGIOT) && SIGIOT != SIGABRT
		case SIGIOT: return "IOT";
#endif
#ifdef SIGKAP
		case SIGKAP: return "KAP";
#endif
		case SIGKILL: return "KILL";
#ifdef SIGKILLTHR
		case SIGKILLTHR: return "KILLTHR";
#endif
#ifdef SIGLOST
		case SIGLOST: return "LOST";
#endif
#ifdef SIGLWP
		case SIGLWP: return "LWP";
#endif
#ifdef SIGMIGRATE
		case SIGMIGRATE: return "MIGRATE";
#endif
#ifdef SIGMSG
		case SIGMSG: return "MSG";
#endif
		case SIGPIPE: return "PIPE";
#ifdef SIGPOLL
		case SIGPOLL: return "POLL";
#endif
#ifdef SIGPRE
		case SIGPRE: return "PRE";
#endif
		case SIGPROF: return "PROF";
#ifdef SIGPWR
		case SIGPWR: return "PWR";
#endif
		case SIGQUIT: return "QUIT";
#ifdef SIGRETRACT
		case SIGRETRACT: return "RETRACT";
#endif
#ifdef SIGSAK
		case SIGSAK: return "SAK";
#endif
		case SIGSEGV: return "SEGV";
#ifdef SIGSOUND
		case SIGSOUND: return "SOUND";
#endif
		case SIGSTOP: return "STOP";
		case SIGSYS: return "SYS";
		case SIGTERM: return "TERM";
#ifdef SIGTHAW
		case SIGTHAW: return "THAW";
#endif
		case SIGTRAP: return "TRAP";
		case SIGTSTP: return "TSTP";
		case SIGTTIN: return "TTIN";
		case SIGTTOU: return "TTOU";
		case SIGURG: return "URG";
		case SIGUSR1: return "USR1";
		case SIGUSR2: return "USR2";
#ifdef SIGVIRT
		case SIGVIRT: return "VIRT";
#endif
		case SIGVTALRM: return "VTALRM";
#ifdef SIGWAITING
		case SIGWAITING: return "WAITING";
#endif
		case SIGWINCH: return "WINCH";
#ifdef SIGWINDOW
		case SIGWINDOW: return "WINDOW";
#endif
		case SIGXCPU: return "XCPU";
		case SIGXFSZ: return "XFSZ";
	}
	return "UNKNOWN"; // better than nothing
}

// vi: sw=4 ts=4 noet tw=80 cc=80
