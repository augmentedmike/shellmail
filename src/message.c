#include <stdlib.h>
#include "message.h"

void message_list_init(MessageList *list) {
    list->headers = NULL;
    list->count = 0;
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
