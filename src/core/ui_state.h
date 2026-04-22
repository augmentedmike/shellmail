#pragma once

typedef enum ActivePane {
    PANE_LIST,
    PANE_READER,
    PANE_COMPOSER,
    PANE_COMMAND,
    PANE_CALENDAR
} ActivePane;

// A URL detected on-screen, used for OSC 8 hyperlink injection.
#define MAX_SCREEN_LINKS 24
typedef struct {
    short row;    // terminal row (0-based, window-relative)
    short col;    // terminal column (0-based)
    short len;    // visible display length
    char  url[512];
} ScreenLink;

typedef struct UIState {
    int        selected_index;
    ActivePane active_pane;
    int        scroll_offset;
    int        hide_seen;
    char       cmd_buf[512];
    int        cmd_cursor;
    // Calendar pane state
    int        cal_year;
    int        cal_month;  // 1-12
    int        cal_day;    // 1-31
    // Screen links for OSC 8 hyperlink injection (populated by draw_reader)
    ScreenLink links[MAX_SCREEN_LINKS];
    int        link_count;
} UIState;

void ui_state_init(UIState *ui_state);

