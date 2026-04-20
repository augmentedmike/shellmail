#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psa/crypto.h>
#include <mbedtls/base64.h>
#include "smtp.h"

// ---------------------------------------------------------------------------
// Low-level I/O
// ---------------------------------------------------------------------------

static int smtp_write(SmtpConnection *conn, const char *buf) {
    int len = (int)strlen(buf);
    int written = 0;
    while (written < len) {
        int ret = mbedtls_ssl_write(&conn->ssl_ctx,
                                    (const unsigned char *)buf + written,
                                    len - written);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) return ret;
        written += ret;
    }
    return written;
}

static int smtp_read_line(SmtpConnection *conn, char *buf, size_t buf_size) {
    int ret;
    do {
        ret = mbedtls_ssl_read(&conn->ssl_ctx, (unsigned char *)buf, buf_size - 1);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (ret < 0) return ret;
    buf[ret] = '\0';
    return ret;
}

// Read until we see a line starting with "CODE " (not "CODE-")
static int smtp_read_response(SmtpConnection *conn, int expected_code) {
    char buf[2048];
    char code_str[8];
    snprintf(code_str, sizeof(code_str), "%d ", expected_code);

    while (1) {
        int n = smtp_read_line(conn, buf, sizeof(buf));
        if (n <= 0) return -1;
        if (strncmp(buf, code_str, 4) == 0) return 0;
        // Multi-line: "CODE-..." means more lines follow — keep reading
        char prefix[5];
        snprintf(prefix, sizeof(prefix), "%d-", expected_code);
        if (strncmp(buf, prefix, 4) != 0) return -1; // unexpected code
    }
}

static char *base64_encode(const char *input) {
    size_t in_len = strlen(input);
    size_t out_len = 0;
    mbedtls_base64_encode(NULL, 0, &out_len, (const unsigned char *)input, in_len);
    char *out = malloc(out_len + 1);
    if (!out) return NULL;
    mbedtls_base64_encode((unsigned char *)out, out_len, &out_len,
                          (const unsigned char *)input, in_len);
    out[out_len] = '\0';
    return out;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int smtp_connect(SmtpConnection *conn, const char *server, const char *port) {
    psa_crypto_init();

    mbedtls_net_init(&conn->net_ctx);
    mbedtls_ssl_init(&conn->ssl_ctx);
    mbedtls_ssl_config_init(&conn->ssl_conf);
    mbedtls_x509_crt_init(&conn->crt);

    int ret = mbedtls_net_connect(&conn->net_ctx, server, port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) return ret;

    ret = mbedtls_ssl_config_defaults(&conn->ssl_conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return ret;

    mbedtls_ssl_conf_authmode(&conn->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);

    ret = mbedtls_ssl_setup(&conn->ssl_ctx, &conn->ssl_conf);
    if (ret != 0) return ret;

    mbedtls_ssl_set_hostname(&conn->ssl_ctx, server);
    mbedtls_ssl_set_bio(&conn->ssl_ctx, &conn->net_ctx,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    ret = mbedtls_ssl_handshake(&conn->ssl_ctx);
    if (ret != 0) return ret;

    // Read greeting (220)
    return smtp_read_response(conn, 220);
}

int smtp_login(SmtpConnection *conn, const char *username, const char *password) {
    char cmd[512];

    // EHLO
    snprintf(cmd, sizeof(cmd), "EHLO shellmail\r\n");
    if (smtp_write(conn, cmd) < 0) return -1;
    if (smtp_read_response(conn, 250) != 0) return -1;

    // AUTH LOGIN
    if (smtp_write(conn, "AUTH LOGIN\r\n") < 0) return -1;
    if (smtp_read_response(conn, 334) != 0) return -1;

    char *enc_user = base64_encode(username);
    if (!enc_user) return -1;
    snprintf(cmd, sizeof(cmd), "%s\r\n", enc_user);
    free(enc_user);
    if (smtp_write(conn, cmd) < 0) return -1;
    if (smtp_read_response(conn, 334) != 0) return -1;

    char *enc_pass = base64_encode(password);
    if (!enc_pass) return -1;
    snprintf(cmd, sizeof(cmd), "%s\r\n", enc_pass);
    free(enc_pass);
    if (smtp_write(conn, cmd) < 0) return -1;
    if (smtp_read_response(conn, 235) != 0) return -1;

    return 0;
}

int smtp_send(SmtpConnection *conn, const SmtpMessage *msg) {
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "MAIL FROM:<%s>\r\n", msg->from);
    if (smtp_write(conn, cmd) < 0) return -1;
    if (smtp_read_response(conn, 250) != 0) return -1;

    snprintf(cmd, sizeof(cmd), "RCPT TO:<%s>\r\n", msg->to);
    if (smtp_write(conn, cmd) < 0) return -1;
    if (smtp_read_response(conn, 250) != 0) return -1;

    if (smtp_write(conn, "DATA\r\n") < 0) return -1;
    if (smtp_read_response(conn, 354) != 0) return -1;

    // Headers
    snprintf(cmd, sizeof(cmd), "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n",
             msg->from, msg->to, msg->subject);
    if (smtp_write(conn, cmd) < 0) return -1;

    // Body — dot-stuff lines starting with "."
    const char *p = msg->body;
    while (*p) {
        const char *eol = strchrnul(p, '\n');
        size_t line_len = eol - p + (*eol ? 1 : 0);
        if (p[0] == '.') smtp_write(conn, ".");
        char line[1024];
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        if (smtp_write(conn, line) < 0) return -1;
        p += line_len;
    }

    if (smtp_write(conn, "\r\n.\r\n") < 0) return -1;
    if (smtp_read_response(conn, 250) != 0) return -1;

    return 0;
}

void smtp_disconnect(SmtpConnection *conn) {
    smtp_write(conn, "QUIT\r\n");
    mbedtls_ssl_close_notify(&conn->ssl_ctx);
    mbedtls_net_free(&conn->net_ctx);
    mbedtls_ssl_free(&conn->ssl_ctx);
    mbedtls_ssl_config_free(&conn->ssl_conf);
    mbedtls_x509_crt_free(&conn->crt);
}
