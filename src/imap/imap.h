#pragma once

#include <stddef.h>
#include <stdint.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include "core/message.h"

// Forward-declare Cache so imap_sync_flags can reference it without
// pulling in all of cache.h (which would create a circular header chain).
typedef struct Cache Cache;

typedef struct ImapConnection {
    mbedtls_net_context net_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config  ssl_conf;
    mbedtls_x509_crt    crt;

    int tag_counter;
} ImapConnection;

// TLS connect / disconnect (also used by sync.c and session.c)
int  imap_tls_connect(ImapConnection *conn, const char *server, const char *port);
void imap_tls_disconnect(ImapConnection *conn);

// Low-level send/recv
int  imap_send(ImapConnection *conn, const char *buf);
int  imap_recv(ImapConnection *conn, char *buf, size_t buf_size);

// Read until the tagged completion line; caller must free *out
int  imap_recv_response(ImapConnection *conn, int tag, char **out, size_t *out_len);

// Auth
int  imap_login(ImapConnection *conn, const char *username, const char *password);
int  imap_logout(ImapConnection *conn);

// Mailbox
int  imap_select(ImapConnection *conn, const char *mailbox, int *out_exists);
int  imap_list(ImapConnection *conn, char ***out_names, int *out_count);
int  imap_create_mailbox(ImapConnection *conn, const char *name);
// Move message to dest folder: UID COPY + UID STORE \Deleted + EXPUNGE
int  imap_uid_move(ImapConnection *conn, uint32_t uid, const char *dest);

// Mark all messages in currently-selected mailbox as \Seen
int  imap_mark_all_seen(ImapConnection *conn);

// Messages
// Fetch headers for messages start..end (inclusive, 1-based seq nums) into list
int  imap_fetch_headers(ImapConnection *conn, int start, int end, MessageList *list);

// Fetch FLAGS for all messages and update cache in bulk
int  imap_sync_flags(ImapConnection *conn, Cache *c);

// Fetch full RFC 2822 body for uid; caller must free *out
int  imap_fetch_body(ImapConnection *conn, uint32_t uid, char **out, size_t *out_len);

