#include <stdlib.h>
#include <string.h>
#include "core/app_state.h"
#include "core/message.h"

AppState *appstate_init(void) {
    AppState *state = malloc(sizeof(AppState));
    if (!state) return NULL;
    memset(state, 0, sizeof(AppState));
    ui_state_init(&state->ui_state);
    session_init(&state->session);
    message_list_init(&state->message_list);
    return state;
}

// Rebuild the filtered view of thread_list based on ui_state.hide_seen.
// Pointers into thread_list.threads — not owned.
void appstate_rebuild_view(AppState *state) {
    free(state->view);
    state->view       = NULL;
    state->view_count = 0;

    size_t n = state->thread_list.count;
    if (!n) return;

    state->view = malloc(n * sizeof(Thread *));
    if (!state->view) return;

    for (size_t i = 0; i < n; i++) {
        Thread *t = &state->thread_list.threads[i];
        if (state->ui_state.hide_seen && (t->flags & FLAG_SEEN))
            continue;
        state->view[state->view_count++] = t;
    }

    // Clamp selection
    if (state->view_count == 0) {
        state->ui_state.selected_index = 0;
    } else if ((size_t)state->ui_state.selected_index >= state->view_count) {
        state->ui_state.selected_index = (int)state->view_count - 1;
    }
}
