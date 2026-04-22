#include <time.h>
#include "core/ui_state.h"

void ui_state_init(UIState *ui_state) {
    ui_state->selected_index = 0;
    ui_state->active_pane    = PANE_LIST;
    ui_state->scroll_offset  = 0;
    ui_state->hide_seen      = 1;

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    ui_state->cal_year  = tm->tm_year + 1900;
    ui_state->cal_month = tm->tm_mon  + 1;
    ui_state->cal_day   = tm->tm_mday;
}
