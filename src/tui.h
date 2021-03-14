#ifndef INC_TUI_H
#define INC_TUI_H

void tui_init(int devtty);
void tui_redraw(void);

extern int tui_ndone;
extern char *tui_lastdone;

/* more yucky spaghetti, for proc.c (see HACK comments in tui.c) */
void tui_prevfork(void);
void tui_postvfork(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
