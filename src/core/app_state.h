#pragma once

#include <stdatomic.h>

#include "core/config.h"
#include "core/ui_state.h"
#include "net/session.h"
#include "core/message.h"

struct Cache;
struct SyncContext;

typedef struct AppState {
    Config       config;
    UIState      ui_state;
    Session      session;
    MessageList  message_list;
    ThreadList   thread_list;
    Thread      *current_thread;   // points into thread_list, not owned
    Message     *current_message;
    struct Cache       *cache;
    struct SyncContext *sync;

    // Filtered view of thread_list (non-owning pointers)
    Thread     **view;
    size_t       view_count;
} AppState;

extern _Atomic(AppState*) app_state;

AppState *appstate_init(void);
void      appstate_rebuild_view(AppState *state);
