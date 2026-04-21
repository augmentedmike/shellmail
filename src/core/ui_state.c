#include "core/ui_state.h"

void ui_state_init(UIState *ui_state) {
    ui_state->selected_index = 0;
    ui_state->active_pane = PANE_LIST;
    ui_state->scroll_offset = 0;
    ui_state->hide_seen = 1;
}
