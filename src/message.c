#include <stdlib.h>
#include <string.h>
#include "message.h"

void message_list_init(MessageList *list) {
    list->headers = NULL;
    list->count = 0;
}

void message_list_reverse(MessageList *list) {
    if (list->count < 2) return;
    size_t lo = 0, hi = list->count - 1;
    while (lo < hi) {
        MessageHeader tmp   = list->headers[lo];
        list->headers[lo++] = list->headers[hi];
        list->headers[hi--] = tmp;
    }
}

void message_list_free(MessageList *list) {
    free(list->headers);
    list->headers = NULL;
    list->count = 0;
}

void message_free(Message *msg) {
    if (!msg) return;
    free(msg->body.data);
    for (size_t i = 0; i < msg->body.attachment_count; i++)
        free(msg->body.attachments[i].data);
    free(msg->body.attachments);
}

// ---------------------------------------------------------------------------
// Threading
// ---------------------------------------------------------------------------

// Find thread by id in the growing array, return index or -1
static int find_thread(ThreadList *tl, uint64_t tid) {
    for (size_t i = 0; i < tl->count; i++)
        if (tl->threads[i].thread_id == tid) return (int)i;
    return -1;
}

// Append a name to the participants string if not already present
static void add_participant(char *buf, size_t buf_size, const char *name) {
    if (!name[0]) return;
    if (strstr(buf, name)) return;
    size_t cur = strlen(buf);
    if (cur > 0 && cur + 2 < buf_size) {
        buf[cur++] = ',';
        buf[cur++] = ' ';
        buf[cur]   = '\0';
    }
    strncat(buf, name, buf_size - strlen(buf) - 1);
}

// Compare threads by latest_date descending (for qsort)
static int cmp_thread_desc(const void *a, const void *b) {
    const Thread *ta = (const Thread *)a;
    const Thread *tb = (const Thread *)b;
    // Simple lexicographic compare on the date string works reasonably
    // for RFC 2822 dates with consistent timezone formatting
    return strcmp(tb->latest_date, ta->latest_date);
}

void thread_list_build(const MessageList *src, ThreadList *out) {
    out->threads = NULL;
    out->count   = 0;
    if (!src->count) return;

    // Allocate worst-case (every message its own thread)
    out->threads = calloc(src->count, sizeof(Thread));
    if (!out->threads) return;

    // src->headers are oldest-first after message_list_reverse was NOT called,
    // or newest-first if it was. Either way we iterate all of them.
    for (size_t i = 0; i < src->count; i++) {
        const MessageHeader *h = &src->headers[i];

        int idx = find_thread(out, h->thread_id);
        if (idx < 0) {
            // New thread
            idx = (int)out->count++;
            Thread *t    = &out->threads[idx];
            t->thread_id = h->thread_id;
            strncpy(t->subject, h->subject, sizeof(t->subject) - 1);
            t->headers   = malloc(sizeof(MessageHeader));
            t->count     = 0;
        }

        Thread *t = &out->threads[idx];

        // Grow headers array
        MessageHeader *tmp = realloc(t->headers, (t->count + 1) * sizeof(MessageHeader));
        if (!tmp) continue;
        t->headers = tmp;
        t->headers[t->count++] = *h;

        // Update latest_date (keep the most recent)
        if (strcmp(h->date, t->latest_date) > 0)
            strncpy(t->latest_date, h->date, sizeof(t->latest_date) - 1);

        // Accumulate flags
        t->flags |= h->flags;

        // Add to participants
        const char *name = h->from_name[0] ? h->from_name : h->from_address;
        add_participant(t->participants, sizeof(t->participants), name);
    }

    // Sort threads newest-first
    qsort(out->threads, out->count, sizeof(Thread), cmp_thread_desc);
}

void thread_list_free(ThreadList *list) {
    if (!list->threads) return;
    for (size_t i = 0; i < list->count; i++)
        free(list->threads[i].headers);
    free(list->threads);
    list->threads = NULL;
    list->count   = 0;
}
