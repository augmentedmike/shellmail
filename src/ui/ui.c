#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "ui/ui.h"
#include "imap/imap.h"
#include "sync/sync.h"
#include "cache/cache.h"
#include "core/message.h"
#include "caldav/caldav.h"

WINDOW *win_list;
WINDOW *win_reader;
WINDOW *win_composer;
WINDOW *win_status;
WINDOW *win_calendar;

static void cal_load_month(AppState *state); // forward decl — defined after composer handler

static void create_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    win_list     = newwin(rows - 1, cols, 0, 0);
    win_reader   = newwin(rows - 1, cols, 0, 0);
    win_composer = newwin(rows - 3, cols - 4, 1, 2);
    win_status   = newwin(1, cols, rows - 1, 0);
    win_calendar = newwin(rows - 1, cols, 0, 0);

    keypad(win_list,     TRUE);
    keypad(win_reader,   TRUE);
    keypad(win_composer, TRUE);
    keypad(win_status,   TRUE);
    keypad(win_calendar, TRUE);
}

static void destroy_windows(void) {
    delwin(win_list);
    delwin(win_reader);
    delwin(win_composer);
    delwin(win_status);
    delwin(win_calendar);
}

static void ui_init(void) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_BLACK, COLOR_CYAN);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_CYAN,  -1);
        init_pair(4, COLOR_BLACK, COLOR_WHITE);
    }

    create_windows();
}

static void ui_teardown(void) {
    destroy_windows();
    endwin();
}

static void handle_resize(void) {
    endwin();
    refresh();
    destroy_windows();
    create_windows();
}

void reload_threads(AppState *state) {
    if (state->current_message && state->current_thread) {
        for (size_t i = 0; i < state->current_thread->count; i++)
            message_free(&state->current_message[i]);
        free(state->current_message);
        state->current_message = NULL;
        state->current_thread  = NULL;
    }
    message_list_free(&state->message_list);
    thread_list_free(&state->thread_list);
    if (state->cache) {
        cache_load_headers(state->cache, &state->message_list);
        thread_list_build(&state->message_list, &state->thread_list);
    }
    appstate_rebuild_view(state);
}

static void open_selected(AppState *state) {
    if (state->view_count == 0) return;

    int idx = state->ui_state.selected_index;
    if (idx < 0 || (size_t)idx >= state->view_count) return;

    if (state->current_message && state->current_thread) {
        for (size_t i = 0; i < state->current_thread->count; i++)
            message_free(&state->current_message[i]);
        free(state->current_message);
        state->current_message = NULL;
    }

    state->current_thread         = state->view[idx];
    state->ui_state.active_pane   = PANE_READER;
    state->ui_state.scroll_offset = 0;
}

static void handle_key_list(int ch, AppState *state) {
    UIState *ui  = &state->ui_state;
    int      max = (int)state->view_count - 1;

    switch (ch) {
        case 'j': case KEY_DOWN:
            if (ui->selected_index < max) ui->selected_index++;
            break;
        case 'k': case KEY_UP:
            if (ui->selected_index > 0) ui->selected_index--;
            break;
        case '\n': case KEY_ENTER:
            open_selected(state);
            break;
        case 'c':
            ui->active_pane = PANE_COMPOSER;
            break;
        case 'R':
            if (state->sync) sync_request(state->sync);
            break;
        case 'H':
            ui->hide_seen = !ui->hide_seen;
            appstate_rebuild_view(state);
            break;
        case 'M':
            imap_mark_all_seen(&state->session.imap_conn);
            if (state->cache) cache_mark_all_seen(state->cache);
            reload_threads(state);
            break;
        case 'A': {
            if (state->view_count == 0) break;
            Thread *t = state->view[ui->selected_index];
            const char *dest = state->config.archive_mailbox;
            imap_create_mailbox(&state->session.imap_conn, dest); // no-op if exists
            for (size_t i = 0; i < t->count; i++) {
                uint32_t uid = t->headers[i].uid;
                imap_uid_move(&state->session.imap_conn, uid, dest);
                if (state->cache) cache_update_folder(state->cache, uid, dest);
            }
            reload_threads(state);
            break;
        }
        case 'C':  // Shift-C — open calendar
            ui->active_pane = PANE_CALENDAR;
            if (!state->cal_loaded ||
                    state->cal_loaded_year  != ui->cal_year ||
                    state->cal_loaded_month != ui->cal_month) {
                cal_load_month(state);
            }
            break;
        case ':': {
            // Pre-fill command bar with filter suggestion
            if (state->view_count > 0 && ui->selected_index >= 0 &&
                    (size_t)ui->selected_index < state->view_count) {
                Thread *t = state->view[ui->selected_index];
                const char *from = t->headers && t->headers[0].from_address[0]
                    ? t->headers[0].from_address
                    : (t->headers ? t->headers[0].from_name : "");
                snprintf(ui->cmd_buf, sizeof(ui->cmd_buf),
                         "filter from \"%s\" -> ", from);
            } else {
                snprintf(ui->cmd_buf, sizeof(ui->cmd_buf), "filter from \"\" -> ");
            }
            ui->cmd_cursor  = (int)strlen(ui->cmd_buf);
            ui->active_pane = PANE_COMMAND;
            curs_set(1);
            break;
        }
    }
}

