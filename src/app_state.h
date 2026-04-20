#ifndef app_state_h
#define app_state_h

#include <stdatomic.h>

#include "config.h"
#include "ui_state.h"
#include "session.h"
#include "message.h"

typedef struct AppState{
    Config config;
    UIState ui_state;
    Session session;
    MessageList message_list;
    Message *current_message;
} AppState;

extern _Atomic(AppState*) app_state;

AppState *appstate_init(void);
#endif /* app_state_h */