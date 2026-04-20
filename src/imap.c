#include <stdio.h>
#include <string.h>
#include "imap.h"

int imap_send(ImapConnection *conn, const char *buf) {
    int len = strlen(buf);
    int written = 0;

    while (written < len) {
        int ret = mbedtls_ssl_write(&conn->ssl_ctx, (const unsigned char *)buf + written, len - written);
        if (ret <= 0) {
            // ret == MBEDTLS_ERR_SSL_WANT_WRITE means try again
            // anything else is a real error
            return ret;
        }
        written += ret;
    }
    return written;
}

int imap_recv(ImapConnection *conn, char *buf, size_t buf_size) {
    int ret;
    do {
        ret = mbedtls_ssl_read(&conn->ssl_ctx, (unsigned char *)buf, buf_size - 1);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (ret < 0) {
        return ret;
    }
    buf[ret] = '\0';
    return ret;
}

int imap_login(ImapConnection *conn, const char *username, const char *password) {
    char buf[512];
    snprintf(buf, sizeof(buf), "a%03d LOGIN %s \"%s\"\r\n", conn->tag_counter++, username, password);
    int ret = imap_send(conn, buf);
    if (ret < 0) {
        return ret;
    }
    ret = imap_recv(conn, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }
    if (strstr(buf, " OK") == NULL) {
        return -1;
    }
    return 0; // Success
}

int imap_logout(ImapConnection *conn) {
    char buf[64];
    snprintf(buf, sizeof(buf), "a%03d LOGOUT\r\n", conn->tag_counter++);
    int ret = imap_send(conn, buf);
    if (ret < 0) {
        return ret;
    }
    // Read response (simplified)
    ret = imap_recv(conn, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }
    if (strstr(buf, " OK") == NULL) {
        return -1; // Logout failed
    }
    return 0; // Success
}