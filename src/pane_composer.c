#include "ui.h"

void draw_composer(WINDOW *win, AppState *state) {
    (void)state;
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " COMPOSE ");
    wnoutrefresh(win);
}
