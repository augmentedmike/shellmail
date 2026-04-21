#pragma once

#include <stddef.h>
#include <stdint.h>
#include "core/message.h"

typedef struct Cache Cache;

typedef struct Filter {
    int  id;
    char field[32];    // "from" or "subject"
    char pattern[256];
    char folder[256];
} Filter;

Cache *cache_open(const char *path);   // creates tables if new
void   cache_close(Cache *c);

int    cache_get_last_uid(Cache *c, uint32_t *out);
int    cache_save_headers(Cache *c, const MessageList *list);
int    cache_load_headers(Cache *c, MessageList *out);

int    cache_has_body(Cache *c, uint32_t uid);
int    cache_save_body(Cache *c, uint32_t uid, const char *body, size_t len);
int    cache_load_body(Cache *c, uint32_t uid, char **out, size_t *out_len);

int    cache_update_flags(Cache *c, uint32_t uid, uint32_t flags);
int    cache_mark_all_seen(Cache *c);
int    cache_bulk_update_flags(Cache *c, const uint32_t *uids,
                                const uint32_t *flags, size_t count);
int    cache_update_folder(Cache *c, uint32_t uid, const char *folder);

// Filters
int    cache_save_filter(Cache *c, const char *field, const char *pattern, const char *folder);
int    cache_load_filters(Cache *c, Filter **out, size_t *count);
// Returns UIDs of INBOX messages matching the filter (caller frees *out)
int    cache_get_matching_uids(Cache *c, const char *field, const char *pattern,
                                uint32_t **out, size_t *out_count);

