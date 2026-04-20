#include "ui.h"

void draw_status(WINDOW *win, AppState *state) {
    werase(win);
    wbkgd(win, COLOR_PAIR(4));

    switch (state->ui_state.active_pane) {
        case PANE_LIST:
            mvwprintw(win, 0, 1,
                "shellmail  |  j/k: navigate   Enter: open   c: compose   q: quit");
            break;
        case PANE_READER:
            mvwprintw(win, 0, 1,
                "shellmail  |  j/k: scroll   r: reply   Esc: back to list");
            break;
        case PANE_COMPOSER:
            mvwprintw(win, 0, 1,
                "shellmail  |  Ctrl+S: send   Esc: cancel");
            break;
    }

    wnoutrefresh(win);
}
