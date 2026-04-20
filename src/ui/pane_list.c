#include <string.h>
#include "ui/ui.h"
#include "core/message.h"

void draw_list(WINDOW *win, AppState *state) {
    werase(win);

    int rows, cols;
    getmaxyx(win, rows, cols);

    ThreadList *tl   = &state->thread_list;
    int selected     = state->ui_state.selected_index;
    int content_rows = rows - 1;

    // Keep selected visible
    int scroll = 0;
    if (selected >= content_rows) scroll = selected - content_rows + 1;

    // Title bar
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 1, "INBOX  %zu threads", tl->count);
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // Column widths
    int count_w   = 4;   // " (3)"
    int date_w    = 17;
    int from_w    = 26;
    int subject_w = cols - from_w - date_w - count_w - 4;

    for (int i = 0; i < content_rows && (scroll + i) < (int)tl->count; i++) {
        int idx = scroll + i;
        Thread *t = &tl->threads[idx];
        int row   = i + 1;

        int is_selected = (idx == selected);
        int is_unread   = !(t->flags & FLAG_SEEN);

        if (is_selected)    wattron(win, COLOR_PAIR(1) | A_BOLD);
        else if (is_unread) wattron(win, A_BOLD);

        mvwhline(win, row, 0, ' ', cols);

        // Unread dot
        if (!is_selected && is_unread) {
            wattron(win, COLOR_PAIR(2));
            mvwaddch(win, row, 1, '*');
            wattroff(win, COLOR_PAIR(2));
        } else {
            mvwaddch(win, row, 1, ' ');
        }

        // Participants
        mvwprintw(win, row, 2, "%-*.*s", from_w, from_w, t->participants);

        // Message count badge (if > 1)
        char badge[8] = "";
        if (t->count > 1) snprintf(badge, sizeof(badge), "(%zu)", t->count);
        mvwprintw(win, row, 2 + from_w + 1, "%-*s", count_w, badge);

        // Subject
        mvwprintw(win, row, 2 + from_w + 1 + count_w + 1,
                  "%-*.*s", subject_w, subject_w, t->subject);

        // Date
        int date_len = (int)strlen(t->latest_date);
        const char *date_show = date_len > date_w
            ? t->latest_date + (date_len - date_w)
            : t->latest_date;
        mvwprintw(win, row, cols - date_w - 1, "%-*.*s", date_w, date_w, date_show);

        if (is_selected)    wattroff(win, COLOR_PAIR(1) | A_BOLD);
        else if (is_unread) wattroff(win, A_BOLD);
    }

    wnoutrefresh(win);
}
