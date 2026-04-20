#ifndef session_h
#define session_h

#include "imap/imap.h"

typedef enum {
    SESSION_STATE_DISCONNECTED,
    SESSION_STATE_CONNECTING,
    SESSION_STATE_AUTHENTICATING,
    SESSION_STATE_CONNECTED,
    SESSION_STATE_READY,
    SESSION_STATE_ERROR
} SessionState;

typedef enum {
    SESSION_ERROR_NONE,
    SESSION_ERROR_NETWORK,
    SESSION_ERROR_AUTHENTICATION,
    SESSION_ERROR_UNKNOWN
} SessionError;

typedef struct Session {
    ImapConnection   imap_conn;
    SessionState     state;

    SessionError     error;
    char             error_message[256];

    char             username[256];
    char             password[128];
} Session;


void session_init(Session *session);
void session_connect(Session *session, const char *server, const char *port);


#endif /* session_h */
