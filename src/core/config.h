#ifndef config_h
#define config_h


typedef struct Config {
    char imap_server[256];
    char imap_port[6];
    char smtp_server[256];
    char smtp_port[6];
    char username[256];
    char password[128];
} Config;

struct AppState;
Config load_config(struct AppState *state, const char *filename);

#endif /* config_h */
