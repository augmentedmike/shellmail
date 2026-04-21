#pragma once

typedef struct Config {
    char imap_server[256];
    char imap_port[6];
    char smtp_server[256];
    char smtp_port[6];
    char username[256];
    char password[128];
    char archive_mailbox[256];  // defaults to [Gmail]/All Mail
} Config;

struct AppState;
Config load_config(struct AppState *state, const char *filename);

