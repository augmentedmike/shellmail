#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app_state.h"
#include "config.h"
#include "imap.h"

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

    int ret = imap_login(&state->session.imap_conn, state->config.username, state->config.password);
    if (ret != 0) {
        fprintf(stderr, "Login failed\n");
        return 1;
    }

    return 0;
}