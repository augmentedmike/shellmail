#include <string.h>
#include <stdlib.h>
#include "ui/ui.h"
#include "imap/imap.h"
#include "cache/cache.h"
#include "compat/compat.h"

// Find text/plain body in a raw RFC 2822 or MIME message.
// Returns a malloc'd string; caller must free.
static char *extract_text(const char *raw) {
    const char *plain = compat_strcasestr(raw, "content-type: text/plain");
    if (!plain) plain = compat_strcasestr(raw, "content-type:text/plain");

    if (plain) {
        const char *body = strstr(plain, "\r\n\r\n");
        if (!body) body = strstr(plain, "\n\n");
        if (body) {
            body += (body[1] == '\n') ? 2 : 4;
            const char *end = strstr(body, "\r\n--");
            if (!end) end = strstr(body, "\n--");
            size_t len = end ? (size_t)(end - body) : strlen(body);
            char *out = malloc(len + 1);
            if (out) { memcpy(out, body, len); out[len] = '\0'; }
            return out;
        }
    }

    // Fall back: strip HTML tags from body
    const char *body = strstr(raw, "\r\n\r\n");
    if (!body) body = strstr(raw, "\n\n");
    body = body ? body + (body[1] == '\n' ? 2 : 4) : raw;

    size_t len = strlen(body);
    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t j = 0;
    int in_tag = 0;
    for (size_t i = 0; i < len; i++) {
        if (body[i] == '<')  { in_tag = 1; continue; }
        if (body[i] == '>')  { in_tag = 0; continue; }
        if (in_tag)          continue;
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

// ---------------------------------------------------------------------------
// Line indexing across multiple message bodies
// ---------------------------------------------------------------------------

typedef struct Line {
    const char *ptr;
    int         len;
    int         is_header; // 1 = message divider line
} Line;

static int collect_lines(Line *lines, int max,
                          const char *text, int is_header) {
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;
        lines[n++] = (Line){ p, len, is_header };
        if (!eol) break;
        p = eol + 1;
    }
    return n;
}

void draw_reader(WINDOW *win, AppState *state) {
    werase(win);

    int rows, cols;
    getmaxyx(win, rows, cols);

    Thread *thread = state->current_thread;

    if (!thread) {
        mvwprintw(win, rows / 2, (cols - 16) / 2, "No thread open");
        wnoutrefresh(win);
        return;
    }

    // Title bar
    wattron(win, COLOR_PAIR(3) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 1, "%-*.*s", cols - 2, cols - 2, thread->subject);
    wattroff(win, COLOR_PAIR(3) | A_BOLD);

    // Build a flat line array from all messages in the thread.
    // Lazily allocate current_message as an array of Message.
    if (!state->current_message) {
        state->current_message = calloc(thread->count, sizeof(Message));
        if (!state->current_message) { wnoutrefresh(win); return; }

        for (size_t i = 0; i < thread->count; i++) {
            Message *m = &state->current_message[i];
            m->header = thread->headers[i];
            uint32_t uid = thread->headers[i].uid;

            char *body = NULL;
            size_t body_len = 0;

            // Check cache first; fall back to IMAP
            if (state->cache && cache_has_body(state->cache, uid)) {
                cache_load_body(state->cache, uid, &body, &body_len);
            } else if (imap_fetch_body(&state->session.imap_conn,
                                       uid, &body, &body_len) == 0) {
                if (state->cache)
                    cache_save_body(state->cache, uid, body, body_len);
            }

            if (body) {
                m->body.data     = body;
                m->body.data_len = body_len;
            }
        }
    }

    // Collect all lines into a flat array
    int max_lines = 32768;
    Line *lines = malloc(max_lines * sizeof(Line));
    if (!lines) { wnoutrefresh(win); return; }

    // Also keep extracted text buffers so pointers stay valid
    char **texts = calloc(thread->count, sizeof(char *));
    char **hdrs  = calloc(thread->count, sizeof(char *));

    int total = 0;
    for (size_t i = 0; i < thread->count && total < max_lines - 64; i++) {
        Message *m = &state->current_message[i];
        MessageHeader *h = &m->header;

        // Divider: "From Name — Date"
        char divider[256];
        snprintf(divider, sizeof(divider), "── %s  %s ──",
                 h->from_name[0] ? h->from_name : h->from_address,
                 h->date);
        hdrs[i] = strdup(divider);
        total += collect_lines(lines + total, max_lines - total, hdrs[i], 1);

        // Body
        if (m->body.data) {
            texts[i] = extract_text(m->body.data);
            if (texts[i])
                total += collect_lines(lines + total, max_lines - total, texts[i], 0);
        }

        // Blank separator between messages
        if (i + 1 < thread->count && total < max_lines - 1)
            lines[total++] = (Line){ "", 0, 0 };
    }

    // Render body area
    int body_start = 1;
    int body_rows  = rows - body_start;

    int scroll = state->ui_state.scroll_offset;
    if (total > body_rows && scroll > total - body_rows) scroll = total - body_rows;
    if (scroll < 0) scroll = 0;
    state->ui_state.scroll_offset = scroll;

    for (int i = 0; i < body_rows && (scroll + i) < total; i++) {
        Line *ln = &lines[scroll + i];
        int len  = ln->len > cols - 1 ? cols - 1 : ln->len;
        if (ln->is_header) {
            wattron(win, COLOR_PAIR(3) | A_BOLD);
            mvwprintw(win, body_start + i, 0, "%.*s", len, ln->ptr);
            wattroff(win, COLOR_PAIR(3) | A_BOLD);
        } else {
            mvwprintw(win, body_start + i, 0, "%.*s", len, ln->ptr);
        }
    }

    for (size_t i = 0; i < thread->count; i++) {
        free(texts[i]);
        free(hdrs[i]);
    }
    free(texts);
    free(hdrs);
    free(lines);

    wnoutrefresh(win);
}
