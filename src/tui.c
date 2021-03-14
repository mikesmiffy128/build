#include <stdlib.h>
#include <errmsg.h>
#include <unistd.h>

#include "build.h"
#include "errmsg.h"
#include "evloop.h"
#include "fmt.h"
#include "iobuf.h"
#include "proc.h"
#include "time.h"

static int devtty = -1;
static struct obuf *buf_tty = OBUF(-1, 128);
static bool shouldshow = false;

static const char *const spinner[] = {"╸╺ ", "━  ", "╺╸ ", " ━ "};
static int spinstate = 0;

int tui_ndone = 0;
char *tui_lastdone = 0;

static void redraw(void) {
	// TODO(tui): do... whatever I come up with over here
	obuf_put0t(buf_tty, "\r\033[K");
	obuf_put0t(buf_tty, spinner[spinstate]);
	fmt_buf_u32(buf_tty, nactive);
	obuf_putc(buf_tty, '/');
	fmt_buf_u32(buf_tty, maxpar);
	obuf_put0t(buf_tty, " active, ");
	if (qlen + nblocked) {
		fmt_buf_u32(buf_tty, qlen + nblocked);
		obuf_put0t(buf_tty, " waiting, ");
	}
	fmt_buf_u32(buf_tty, tui_ndone);
	obuf_put0t(buf_tty, " done");
	if (tui_lastdone) {
		obuf_put0t(buf_tty, ", last: `");
		obuf_put0t(buf_tty, tui_lastdone);
		obuf_putc(buf_tty, '`');
	}
	obuf_flush(buf_tty);
	obuf_reset(buf_tty);
	// HACK!! if our tty is different from stderr then stderr was sent to a
	// file, but if it _is_ stderr then we want the next log output to clear the
	// TUI line first. the solution that touches the least other random code is
	// to just prefill buf_err with the vt100 sequence. we also have to redraw
	// after any logging happens; right now that just happens on a timer.
	// oh and of course this assumes that this write won't immediately flush; in
	// practice it really won't because errmsg_* flushes after every line and
	// the buffers are _really big_
	if (devtty == 2) obuf_put0t(buf_err, "\r\033[K");
}

// FURTHER HACK!! since the vforked child shares buf_err, pre-exec() errors
// will get muddled up with our tui stuff if we don't shuffle stuff around
void tui_prevfork(void) { if (devtty) obuf_reset(buf_err); }
void tui_postvfork(void) { if (devtty == 2) obuf_put0t(buf_err, "\r\033[K"); }

#define INTERVAL 50

static struct evloop_timer timer;
static void spincb(struct evloop_timer *unused) {
	spinstate = (spinstate + 1) % (sizeof(spinner) / sizeof(*spinner));
	redraw(); // TODO(tui): can technically partially redraw? worth effort?
	timer.deadline += INTERVAL;
	// note: right now this redraw is also the only redraw (which conveniently
	// rate-limits writes to the terminal a bit to avoid excessive syscall spam)
	evloop_sched(&timer);
}
static void showcb(struct evloop_timer *unused) {
	shouldshow = true;
	redraw();
	timer.cb = &spincb; // play a little animation, for fun!
	timer.deadline += INTERVAL;
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

// vi: sw=4 ts=4 noet tw=80 cc=80
