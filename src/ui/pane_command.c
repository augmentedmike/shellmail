#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ui/ui.h"
#include "core/app_state.h"
#include "core/message.h"
#include "cache/cache.h"
#include "imap/imap.h"

// ---------------------------------------------------------------------------
// Execute a filter command:
//   filter from "pattern" -> FolderName
//   filter subject "pattern" -> FolderName
// ---------------------------------------------------------------------------

static void execute_command(AppState *state) {
    const char *cmd = state->ui_state.cmd_buf;
    while (*cmd == ' ') cmd++;

    if (strncmp(cmd, "filter ", 7) != 0) return;
    cmd += 7;
    while (*cmd == ' ') cmd++;

    // Field
    char field[32] = {0};
    if (strncmp(cmd, "from ", 5) == 0) {
        strcpy(field, "from");
        cmd += 5;
    } else if (strncmp(cmd, "subject ", 8) == 0) {
        strcpy(field, "subject");
        cmd += 8;
    } else {
        return;
    }
    while (*cmd == ' ') cmd++;

    // Pattern (quoted or unquoted)
    char pattern[256] = {0};
    if (*cmd == '"') {
        cmd++;
        const char *end = strchr(cmd, '"');
        if (!end) return;
        size_t len = (size_t)(end - cmd);
        if (len >= sizeof(pattern)) len = sizeof(pattern) - 1;
        memcpy(pattern, cmd, len);
        cmd = end + 1;
    } else {
        const char *end = strstr(cmd, " ->");
        if (!end) end = strstr(cmd, "->");
        if (!end) {
            strncpy(pattern, cmd, sizeof(pattern) - 1);
            cmd += strlen(cmd);
        } else {
            size_t len = (size_t)(end - cmd);
            while (len > 0 && cmd[len - 1] == ' ') len--;
            if (len >= sizeof(pattern)) len = sizeof(pattern) - 1;
            memcpy(pattern, cmd, len);
            cmd = end;
        }
    }
    while (*cmd == ' ') cmd++;

    // Arrow
    if (strncmp(cmd, "->", 2) != 0) return;
    cmd += 2;
    while (*cmd == ' ') cmd++;

    // Folder name
    char folder[256] = {0};
    strncpy(folder, cmd, sizeof(folder) - 1);
    // Trim trailing whitespace/newline
    char *tail = folder + strlen(folder);
    while (tail > folder && isspace((unsigned char)*(tail - 1))) *--tail = '\0';

    if (!pattern[0] || !folder[0]) return;

    // Persist the filter rule
    if (state->cache)
        cache_save_filter(state->cache, field, pattern, folder);

    // Create the IMAP folder (ignore error if it already exists)
    imap_create_mailbox(&state->session.imap_conn, folder);

    // Get matching UIDs and move them
    uint32_t *uids = NULL;
    size_t uid_count = 0;
    if (state->cache)
        cache_get_matching_uids(state->cache, field, pattern, &uids, &uid_count);

    for (size_t i = 0; i < uid_count; i++) {
        imap_uid_move(&state->session.imap_conn, uids[i], folder);
        if (state->cache)
            cache_update_folder(state->cache, uids[i], folder);
    }
    free(uids);

    // Reload thread list from cache
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

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void draw_command(WINDOW *win, AppState *state) {
    werase(win);
    wattron(win, A_BOLD);
    mvwaddch(win, 0, 0, ':');
    wattroff(win, A_BOLD);
    mvwaddstr(win, 0, 1, state->ui_state.cmd_buf);
    wmove(win, 0, 1 + state->ui_state.cmd_cursor);
    wnoutrefresh(win);
}

// ---------------------------------------------------------------------------
// Key handler
// ---------------------------------------------------------------------------

int handle_key_command(int ch, AppState *state) {
    UIState *ui = &state->ui_state;

    if (ch == 27) { // ESC — cancel
        ui->cmd_buf[0] = '\0';
        ui->cmd_cursor = 0;
        ui->active_pane = PANE_LIST;
        curs_set(0);
        return 0;
    }

    if (ch == '\n' || ch == KEY_ENTER) {
        execute_command(state);
        ui->cmd_buf[0] = '\0';
        ui->cmd_cursor = 0;
        ui->active_pane = PANE_LIST;
        curs_set(0);
        return 1;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
        if (ui->cmd_cursor > 0) {
            int len = (int)strlen(ui->cmd_buf);
            memmove(ui->cmd_buf + ui->cmd_cursor - 1,
                    ui->cmd_buf + ui->cmd_cursor,
                    (size_t)(len - ui->cmd_cursor + 1));
            ui->cmd_cursor--;
        }
        return 0;
    }

    if (ch == KEY_LEFT) {
        if (ui->cmd_cursor > 0) ui->cmd_cursor--;
        return 0;
    }

    if (ch == KEY_RIGHT) {
        int len = (int)strlen(ui->cmd_buf);
        if (ui->cmd_cursor < len) ui->cmd_cursor++;
        return 0;
    }

    // Printable character: insert at cursor
    if (ch >= 32 && ch < 127) {
        int len = (int)strlen(ui->cmd_buf);
        if (len < (int)sizeof(ui->cmd_buf) - 2) {
            memmove(ui->cmd_buf + ui->cmd_cursor + 1,
                    ui->cmd_buf + ui->cmd_cursor,
                    (size_t)(len - ui->cmd_cursor + 1));
            ui->cmd_buf[ui->cmd_cursor++] = (char)ch;
        }
    }

    return 0;
}
