#include "message.h"

void message_list_init(MessageList *list) {
    list->headers = NULL;
    list->count = 0;
}