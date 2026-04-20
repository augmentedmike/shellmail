#ifndef cache_h
#define cache_h

#include <stddef.h>
#include <stdint.h>
#include "core/message.h"

typedef struct Cache Cache;

Cache *cache_open(const char *path);   // creates tables if new
void   cache_close(Cache *c);

int    cache_get_last_uid(Cache *c, uint32_t *out);
int    cache_save_headers(Cache *c, const MessageList *list);
int    cache_load_headers(Cache *c, MessageList *out);

int    cache_has_body(Cache *c, uint32_t uid);
int    cache_save_body(Cache *c, uint32_t uid, const char *body, size_t len);
int    cache_load_body(Cache *c, uint32_t uid, char **out, size_t *out_len);

int    cache_update_flags(Cache *c, uint32_t uid, uint32_t flags);

#endif
