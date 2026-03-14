#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <waitqueue.h>

#define TTY_MAX 4
#define TTY_INPUT_BUF_SIZE 256
#define TTY_LINE_BUF_SIZE 256
#define TTY_ESC_PARAMS_MAX 8

typedef enum { TTY_MODE_COOKED, TTY_MODE_RAW } tty_input_mode_t;
typedef enum { TTY_STATE_NORMAL, TTY_STATE_ESC, TTY_STATE_CSI } tty_parse_state_t;

typedef struct {
    char c;
    uint32_t fg, bg;
    bool bold;
    bool reverse;
} tty_cell_t;

typedef struct {
    int id;
    uint32_t cols, rows;

    uint32_t cursor_row, cursor_col;
    uint32_t prev_cursor_row, prev_cursor_col;
    bool cursor_visible;

    uint32_t fg, bg;
    bool bold;
    bool reverse;

    tty_cell_t *cells;

    tty_input_mode_t input_mode;
    char input_buf[TTY_INPUT_BUF_SIZE];
    volatile uint16_t input_head, input_tail;
    waitqueue_t input_wq;

    char line_buf[TTY_LINE_BUF_SIZE];
    uint16_t line_len;

    tty_parse_state_t parse_state;
    int esc_params[TTY_ESC_PARAMS_MAX];
    int esc_param_count;
    bool esc_has_param;
    bool esc_private;

    uint32_t scroll_top, scroll_bottom;

    bool active;
    bool keyboard_attached;
} tty_t;

void tty_init(void);
tty_t *tty_get(int id);
void tty_write(tty_t *tty, const char *buf, uint32_t len);
void tty_putchar(tty_t *tty, char c);
void tty_input_scancode(tty_t *tty, uint8_t scancode, char ascii, bool ctrl, bool shift);
bool tty_input_push(tty_t *tty, char c);
bool tty_input_pop(tty_t *tty, char *c);
void tty_input_flush(tty_t *tty);
int tty_set_mode(tty_t *tty, tty_input_mode_t mode);
void tty_sync_from_fb(tty_t *tty);
void tty_attach_keyboard(tty_t *tty);
void tty_detach_keyboard(tty_t *tty);
