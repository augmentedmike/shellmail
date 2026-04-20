#ifndef smtp_h
#define smtp_h

#include <stddef.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

typedef struct SmtpConnection {
    mbedtls_net_context net_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config  ssl_conf;
    mbedtls_x509_crt    crt;
} SmtpConnection;

typedef struct SmtpMessage {
    const char *from;
    const char *to;
    const char *subject;
    const char *body;
} SmtpMessage;

int smtp_connect(SmtpConnection *conn, const char *server, const char *port);
int smtp_login(SmtpConnection *conn, const char *username, const char *password);
int smtp_send(SmtpConnection *conn, const SmtpMessage *msg);
void smtp_disconnect(SmtpConnection *conn);

#endif /* smtp_h */
