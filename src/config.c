#include <stdio.h>
#include <string.h>
#include "app_state.h"

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
        } else if (strcmp(key, "username") == 0) {
            strncpy(state->config.username, value, sizeof(state->config.username) - 1);
        } else if (strcmp(key, "password") == 0) {
            strncpy(state->config.password, value, sizeof(state->config.password) - 1);
        }
    }

    fclose(file);

    return state->config;
}
