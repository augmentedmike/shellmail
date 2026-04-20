#ifndef imap_connection_h
#define imap_connection_h

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

typedef struct ImapConnection {
    mbedtls_net_context net_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config  ssl_conf;
    mbedtls_x509_crt    crt;

    int tag_counter;
} ImapConnection;


int imap_send(ImapConnection *conn, const char *buf);
int imap_recv(ImapConnection *conn, char *buf, size_t buf_size);
int imap_login(ImapConnection *conn, const char *username, const char *password);
int imap_logout(ImapConnection *conn);


#endif