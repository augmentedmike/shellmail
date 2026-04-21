#pragma once

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

// Shared TLS handshake used by both IMAP and SMTP connections.
// Inits net/ssl/conf, connects, and completes the TLS handshake.
// Returns 0 on success, mbedtls error code on failure.
int  tls_connect(mbedtls_net_context *net, mbedtls_ssl_context *ssl,
                 mbedtls_ssl_config  *conf,
                 const char *server, const char *port);

void tls_disconnect(mbedtls_net_context *net, mbedtls_ssl_context *ssl,
                    mbedtls_ssl_config  *conf, mbedtls_x509_crt *crt);
