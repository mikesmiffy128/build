#ifndef INC_TUI_H
#define INC_TUI_H

void tui_init(int devtty);
void tui_redraw(void);

extern int tui_ndone, tui_nfailed;

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80