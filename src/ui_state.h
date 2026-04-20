#ifndef ui_state_h
#define ui_state_h

typedef enum ActivePane {
    PANE_LIST,
    PANE_READER,
    PANE_COMPOSER
} ActivePane;

typedef struct UIState {
    int selected_index;
    ActivePane active_pane;
    int scroll_offset;
} UIState;

void ui_state_init(UIState *ui_state);

#endif /* ui_state_h */