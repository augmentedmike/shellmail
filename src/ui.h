#ifndef ui_h
#define ui_h

#include <ncurses.h>
#include "app_state.h"

// Windows — accessible to pane renderers
extern WINDOW *win_list;
extern WINDOW *win_reader;
extern WINDOW *win_composer;
extern WINDOW *win_status;

// Entry point — runs the event loop until quit
void ui_run(AppState *state);

// Pane draw functions — implemented in pane_*.c
void draw_list(WINDOW *win, AppState *state);
void draw_reader(WINDOW *win, AppState *state);
void draw_composer(WINDOW *win, AppState *state);
void draw_status(WINDOW *win, AppState *state);  // pane_status.c

#endif /* ui_h */
