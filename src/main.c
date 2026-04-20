#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "core/app_state.h"
#include "core/config.h"
#include "imap/imap.h"
#include "cache/cache.h"
#include "sync/sync.h"
#include "ui/ui.h"

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

    // Open cache (~/.shellmail/mail.db)
    char db_dir[512], db_path[512];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(db_dir,  sizeof(db_dir),  "%s/.shellmail", home);
    mkdir(db_dir, 0700);  // ignore EEXIST
    snprintf(db_path, sizeof(db_path), "%s/.shellmail/mail.db", home);
    state->cache = cache_open(db_path);
    if (!state->cache) {
        fprintf(stderr, "Warning: could not open cache at %s\n", db_path);
    }

    // Load cached headers immediately (fast, no network)
    if (state->cache) {
        cache_load_headers(state->cache, &state->message_list);
        thread_list_build(&state->message_list, &state->thread_list);
    }

    // Connect IMAP (UI thread connection — used for body fetching)
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

    // Select INBOX to get exists count; don't fetch headers here (sync does it)
    int exists = 0;
    ret = imap_select(&state->session.imap_conn, "INBOX", &exists);
    if (ret != 0) {
        fprintf(stderr, "SELECT failed\n");
        return 1;
    }

    // Start background sync
    state->sync = sync_create(state);
    sync_start(state->sync);
    sync_request(state->sync);  // trigger first sync immediately

    // Launch UI (returns when user quits)
    ui_run(state);

    imap_logout(&state->session.imap_conn);
    sync_destroy(state->sync);
    if (state->cache) cache_close(state->cache);
    return 0;
}
