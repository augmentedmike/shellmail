#include <stdlib.h>
#include <string.h>
#include "ui/ui.h"
#include "imap/imap.h"
#include "sync/sync.h"
#include "cache/cache.h"
#include "core/message.h"

WINDOW *win_list;
WINDOW *win_reader;
WINDOW *win_composer;
WINDOW *win_status;

static void create_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    win_list     = newwin(rows - 1, cols, 0, 0);
    win_reader   = newwin(rows - 1, cols, 0, 0);
    win_composer = newwin(rows - 3, cols - 4, 1, 2);
    win_status   = newwin(1, cols, rows - 1, 0);

    keypad(win_list,     TRUE);
    keypad(win_reader,   TRUE);
    keypad(win_composer, TRUE);
    keypad(win_status,   TRUE);
}

static void destroy_windows(void) {
    delwin(win_list);
    delwin(win_reader);
    delwin(win_composer);
    delwin(win_status);
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
        }

        WINDOW *active_win = win_list;
        int     timeout_ms = 200;

        if (state->ui_state.active_pane == PANE_READER)   active_win = win_reader;
        if (state->ui_state.active_pane == PANE_COMPOSER) active_win = win_composer;
        if (state->ui_state.active_pane == PANE_COMMAND) {
            active_win = win_status;
            timeout_ms = -1; // block while user types
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
        }
    }

    ui_teardown();
}