static void handle_key_reader(int ch, AppState *state) {
    UIState *ui = &state->ui_state;

    switch (ch) {
        case 'j': case KEY_DOWN:
            ui->scroll_offset++;
            break;
        case 'k': case KEY_UP:
            if (ui->scroll_offset > 0) ui->scroll_offset--;
            break;
        case 27: // ESC
            ui->active_pane   = PANE_LIST;
            ui->scroll_offset = 0;
            break;
        case 'r':
            ui->active_pane = PANE_COMPOSER;
            break;
    }
}

static void handle_key_composer(int ch, AppState *state) {
    if (ch == 27)
        state->ui_state.active_pane = PANE_LIST;
}

// ============================================================================
// Calendar helpers
// ============================================================================

static int cal_days_in_month(int year, int month) {
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = month;  // 0-indexed, so this is next month
    t.tm_mday = 0;      // day 0 = last day of current month
    mktime(&t);
    return t.tm_mday;
}

static void cal_load_month(AppState *state) {
    UIState *ui = &state->ui_state;
    if (!state->config.caldav_url[0]) return;

    // Initialise CalDAV connection on first use
    if (!state->caldav.server[0]) {
        const char *user = state->config.caldav_username[0]
            ? state->config.caldav_username : state->config.username;
        const char *pass = state->config.caldav_password[0]
            ? state->config.caldav_password : state->config.password;
        caldav_init(&state->caldav, state->config.caldav_url, user, pass);
    }

    cal_event_list_free(&state->cal_events);
    cal_event_list_init(&state->cal_events);
    state->cal_loaded = 0;

    // Show "Loading..." while the network request runs
    draw_calendar(win_calendar, state);
    draw_status(win_status, state);
    doupdate();

    int fetch_ret = caldav_fetch_month(&state->caldav, ui->cal_year, ui->cal_month,
                                        &state->cal_events);
    if (fetch_ret != 0) {
        if (fetch_ret > 0)
            snprintf(state->cal_error, sizeof(state->cal_error),
                     "CalDAV error: HTTP %d", fetch_ret);
        else
            snprintf(state->cal_error, sizeof(state->cal_error),
                     "CalDAV error: connection failed");
    } else {
        state->cal_error[0] = '\0';
    }
    state->cal_loaded       = 1;
    state->cal_loaded_year  = ui->cal_year;
    state->cal_loaded_month = ui->cal_month;
}

