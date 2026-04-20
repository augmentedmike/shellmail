#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app_state.h"
#include "config.h"
#include "imap.h"
#include "ui.h"

int main(int argc, char *argv[]) {
    AppState *state = appstate_init();
    atomic_store(&app_state, state);

    const char *filename = "config.yaml";
    if (argc > 2 && strcmp(argv[1], "--config") == 0) {
        filename = argv[2];
    } else if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "Usage: %s [--config config_file]\n", argv[0]);
        return 1;
    }
    load_config(state, filename);

    if (state->session.state == SESSION_STATE_ERROR) {
        fprintf(stderr, "Error: %s\n", state->session.error_message);
        return 1;
    }

    session_connect(&state->session, state->config.imap_server, state->config.imap_port);
    if (state->session.state == SESSION_STATE_ERROR) {
        fprintf(stderr, "Connection error: %s\n", state->session.error_message);
        return 1;
    }

    int ret = imap_login(&state->session.imap_conn,
                         state->config.username, state->config.password);
    if (ret != 0) {
        fprintf(stderr, "Login failed\n");
        return 1;
    }

    // Select INBOX
    int exists = 0;
    ret = imap_select(&state->session.imap_conn, "INBOX", &exists);
    if (ret != 0) {
        fprintf(stderr, "SELECT failed\n");
        return 1;
    }
    // Fetch the most recent 50 headers and reverse to show newest first
    int fetch_count = exists < 50 ? exists : 50;
    int start = exists - fetch_count + 1;
    ret = imap_fetch_headers(&state->session.imap_conn, start, exists, &state->message_list);
    if (ret != 0) {
        fprintf(stderr, "FETCH headers failed\n");
        return 1;
    }
    message_list_reverse(&state->message_list);

    ui_run(state);

    imap_logout(&state->session.imap_conn);
    return 0;
}
