#pragma once

#include <ncurses.h>
#include "core/app_state.h"

// Windows — accessible to pane renderers
extern WINDOW *win_list;
extern WINDOW *win_reader;
extern WINDOW *win_composer;
extern WINDOW *win_status;
extern WINDOW *win_calendar;

// Entry point — runs the event loop until quit
void ui_run(AppState *state);

// Pane draw functions — implemented in pane_*.c
void draw_list(WINDOW *win, AppState *state);
void draw_reader(WINDOW *win, AppState *state);
void draw_composer(WINDOW *win, AppState *state);
void draw_status(WINDOW *win, AppState *state);
void draw_command(WINDOW *win, AppState *state);
void draw_calendar(WINDOW *win, AppState *state);

// Reload thread list from cache (shared by ui.c and pane_command.c)
void reload_threads(AppState *state);

// Command-mode handler; returns 1 if command was executed
int  handle_key_command(int ch, AppState *state);

