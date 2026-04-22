#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ui/ui.h"
#include "core/app_state.h"

static const char *MONTH_NAMES[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *DAY_NAMES[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// Returns the number of days in [year, month] (month 1-12).
static int dim(int year, int month) {
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = month;  // 0-indexed: month means next month
    t.tm_mday = 0;      // day 0 = last day of current month
    mktime(&t);
    return t.tm_mday;
}

// Returns the weekday of the 1st (0=Sun).
static int first_wday(int year, int month) {
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = 1;
    mktime(&t);
    return t.tm_wday;
}

void draw_calendar(WINDOW *win, AppState *state) {
    werase(win);
    int rows, cols;
    getmaxyx(win, rows, cols);

    UIState *ui    = &state->ui_state;
    int year       = ui->cal_year;
    int month      = ui->cal_month;
    int sel        = ui->cal_day;
    int days       = dim(year, month);

    if (sel < 1)     sel = 1;
    if (sel > days)  sel = days;

    // ---- Title bar -------------------------------------------------------
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 1,
        "CALENDAR  %s %d   [h/l: day  j/k: week  [/]: month  R: refresh  Esc: back]",
        MONTH_NAMES[month - 1], year);
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // ---- Day-of-week header ----------------------------------------------
    const int cell_w = 5;
    const int grid_x = 2;

    wattron(win, A_BOLD);
    for (int d = 0; d < 7; d++)
        mvwprintw(win, 2, grid_x + d * cell_w, " %-3s", DAY_NAMES[d]);
    wattroff(win, A_BOLD);

    // ---- Build event-presence map (days 1-31) ----------------------------
    char has_ev[32] = {0};
    for (size_t i = 0; i < state->cal_events.count; i++) {
        const CalEvent *ev = &state->cal_events.events[i];
        int ey, em, ed;
        if (sscanf(ev->date, "%4d-%2d-%2d", &ey, &em, &ed) == 3)
            if (ey == year && em == month && ed >= 1 && ed <= 31)
                has_ev[ed] = 1;
    }

    // ---- Calendar grid ---------------------------------------------------
    int col = first_wday(year, month);
    int row = 3;

    for (int day = 1; day <= days; day++) {
        int x      = grid_x + col * cell_w;
        int is_sel = (day == sel);
        int has    = has_ev[day];

        if      (is_sel) wattron(win, COLOR_PAIR(1) | A_BOLD);
        else if (has)    wattron(win, COLOR_PAIR(2)  | A_BOLD);

        if (is_sel)
            mvwprintw(win, row, x, "[%2d]", day);
        else if (has)
            mvwprintw(win, row, x, " %2d*", day);
        else
            mvwprintw(win, row, x, "  %2d", day);

        if      (is_sel) wattroff(win, COLOR_PAIR(1) | A_BOLD);
        else if (has)    wattroff(win, COLOR_PAIR(2)  | A_BOLD);

        if (++col >= 7) { col = 0; row++; }
    }

    // ---- Divider ---------------------------------------------------------
    int div_row = row + 1;
    if (div_row < rows - 2)
        mvwhline(win, div_row, 0, ACS_HLINE, cols);

    int ev_row = div_row + 1;
    if (ev_row >= rows) { wnoutrefresh(win); return; }

    // ---- Events for selected day -----------------------------------------
    char sel_date[11];
    snprintf(sel_date, sizeof(sel_date), "%04d-%02d-%02d", year, month, sel);

    if (!state->config.caldav_url[0]) {
        mvwprintw(win, ev_row, 2,
            "CalDAV not configured — add caldav_url to config.yaml");
        wnoutrefresh(win);
        return;
    }

    if (!state->cal_loaded) {
        mvwprintw(win, ev_row, 2, "Loading events...");
        wnoutrefresh(win);
        return;
    }

    if (state->cal_error[0]) {
        wattron(win, COLOR_PAIR(2) | A_BOLD);
        mvwprintw(win, ev_row, 2, "%s", state->cal_error);
        wattroff(win, COLOR_PAIR(2) | A_BOLD);
        wnoutrefresh(win);
        return;
    }

    // Header for the day's events
    wattron(win, A_BOLD);
    mvwprintw(win, ev_row++, 2, "Events - %s:", sel_date);
    wattroff(win, A_BOLD);

    int count = 0;
    for (size_t i = 0; i < state->cal_events.count && ev_row < rows - 1; i++) {
        const CalEvent *ev = &state->cal_events.events[i];
        if (strcmp(ev->date, sel_date) != 0) continue;
        count++;

        int indent  = 15;             // column for text after the time prefix
        int max_txt = cols - indent - 2;
        if (max_txt < 8) max_txt = 8;

        // ---- Summary line with time prefix ----
        wattron(win, A_BOLD);
        if (ev->all_day || ev->hour < 0) {
            mvwprintw(win, ev_row, 4, "all day");
        } else {
            mvwprintw(win, ev_row, 4, "%02d:%02d", ev->hour, ev->minute);
        }
        wattroff(win, A_BOLD);
        mvwprintw(win, ev_row, indent, "%.*s", max_txt, ev->summary);
        ev_row++;

        // ---- Location (own line) ----
        if (ev->location[0] && ev_row < rows - 1) {
            mvwprintw(win, ev_row, indent, "@ %.*s", max_txt - 2, ev->location);
            ev_row++;
        }

        // ---- Description — split on iCal \n escapes and word-wrap ----
        if (ev->description[0] && ev_row < rows - 1) {
            const char *dp = ev->description;
            int line_limit = 6;   // max description lines per event
            while (*dp && ev_row < rows - 1 && line_limit-- > 0) {
                // Find the next \n escape or end of string
                const char *nl = dp;
                while (*nl && !(nl[0] == '\\' && nl[1] == 'n')) nl++;

                // Word-wrap this segment into max_txt-wide lines
                int seg_len = (int)(nl - dp);
                int off = 0;
                while (off < seg_len && ev_row < rows - 1 && line_limit >= 0) {
                    int chunk = seg_len - off;
                    if (chunk > max_txt) chunk = max_txt;
                    // Break at last space if possible
                    if (chunk == max_txt) {
                        int sp = chunk - 1;
                        while (sp > max_txt / 2 && dp[off + sp] != ' ') sp--;
                        if (dp[off + sp] == ' ') chunk = sp;
                    }
                    wattron(win, COLOR_PAIR(3));
                    mvwaddnstr(win, ev_row, indent, dp + off, chunk);
                    wattroff(win, COLOR_PAIR(3));
                    ev_row++;
                    off += chunk;
                    while (off < seg_len && dp[off] == ' ') off++; // skip leading space on next wrap
                    if (chunk < max_txt) break; // short line = end of segment
                }

                if (*nl == '\0') break;
                dp = nl + 2; // skip the \n escape
            }
        }

        // ---- Blank line between events ----
        if (ev_row < rows - 1) ev_row++;
    }

    if (count == 0 && ev_row < rows)
        mvwprintw(win, ev_row, 4, "(no events)");

    wnoutrefresh(win);
}
