#include <stdlib.h>
#include "ui.h"
#include "imap.h"

WINDOW *win_list;
WINDOW *win_reader;
WINDOW *win_composer;
WINDOW *win_status;

// Color pairs
#define COLOR_SELECTED   1   // highlighted row
#define COLOR_UNREAD     2   // unread indicator
#define COLOR_HEADER     3   // message header labels
#define COLOR_STATUS     4   // status bar

static void create_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int list_h   = rows / 2;
    int reader_h = rows - list_h - 1;  // -1 for status bar

    win_list     = newwin(list_h,   cols, 0,        0);
    win_reader   = newwin(reader_h, cols, list_h,   0);
    win_composer = newwin(rows - 2, cols - 4, 1, 2); // centered overlay
    win_status   = newwin(1,        cols, rows - 1, 0);

    keypad(win_list,     TRUE);
    keypad(win_reader,   TRUE);
    keypad(win_composer, TRUE);
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
        init_pair(COLOR_SELECTED, COLOR_BLACK,  COLOR_CYAN);
        init_pair(COLOR_UNREAD,   COLOR_GREEN,  -1);
        init_pair(COLOR_HEADER,   COLOR_CYAN,   -1);
        init_pair(COLOR_STATUS,   COLOR_BLACK,  COLOR_WHITE);
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

// Open the selected message: fetch body, switch to reader
static void open_selected(AppState *state) {
    MessageList *list = &state->message_list;
    if (list->count == 0) return;

    int idx = state->ui_state.selected_index;
    if (idx < 0 || (size_t)idx >= list->count) return;

    uint32_t uid = list->headers[idx].uid;

    // Free previous message if any
    if (state->current_message) {
        message_free(state->current_message);
        free(state->current_message);
    }
    state->current_message = calloc(1, sizeof(Message));

    char *body = NULL;
    size_t body_len = 0;
    if (imap_fetch_body(&state->session.imap_conn, uid, &body, &body_len) == 0) {
        state->current_message->body.data     = body;
        state->current_message->body.data_len = body_len;
        state->current_message->header        = list->headers[idx];
    }

    state->ui_state.active_pane   = PANE_READER;
    state->ui_state.scroll_offset = 0;
}

static void handle_key_list(int ch, AppState *state) {
    UIState *ui = &state->ui_state;
    int max = (int)state->message_list.count - 1;

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
        case 'q': case KEY_BACKSPACE:
            ui->active_pane   = PANE_LIST;
            ui->scroll_offset = 0;
            break;
        case 'r':
            ui->active_pane = PANE_COMPOSER;
            break;
    }
}

static void handle_key_composer(int ch, AppState *state) {
    switch (ch) {
        case 27: // Escape
            state->ui_state.active_pane = PANE_LIST;
            break;
        // Further composer key handling in pane_composer.c
    }
}

void ui_run(AppState *state) {
    ui_init();

    int running = 1;
    while (running) {
        // Draw all panes
        draw_list(win_list, state);
        draw_status(win_status, state);

        if (state->ui_state.active_pane == PANE_READER ||
            state->ui_state.active_pane == PANE_LIST) {
            draw_reader(win_reader, state);
        }

        if (state->ui_state.active_pane == PANE_COMPOSER) {
            draw_composer(win_composer, state);
        }

        doupdate();

        // Read key from the active window
        WINDOW *active_win = win_list;
        if (state->ui_state.active_pane == PANE_READER)   active_win = win_reader;
        if (state->ui_state.active_pane == PANE_COMPOSER) active_win = win_composer;

        int ch = wgetch(active_win);

        switch (ch) {
            case 'q':
                if (state->ui_state.active_pane == PANE_LIST) {
                    running = 0;
                } else {
                    handle_key_reader(ch, state);
                }
                break;
            case KEY_RESIZE:
                handle_resize();
                break;
            default:
                switch (state->ui_state.active_pane) {
                    case PANE_LIST:     handle_key_list(ch, state);     break;
                    case PANE_READER:   handle_key_reader(ch, state);   break;
                    case PANE_COMPOSER: handle_key_composer(ch, state); break;
                }
        }
    }

    ui_teardown();
}
