#include <stdio.h>
#include <string.h>
#include "core/app_state.h"

Config load_config(struct AppState *state, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open config file: %s\n", filename);
        return (Config){0};
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char *key = strtok(line, ":");
        char *value = strtok(NULL, "\n");

        if (!key || !value) continue;
        while (*value == ' ' || *value == '\t') value++;
        if (strcmp(key, "server") == 0) {
            strncpy(state->config.imap_server, value, sizeof(state->config.imap_server) - 1);
        } else if (strcmp(key, "port") == 0) {
            strncpy(state->config.imap_port, value, sizeof(state->config.imap_port) - 1);
        } else if (strcmp(key, "smtp_server") == 0) {
            strncpy(state->config.smtp_server, value, sizeof(state->config.smtp_server) - 1);
        } else if (strcmp(key, "smtp_port") == 0) {
            strncpy(state->config.smtp_port, value, sizeof(state->config.smtp_port) - 1);
        } else if (strcmp(key, "username") == 0) {
            strncpy(state->config.username, value, sizeof(state->config.username) - 1);
        } else if (strcmp(key, "password") == 0) {
            strncpy(state->config.password, value, sizeof(state->config.password) - 1);
        } else if (strcmp(key, "archive_mailbox") == 0) {
            strncpy(state->config.archive_mailbox, value, sizeof(state->config.archive_mailbox) - 1);
        } else if (strcmp(key, "caldav_url") == 0) {
            strncpy(state->config.caldav_url, value, sizeof(state->config.caldav_url) - 1);
        } else if (strcmp(key, "caldav_username") == 0) {
            strncpy(state->config.caldav_username, value, sizeof(state->config.caldav_username) - 1);
        } else if (strcmp(key, "caldav_password") == 0) {
            strncpy(state->config.caldav_password, value, sizeof(state->config.caldav_password) - 1);
        }
    }

    fclose(file);

    // Default archive folder for Gmail; override in config with archive_mailbox:
    if (!state->config.archive_mailbox[0])
        strncpy(state->config.archive_mailbox, "[Gmail]/All Mail",
                sizeof(state->config.archive_mailbox) - 1);

    // CalDAV credentials fall back to IMAP credentials if not specified
    if (!state->config.caldav_username[0])
        strncpy(state->config.caldav_username, state->config.username,
                sizeof(state->config.caldav_username) - 1);
    if (!state->config.caldav_password[0])
        strncpy(state->config.caldav_password, state->config.password,
                sizeof(state->config.caldav_password) - 1);

    return state->config;
}
