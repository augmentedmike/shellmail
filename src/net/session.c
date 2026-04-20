#include <stdio.h>
#include <stdlib.h>
#include <psa/crypto.h>
#include "net/session.h"

void session_init(Session *session) {
    session->state = SESSION_STATE_DISCONNECTED;
    session->error = SESSION_ERROR_NONE;
    session->error_message[0] = '\0';
    session->username[0] = '\0';
    session->password[0] = '\0';
}

void session_connect(Session *session, const char *server, const char *port) {
    session->state = SESSION_STATE_CONNECTING;

    psa_crypto_init();

    mbedtls_net_init(&session->imap_conn.net_ctx);
    mbedtls_ssl_init(&session->imap_conn.ssl_ctx);
    mbedtls_ssl_config_init(&session->imap_conn.ssl_conf);
    mbedtls_x509_crt_init(&session->imap_conn.crt);

    int ret = mbedtls_net_connect(&session->imap_conn.net_ctx, server, port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        session->state = SESSION_STATE_ERROR;
        snprintf(session->error_message, sizeof(session->error_message), "Network connection failed: -0x%04x", -ret);
        return;
    }

    mbedtls_ssl_config_defaults(&session->imap_conn.ssl_conf,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);

    // TODO: restore MBEDTLS_SSL_VERIFY_REQUIRED once cert loading is fixed
    mbedtls_ssl_conf_authmode(&session->imap_conn.ssl_conf, MBEDTLS_SSL_VERIFY_NONE);

    ret = mbedtls_ssl_setup(&session->imap_conn.ssl_ctx, &session->imap_conn.ssl_conf);
    if (ret != 0) {
        session->state = SESSION_STATE_ERROR;
        snprintf(session->error_message, sizeof(session->error_message), "SSL setup failed: -0x%04x", -ret);
        return;
    }

    mbedtls_ssl_set_hostname(&session->imap_conn.ssl_ctx, server);
    mbedtls_ssl_set_bio(&session->imap_conn.ssl_ctx, &session->imap_conn.net_ctx, mbedtls_net_send, mbedtls_net_recv, NULL);

    ret = mbedtls_ssl_handshake(&session->imap_conn.ssl_ctx);
    if (ret != 0) {
        session->state = SESSION_STATE_ERROR;
        snprintf(session->error_message, sizeof(session->error_message), "SSL handshake failed: -0x%04x", -ret);
        return;
    }

    // Read and discard the server greeting before handing off to IMAP layer
    char greeting[512];
    int n = imap_recv(&session->imap_conn, greeting, sizeof(greeting));
    if (n < 0) {
        session->state = SESSION_STATE_ERROR;
        snprintf(session->error_message, sizeof(session->error_message), "Failed to read server greeting: -0x%04x", -n);
        return;
    }

    session->state = SESSION_STATE_AUTHENTICATING;
}
