#include "net/session.h"
#include "imap/imap.h"

void session_init(Session *session) {
    session->state         = SESSION_STATE_DISCONNECTED;
    session->error         = SESSION_ERROR_NONE;
    session->error_message[0] = '\0';
    session->username[0]   = '\0';
    session->password[0]   = '\0';
}

void session_connect(Session *session, const char *server, const char *port) {
    session->state = SESSION_STATE_CONNECTING;

    int ret = imap_tls_connect(&session->imap_conn, server, port);
    if (ret != 0) {
        session->state = SESSION_STATE_ERROR;
        snprintf(session->error_message, sizeof(session->error_message),
                 "Connection failed: -0x%04x", -ret);
        return;
    }

    session->state = SESSION_STATE_AUTHENTICATING;
}
