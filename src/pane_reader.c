#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "ui.h"

// In a MIME multipart message, find the text/plain part body.
// Falls back to stripping HTML tags if only text/html is found.
// Returns a malloc'd string the caller must free.
static char *extract_text(const char *raw) {
    // Look for text/plain part
    const char *plain = strcasestr(raw, "content-type: text/plain");
    if (!plain) plain = strcasestr(raw, "content-type:text/plain");

    if (plain) {
        // Skip past headers to the blank line
        const char *body = strstr(plain, "\r\n\r\n");
        if (!body) body = strstr(plain, "\n\n");
        if (body) {
            body += (body[1] == '\n') ? 2 : 4;
            // Find end: next MIME boundary "--" or end of string
            const char *end = strstr(body, "\r\n--");
            if (!end) end = strstr(body, "\n--");
            size_t len = end ? (size_t)(end - body) : strlen(body);
            char *out = malloc(len + 1);
            if (out) { memcpy(out, body, len); out[len] = '\0'; }
            return out;
        }
    }

    // No plain text — strip HTML tags from the whole body
    const char *body = strstr(raw, "\r\n\r\n");
    if (!body) body = strstr(raw, "\n\n");
    body = body ? body + (body[1] == '\n' ? 2 : 4) : raw;

    size_t len = strlen(body);
    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t j = 0;
    int in_tag = 0;
    for (size_t i = 0; i < len; i++) {
        if (body[i] == '<')      { in_tag = 1; continue; }
        if (body[i] == '>')      { in_tag = 0; continue; }
        if (in_tag)              continue;
        // Collapse &nbsp; and similar
        if (body[i] == '&') {
            const char *semi = strchr(body + i, ';');
            if (semi && (semi - (body + i)) < 8) {
                out[j++] = ' ';
                i = (size_t)(semi - body);
                continue;
            }
        }
        out[j++] = body[i];
    }
    out[j] = '\0';
    return out;
}

static int index_lines(const char *text, const char **lines, int max_lines) {
    int n = 0;
    const char *p = text;
    while (*p && n < max_lines) {
        lines[n++] = p;
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return n;
}

void draw_reader(WINDOW *win, AppState *state) {
    werase(win);

    int rows, cols;
    getmaxyx(win, rows, cols);

    Message *msg = state->current_message;

    if (!msg || !msg->body.data) {
        mvwprintw(win, rows / 2, (cols - 14) / 2, "No message open");
        wnoutrefresh(win);
        return;
    }

    MessageHeader *h = &msg->header;

    // Title bar: subject
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 1, "%-*.*s", cols - 2, cols - 2, h->subject);
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // From
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 1, "From: ");
    wattroff(win, A_BOLD);
    if (h->from_name[0])
        wprintw(win, "%s <%s>", h->from_name, h->from_address);
    else
        wprintw(win, "%s", h->from_address);

    // Date
    wattron(win, A_BOLD);
    mvwprintw(win, 2, 1, "Date: ");
    wattroff(win, A_BOLD);
    wprintw(win, "%.*s", cols - 8, h->date);

    // Divider
    wattron(win, COLOR_PAIR(3));
    mvwhline(win, 3, 0, ACS_HLINE, cols);
    wattroff(win, COLOR_PAIR(3));

    // Extract plain text body
    char *text = extract_text(msg->body.data);
    if (!text) { wnoutrefresh(win); return; }

    int body_start_row = 4;
    int body_rows      = rows - body_start_row;

    const char **lines = malloc(8192 * sizeof(char *));
    if (!lines) { free(text); wnoutrefresh(win); return; }
    int total_lines = index_lines(text, lines, 8192);

    int scroll = state->ui_state.scroll_offset;
    if (total_lines > body_rows && scroll > total_lines - body_rows)
        scroll = total_lines - body_rows;
    if (scroll < 0) scroll = 0;
    state->ui_state.scroll_offset = scroll;

    for (int i = 0; i < body_rows && (scroll + i) < total_lines; i++) {
        const char *line = lines[scroll + i];
        const char *eol  = strchr(line, '\n');
        int len = eol ? (int)(eol - line) : (int)strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len > cols - 1) len = cols - 1;
        mvwprintw(win, body_start_row + i, 0, "%.*s", len, line);
    }

    free(lines);
    free(text);
    wnoutrefresh(win);
}
