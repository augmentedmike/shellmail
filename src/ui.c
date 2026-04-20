#include <stdlib.h>
#include "ui.h"
#include "imap.h"

WINDOW *win_list;
WINDOW *win_reader;
WINDOW *win_composer;
WINDOW *win_status;

static void create_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // List and reader are both full-screen (minus status bar)
    // Only one is shown at a time
    win_list     = newwin(rows - 1, cols, 0, 0);
    win_reader   = newwin(rows - 1, cols, 0, 0);
    win_composer = newwin(rows - 3, cols - 4, 1, 2);
    win_status   = newwin(1, cols, rows - 1, 0);

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
        init_pair(1, COLOR_BLACK, COLOR_CYAN);   // selected row
        init_pair(2, COLOR_GREEN, -1);            // unread indicator
        init_pair(3, COLOR_CYAN,  -1);            // header labels
        init_pair(4, COLOR_BLACK, COLOR_WHITE);   // status bar
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

static void open_selected(AppState *state) {
    ThreadList *tl = &state->thread_list;
    if (tl->count == 0) return;

    int idx = state->ui_state.selected_index;
    if (idx < 0 || (size_t)idx >= tl->count) return;

    // Free previous fetched bodies
    if (state->current_message && state->current_thread) {
        for (size_t i = 0; i < state->current_thread->count; i++)
            message_free(&state->current_message[i]);
        free(state->current_message);
        state->current_message = NULL;
    }

    state->current_thread         = &tl->threads[idx];
    state->ui_state.active_pane   = PANE_READER;
    state->ui_state.scroll_offset = 0;
}

static void handle_key_list(int ch, AppState *state) {
    UIState *ui = &state->ui_state;
    int max = (int)state->thread_list.count - 1;

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
    if (ch == 27) // ESC
        state->ui_state.active_pane = PANE_LIST;
}

void ui_run(AppState *state) {
    ui_init();

    int running = 1;
    while (running) {
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
        }

        WINDOW *active_win = win_list;
        if (state->ui_state.active_pane == PANE_READER)   active_win = win_reader;
        if (state->ui_state.active_pane == PANE_COMPOSER) active_win = win_composer;

        int ch = wgetch(active_win);

        if (ch == KEY_RESIZE) { handle_resize(); continue; }

        if (ch == 'q' && state->ui_state.active_pane == PANE_LIST) {
            running = 0;
            continue;
        }

        switch (state->ui_state.active_pane) {
            case PANE_LIST:     handle_key_list(ch, state);     break;
            case PANE_READER:   handle_key_reader(ch, state);   break;
            case PANE_COMPOSER: handle_key_composer(ch, state); break;
        }
    }

    ui_teardown();
}
