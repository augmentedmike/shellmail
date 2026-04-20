#ifndef imap_connection_h
#define imap_connection_h

#include <stddef.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include "core/message.h"

typedef struct ImapConnection {
    mbedtls_net_context net_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config  ssl_conf;
    mbedtls_x509_crt    crt;

    int tag_counter;
} ImapConnection;

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

// Messages
// Fetch headers for messages start..end (inclusive, 1-based seq nums) into list
int  imap_fetch_headers(ImapConnection *conn, int start, int end, MessageList *list);

// Fetch full RFC 2822 body for uid; caller must free *out
int  imap_fetch_body(ImapConnection *conn, uint32_t uid, char **out, size_t *out_len);

#endif
