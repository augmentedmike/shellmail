#include "app_state.h"
#include <string.h>

AppState *appstate_init(void) {
    AppState *state = malloc(sizeof(AppState));
    if (!state) {
        return NULL;
    }
    memset(state, 0, sizeof(AppState));
    // Initialize the state components
    ui_state_init(&state->ui_state);
    session_init(&state->session);
    message_list_init(&state->message_list);
    state->current_message = NULL;
    return state;
}

