#ifndef message_h
#define message_h

#include <stdint.h>
#include <stdlib.h>

#define FLAG_SEEN      (1 << 0)
#define FLAG_ANSWERED  (1 << 1)
#define FLAG_FLAGGED   (1 << 2)
#define FLAG_DELETED   (1 << 3)
#define FLAG_DRAFT     (1 << 4)
#define FLAG_RECENT    (1 << 5)   // server-set, not user-set

typedef struct MessageHeader {
    char         date[64];
    char         subject[256];
    char         from_name[128];
    char         from_address[256];
    char         message_id[256];
    char         in_reply_to[256];

    uint32_t     uid;
    uint32_t     size;
    uint32_t     flags;
     
} MessageHeader;

typedef struct MessageAttachment {
    char         filename[256];
    char         content_type[128];
    char         content_transfer_encoding[64];
    size_t       data_len;
    char         *data;
} MessageAttachment;

typedef struct MessageBody {
    char         content_type[128];
    char         content_transfer_encoding[64];
    char         content_disposition[64];
    size_t       data_len;
    char         *data;

    MessageAttachment *attachments;
    size_t            attachment_count;
    
} MessageBody;

typedef struct Message {
    MessageHeader header;
    MessageBody   body;
} Message;


typedef struct MessageList {
    MessageHeader *headers;
    size_t        count;
} MessageList;

void message_list_init(MessageList *list);



#endif /* message_h */