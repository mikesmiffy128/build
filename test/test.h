/*
 * test.h - Michael Smith <mikesmiffy128@gmail.com>
 * I hereby dedicate the contents of this file to the public domain. In
 * jurisdictions with no public domain, go you your supreme court and get a
 * public domain.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>

#ifdef __clang__
#define _TEST_SILENCE_CLANG \
	_Pragma("clang diagnostic push") \
	_Pragma("clang diagnostic ignored \"-Winitializer-overrides\"")
#define _TEST_UNSILENCE_CLANG \
	_Pragma("clang diagnostic pop")
#else
#define _TEST_SILENCE_CLANG
#define _TEST_UNSILENCE_CLANG
#endif

static struct _test_desc {
	char *desc;
	int default_flags;
} _test_desc;

static struct _test {
	char *desc;
	/* Optional attributes that can be set to further customise the test case */
	// Note: the first attribute here has to have a default value of 0 because
	// of __VA_ARGS__ requiring at least one argument
	int expected_exit;	/* expected exit from forked child (255 is reserved!) */
	int flags;			/* see flags below */
	int timeout;		/* milisecond timeout on forked child (if forking) */
	bool (*_f)(void);
	struct _test *_next;
} *_tests = 0, **_tests_tail = &_tests;
static int _ntests = 0;

/* Test flags - currently just NOFORK but you could add your own custom ones! */
#define NOFORK 1

#define _TEST_USE_DEFAULT_FLAGS -1 // indicator to use global default_flags
#define _TEST_DEFAULT_TIMEOUT 1000 // 1s seems reasonable

#define _TESTCAT1(a, b) a##b
#define _TESTCAT(a, b) _TESTCAT1(a, b)
#define _TESTSTR1(x) #x
#define _TESTSTR(x) _TESTSTR1(x)
#define TEST(...) \
	static bool _TESTCAT(_test_f_, __LINE__)(void); \
	_TEST_SILENCE_CLANG \
	static struct _test _TESTCAT(_test_, __LINE__) = { \
		.flags = _TEST_USE_DEFAULT_FLAGS, \
		.timeout = _TEST_DEFAULT_TIMEOUT, \
		.desc = __FILE__":"_TESTSTR(__LINE__)": " __VA_ARGS__, \
		._f = &_TESTCAT(_test_f_, __LINE__) \
	}; \
	_TEST_UNSILENCE_CLANG \
	/* constructor adds tests to the list tail to run them in order */ \
	__attribute__((constructor(100 + __LINE__))) \
	static void _TESTCAT(_test_init_, __LINE__)(void) { \
		if (_TESTCAT(_test_, __LINE__).flags == _TEST_USE_DEFAULT_FLAGS) { \
			_TESTCAT(_test_, __LINE__).flags = _test_desc.default_flags; \
		} \
		_TESTCAT(_test_, __LINE__)._next = *_tests_tail; \
		*_tests_tail = &_TESTCAT(_test_, __LINE__); \
		_tests_tail = &_TESTCAT(_test_, __LINE__)._next; \
		++_ntests; \
	} \
	static bool _TESTCAT(_test_f_, __LINE__)(void)

static sigset_t _test_sigmask = {0};

static bool _run_test(struct _test *t) {
	if (t->flags & NOFORK) return t->_f();

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return false;
	}
	if (!pid) {
		bool ret = t->_f();
		if (!ret) exit(255);
		exit(t->expected_exit);
	}
	if (t->timeout) {
		struct timespec ts = { t->timeout / 1000, (t->timeout % 1000) * 1000000 };
		if (!pselect(0, 0, 0, 0, &ts, &_test_sigmask)) {
			// if pselect returned zero it must've timed out
			fprintf(stderr, "child process timed out after %d milliseconds\n",
					t->timeout);
			kill(pid, SIGKILL); // XXX should this be a less harsh signal?
			waitpid(pid, 0, 0); // still have to reap the zombie process
			return false;
		}
	}
	// either there was no timeout value or pselect errored meaning we should
	// have something to wait() on!
	int status;
	wait(&status);
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status) == t->expected_exit;
	}
	else /* WIFSIGNALED(status) */ {
		fprintf(stderr, "child process killed by signal %d (%s)\n",
				WTERMSIG(status), strsignal(WTERMSIG(status)));
		return false;
	}
}

static void _test_sigchld(int sig) {}

/*
 * Main test driver, does the important stuff
 */
int main(void) {
	// set up no-op SIGCHLD handling so we can (ab)use pselect() to do race-free
	// timeouts
	struct sigaction sa;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = &_test_sigchld;
	sigaction(SIGCHLD, &sa, 0);
	sigaddset(&_test_sigmask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &_test_sigmask, 0);
	sigemptyset(&_test_sigmask);

	int thistest = 1;
	bool failed = false;
	for (struct _test *t = _tests; t; t = t->_next, ++thistest) {
		if (!_run_test(t)) {
			if (!failed) {
				fprintf(stderr, "\
\x1b[1;31m==== TESTS FAILED ====\x1b[0m\n\
Testing \x1b[36m%s\x1b[0m failed on the following cases:\n\
",					_test_desc.desc);
			}
			failed = true;
			fprintf(stderr, "[%02d/%02d] %s\n", thistest, _ntests, t->desc);
		}
	}
	return !!failed;
}

// get any normal main()s out the way in the tested code
#define main _main

static struct _test_desc _test_desc = // user input follows this header

// vi: sw=4 ts=4 noet tw=80 cc=80
