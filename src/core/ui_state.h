#ifndef ui_state_h
#define ui_state_h

typedef enum ActivePane {
    PANE_LIST,
    PANE_READER,
    PANE_COMPOSER,
    PANE_COMMAND
} ActivePane;

typedef struct UIState {
    int        selected_index;
    ActivePane active_pane;
    int        scroll_offset;
    int        hide_seen;
    char       cmd_buf[512];
    int        cmd_cursor;
} UIState;

void ui_state_init(UIState *ui_state);

#endif /* ui_state_h */
