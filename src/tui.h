#ifndef INC_TUI_H
#define INC_TUI_H

void tui_init(int devtty);

void tui_setnactive(int n);
int tui_getnactive(void);
void tui_setnpending(int n);
int tui_getnpending(void);
void tui_setndone(int n);
int tui_getndone(void);

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
