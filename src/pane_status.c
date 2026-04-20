#include "ui.h"

void draw_status(WINDOW *win, AppState *state) {
    werase(win);
    wbkgd(win, COLOR_PAIR(4));

    const char *pane = "LIST";
    if (state->ui_state.active_pane == PANE_READER)   pane = "READER";
    if (state->ui_state.active_pane == PANE_COMPOSER) pane = "COMPOSE";

    mvwprintw(win, 0, 1, "shellmail | %s | j/k:navigate  Enter:open  c:compose  q:quit",
              pane);
    wnoutrefresh(win);
}
