#include <stdlib.h>
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
