#include "ui/ui.h"
#include "sync/sync.h"

void draw_status(WINDOW *win, AppState *state) {
    werase(win);
    wbkgd(win, COLOR_PAIR(4));

    // Determine sync status label
    const char *sync_label = "";
    if (state->sync) {
        SyncStatus ss = sync_status(state->sync);
        switch (ss) {
            case SYNC_CONNECTING: sync_label = "  (connecting...)"; break;
            case SYNC_FETCHING:   sync_label = "  (syncing...)";    break;
            case SYNC_ERROR:      sync_label = "  (sync error)";    break;
            case SYNC_DONE:
            case SYNC_IDLE:
            default:              sync_label = "  (up to date)";    break;
        }
    }

    switch (state->ui_state.active_pane) {
        case PANE_LIST:
            mvwprintw(win, 0, 1,
                "shellmail  |  j/k: navigate   Enter: open   R: sync   c: compose   q: quit%s",
                sync_label);
            break;
        case PANE_READER:
            mvwprintw(win, 0, 1,
                "shellmail  |  j/k: scroll   r: reply   Esc: back to list%s",
                sync_label);
            break;
        case PANE_COMPOSER:
            mvwprintw(win, 0, 1,
                "shellmail  |  Ctrl+S: send   Esc: cancel");
            break;
    }

    wnoutrefresh(win);
}