static void handle_key_calendar(int ch, AppState *state) {
    UIState *ui  = &state->ui_state;
    int      dim = cal_days_in_month(ui->cal_year, ui->cal_month);

    switch (ch) {
        case 'h': case KEY_LEFT:
            if (--ui->cal_day < 1) {
                if (--ui->cal_month < 1) { ui->cal_month = 12; ui->cal_year--; }
                ui->cal_day = cal_days_in_month(ui->cal_year, ui->cal_month);
                cal_load_month(state);
            }
            break;
        case 'l': case KEY_RIGHT:
            if (++ui->cal_day > dim) {
                ui->cal_day = 1;
                if (++ui->cal_month > 12) { ui->cal_month = 1; ui->cal_year++; }
                cal_load_month(state);
            }
            break;
        case 'k': case KEY_UP:
            ui->cal_day -= 7;
            if (ui->cal_day < 1) {
                if (--ui->cal_month < 1) { ui->cal_month = 12; ui->cal_year--; }
                ui->cal_day += cal_days_in_month(ui->cal_year, ui->cal_month);
                cal_load_month(state);
            }
            break;
        case 'j': case KEY_DOWN:
            ui->cal_day += 7;
            if (ui->cal_day > dim) {
                ui->cal_day -= dim;
                if (++ui->cal_month > 12) { ui->cal_month = 1; ui->cal_year++; }
                cal_load_month(state);
            }
            break;
        case '[':
            if (--ui->cal_month < 1) { ui->cal_month = 12; ui->cal_year--; }
            dim = cal_days_in_month(ui->cal_year, ui->cal_month);
            if (ui->cal_day > dim) ui->cal_day = dim;
            cal_load_month(state);
            break;
        case ']':
            if (++ui->cal_month > 12) { ui->cal_month = 1; ui->cal_year++; }
            dim = cal_days_in_month(ui->cal_year, ui->cal_month);
            if (ui->cal_day > dim) ui->cal_day = dim;
            cal_load_month(state);
            break;
        case 'R':
            state->cal_loaded = 0; // force refetch
            cal_load_month(state);
            break;
        case 27: // ESC
            ui->active_pane = PANE_LIST;
            break;
    }
}

void ui_run(AppState *state) {
    ui_init();
    appstate_rebuild_view(state);

    int running = 1;
    while (running) {
        // Check if background sync delivered new data
        if (state->sync && sync_needs_reload(state->sync)) {
            sync_clear_reload(state->sync);
            reload_threads(state);
        }

        switch (state->ui_state.active_pane) {
            case PANE_LIST:
                draw_list(win_list, state);
                draw_status(win_status, state);
                doupdate();
                break;
            case PANE_READER:
                draw_reader(win_reader, state);
                draw_status(win_status, state);
                doupdate();
                // Inject OSC 8 hyperlinks for URLs detected in draw_reader.
                // Written as raw terminal sequences after ncurses flushes.
                if (state->ui_state.link_count > 0) {
                    UIState *ui = &state->ui_state;
                    for (int li = 0; li < ui->link_count; li++) {
                        ScreenLink *sl = &ui->links[li];
                        // Position cursor, open OSC 8 link, write visible text, close.
                        fprintf(stdout, "\033[%d;%dH\033]8;;%s\033\\%.*s\033]8;;\033\\",
                                sl->row + 1, sl->col + 1,
                                sl->url, (int)sl->len, sl->url);
                    }
                    fputs("\033[H", stdout);   // cursor to top-left
                    fflush(stdout);
                    // Force full redraw next frame so ncurses cursor tracking stays clean.
                    clearok(win_reader, TRUE);
                }
                break;
            case PANE_COMPOSER:
                draw_composer(win_composer, state);
                draw_status(win_status, state);
                doupdate();
                break;
            case PANE_COMMAND:
                draw_list(win_list, state);
                draw_command(win_status, state);
                doupdate();
                break;
            case PANE_CALENDAR:
                draw_calendar(win_calendar, state);
                draw_status(win_status, state);
                doupdate();
                break;
        }

        WINDOW *active_win = win_list;
        int     timeout_ms = 200;

        switch (state->ui_state.active_pane) {
            case PANE_READER:   active_win = win_reader;   break;
            case PANE_COMPOSER: active_win = win_composer; break;
            case PANE_CALENDAR: active_win = win_calendar; break;
            case PANE_COMMAND:
                active_win = win_status;
                timeout_ms = -1; // block while user types
                break;
            default: break;
        }

        wtimeout(active_win, timeout_ms);
        int ch = wgetch(active_win);

        if (ch == KEY_RESIZE) { handle_resize(); continue; }
        if (ch == ERR) continue;

        if (ch == 'q' && state->ui_state.active_pane == PANE_LIST) {
            running = 0;
            continue;
        }

        switch (state->ui_state.active_pane) {
            case PANE_LIST:     handle_key_list(ch, state);     break;
            case PANE_READER:   handle_key_reader(ch, state);   break;
            case PANE_COMPOSER: handle_key_composer(ch, state); break;
            case PANE_COMMAND:  handle_key_command(ch, state);  break;
            case PANE_CALENDAR: handle_key_calendar(ch, state); break;
        }
    }

    ui_teardown();
}
