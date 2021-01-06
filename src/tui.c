#include <stdlib.h>
#include <errmsg.h>
#include <unistd.h>

#include "errmsg.h"
#include "evloop.h"
#include "fmt.h"
#include "iobuf.h"
#include "time.h"

static int devtty = -1;
static struct obuf *buf_tty = OBUF(-1, 512);
static bool shouldshow = false;

static int nactive = 0;
static int npending = 0;
static int ndone = 0;

static const char *const spinner[] = {"╸╺", "━ ", "╺╸", " ━"};
int spinstate = 0;

static void tui_redraw(void) {
	// TODO(tui): do... whatever I come up with over here
	obuf_put0t(buf_tty, "\r\033[K ");
	obuf_put0t(buf_tty, spinner[spinstate]);
	obuf_put0t(buf_tty, " tasks: ");
	fmt_buf_u32(buf_tty, nactive);
	obuf_put0t(buf_tty, " active, ");
	fmt_buf_u32(buf_tty, npending);
	obuf_put0t(buf_tty, " pending, ");
	fmt_buf_u32(buf_tty, ndone);
	obuf_put0t(buf_tty, " done");
	obuf_flush(buf_tty);
	obuf_reset(buf_tty);
	// HACK!! if our tty is different from stderr then stderr was sent to a
	// file, but if it _is_ stderr then we want the next log output to clear the
	// TUI line first. the solution that touches the least other random code is
	// to just prefill buf_err with the vt100 sequence. we also have to make
	// sure that anything that logs does a redraw afterwards...
	// oh and of course this assumes that this write won't immediately flush; in
	// practice it really won't because errmsg_* flushes after every line and
	// the buffers are _really big_
	if (devtty == 2) obuf_put0t(buf_err, "\r\033[K");
}

static struct evloop_timer timer;

static void spincb(struct evloop_timer *unused) {
	spinstate = (spinstate + 1) % (sizeof(spinner) / sizeof(*spinner));
	tui_redraw(); // TODO(tui): can technically partially redraw? worth effort?
	timer.deadline += 100;
	evloop_sched(&timer);
}

static void showcb(struct evloop_timer *unused) {
	shouldshow = true;
	tui_redraw();
	timer.cb = &spincb; // play a little animation, for fun!
	timer.deadline += 100;
	evloop_sched(&timer);
}
static struct evloop_timer timer = {.cb = showcb};

static void end(void) { write(devtty, "\r\033[K", 4); }

void tui_init(int devtty_) {
	devtty = devtty_;
	buf_tty->fd = devtty;
	// for quick incremental rebuilds, there's not much point displaying
	// anything, but after, say, 300ms, the user might start to wonder what's
	// going on
	timer.deadline = time_now() + 300;
	evloop_sched(&timer);
	atexit(&end);
}

#define VARFUNCS(name) \
void tui_set##name(int n) { \
	if (name == n) return; \
	name = n; \
	if (shouldshow) tui_redraw(); \
} \
int tui_get##name(void) { \
	return name; \
}
VARFUNCS(nactive)
VARFUNCS(npending)
VARFUNCS(ndone)
#undef VARFUNCS

// vi: sw=4 ts=4 noet tw=80 cc=80
