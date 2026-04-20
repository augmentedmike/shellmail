#ifndef app_state_h
#define app_state_h

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
} AppState;

extern _Atomic(AppState*) app_state;

AppState *appstate_init(void);
#endif /* app_state_h */
