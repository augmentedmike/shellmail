#include <string.h>
#include "ui.h"
#include "message.h"

void draw_list(WINDOW *win, AppState *state) {
    werase(win);

    int rows, cols;
    getmaxyx(win, rows, cols);

    MessageList *list = &state->message_list;
    int selected      = state->ui_state.selected_index;
    int content_rows  = rows - 1; // row 0 is the title bar

    // Keep selected visible
    int scroll = 0;
    if (selected >= content_rows) scroll = selected - content_rows + 1;

    // Title bar
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 1, "INBOX  %zu messages", list->count);
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // Column widths
    int date_w    = 17;
    int from_w    = 26;
    int subject_w = cols - from_w - date_w - 4; // 4 = indicator + gaps

    for (int i = 0; i < content_rows && (scroll + i) < (int)list->count; i++) {
        int idx = scroll + i;
        MessageHeader *h = &list->headers[idx];
        int row = i + 1;

        int is_selected = (idx == selected);
        int is_unread   = !(h->flags & FLAG_SEEN);

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

        // From
        const char *from_src = h->from_name[0] ? h->from_name : h->from_address;
        mvwprintw(win, row, 2, "%-*.*s", from_w, from_w, from_src);

        // Subject
        mvwprintw(win, row, 2 + from_w + 1, "%-*.*s", subject_w, subject_w, h->subject);

        // Date — trim to last 17 chars to show "Apr  3 10:17 +000" style
        int date_len = (int)strlen(h->date);
        const char *date_show = date_len > date_w
            ? h->date + (date_len - date_w)
            : h->date;
        mvwprintw(win, row, cols - date_w - 1, "%-*.*s", date_w, date_w, date_show);

        if (is_selected)    wattroff(win, COLOR_PAIR(1) | A_BOLD);
        else if (is_unread) wattroff(win, A_BOLD);
    }

    wnoutrefresh(win);
}
