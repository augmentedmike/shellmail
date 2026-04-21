#pragma once

#include <stdatomic.h>

typedef enum {
    SYNC_IDLE,
    SYNC_CONNECTING,
    SYNC_FETCHING,
    SYNC_DONE,
    SYNC_ERROR
} SyncStatus;

typedef struct SyncContext SyncContext;
struct AppState;

SyncContext *sync_create(struct AppState *state);
void         sync_start(SyncContext *ctx);     // spawns pthread
SyncStatus   sync_status(SyncContext *ctx);    // thread-safe read
void         sync_request(SyncContext *ctx);   // wake worker to re-sync
int          sync_needs_reload(SyncContext *ctx); // 1 if new data available
void         sync_clear_reload(SyncContext *ctx);
void         sync_destroy(SyncContext *ctx);

