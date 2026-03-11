#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <font.h>
#include <framebuffer.h>
#include <limine.h>
#include <keyboard.h>
#include <stdio.h>
#include <tty.h>
#include <waitqueue.h>

static tty_t ttys[TTY_MAX];

static const uint32_t ansi_colors[8] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
};

static const uint32_t ansi_bright[8] = {
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};

static void tty_sync_cursor(tty_t *tty)
{
    fb_set_cursor(tty->cursor_col * char_width, tty->cursor_row * char_height);
}

static void tty_scroll(tty_t *tty)
{
    fb_scroll();
    if (tty->cursor_row > 0)
        tty->cursor_row--;
}

static void tty_raw_putchar(tty_t *tty, char c)
{
    uint32_t saved_fg = fb_get_fg();
    uint32_t saved_bg = fb_get_bg();
    fb_set_color(tty->fg, tty->bg);

    if (c == '\n') {
        tty->cursor_col = 0;
        tty->cursor_row++;
        if (tty->cursor_row >= tty->rows) {
            tty_scroll(tty);
        }
        tty_sync_cursor(tty);
    } else if (c == '\r') {
        tty->cursor_col = 0;
        tty_sync_cursor(tty);
    } else if (c == '\b') {
        if (tty->cursor_col > 0) {
            tty->cursor_col--;
            tty_sync_cursor(tty);
            fb_putchar_at(' ', tty->cursor_col * char_width,
                          tty->cursor_row * char_height);
        }
    } else if (c == '\t') {
        uint32_t next = (tty->cursor_col + 4) & ~3u;
        if (next > tty->cols)
            next = tty->cols;
        tty->cursor_col = next;
        if (tty->cursor_col >= tty->cols) {
            tty->cursor_col = 0;
            tty->cursor_row++;
            if (tty->cursor_row >= tty->rows)
                tty_scroll(tty);
        }
        tty_sync_cursor(tty);
    } else if (c == '\a') {
        // bell - ignore
    } else if (c >= 32 || c < 0) {
        fb_putchar_at(c, tty->cursor_col * char_width,
                      tty->cursor_row * char_height);
        tty->cursor_col++;
        if (tty->cursor_col >= tty->cols) {
            tty->cursor_col = 0;
            tty->cursor_row++;
            if (tty->cursor_row >= tty->rows)
                tty_scroll(tty);
        }
        tty_sync_cursor(tty);
    }

    fb_set_color(saved_fg, saved_bg);
}

static int esc_param(tty_t *tty, int idx, int def)
{
    if (idx < tty->esc_param_count && tty->esc_params[idx] >= 0)
        return tty->esc_params[idx];
    return def;
}

static void tty_handle_sgr(tty_t *tty)
{
    if (tty->esc_param_count == 0) {
        tty->fg = 0xFFFFFF;
        tty->bg = 0x000000;
        tty->bold = false;
        return;
    }

    for (int i = 0; i < tty->esc_param_count; i++) {
        int p = tty->esc_params[i];
        if (p < 0) p = 0;

        if (p == 0) {
            tty->fg = 0xFFFFFF;
            tty->bg = 0x000000;
            tty->bold = false;
        } else if (p == 1) {
            tty->bold = true;
        } else if (p >= 30 && p <= 37) {
            tty->fg = tty->bold ? ansi_bright[p - 30] : ansi_colors[p - 30];
        } else if (p >= 40 && p <= 47) {
            tty->bg = ansi_colors[p - 40];
        } else if (p == 39) {
            tty->fg = 0xFFFFFF;
        } else if (p == 49) {
            tty->bg = 0x000000;
        } else if (p >= 90 && p <= 97) {
            tty->fg = ansi_bright[p - 90];
        } else if (p >= 100 && p <= 107) {
            tty->bg = ansi_bright[p - 100];
        }
    }
}

static void tty_handle_csi(tty_t *tty, char c)
{
    int n;
    switch (c) {
    case 'A': // cursor up
        n = esc_param(tty, 0, 1);
        tty->cursor_row = (uint32_t)n > tty->cursor_row ? 0 : tty->cursor_row - n;
        tty_sync_cursor(tty);
        break;
    case 'B': // cursor down
        n = esc_param(tty, 0, 1);
        tty->cursor_row += n;
        if (tty->cursor_row >= tty->rows)
            tty->cursor_row = tty->rows - 1;
        tty_sync_cursor(tty);
        break;
    case 'C': // cursor forward
        n = esc_param(tty, 0, 1);
        tty->cursor_col += n;
        if (tty->cursor_col >= tty->cols)
            tty->cursor_col = tty->cols - 1;
        tty_sync_cursor(tty);
        break;
    case 'D': // cursor back
        n = esc_param(tty, 0, 1);
        tty->cursor_col = (uint32_t)n > tty->cursor_col ? 0 : tty->cursor_col - n;
        tty_sync_cursor(tty);
        break;
    case 'H':
    case 'f': { // cursor position (1-based)
        int row = esc_param(tty, 0, 1) - 1;
        int col = esc_param(tty, 1, 1) - 1;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        if ((uint32_t)row >= tty->rows) row = tty->rows - 1;
        if ((uint32_t)col >= tty->cols) col = tty->cols - 1;
        tty->cursor_row = row;
        tty->cursor_col = col;
        tty_sync_cursor(tty);
        break;
    }
    case 'J': { // erase display
        n = esc_param(tty, 0, 0);
        if (n == 0) {
            // erase from cursor to end
            uint32_t cx = tty->cursor_col * char_width;
            uint32_t cy = tty->cursor_row * char_height;
            fb_clear_region(cx, cy, fb->width, cy + char_height);
            if (tty->cursor_row + 1 < tty->rows)
                fb_clear_region(0, (tty->cursor_row + 1) * char_height,
                                fb->width, tty->rows * char_height);
        } else if (n == 2) {
            fb_clear();
            tty->cursor_row = 0;
            tty->cursor_col = 0;
            tty_sync_cursor(tty);
        }
        break;
    }
    case 'K': { // erase line
        n = esc_param(tty, 0, 0);
        uint32_t cy = tty->cursor_row * char_height;
        if (n == 0) {
            fb_clear_region(tty->cursor_col * char_width, cy,
                            fb->width, cy + char_height);
        } else if (n == 2) {
            fb_clear_region(0, cy, fb->width, cy + char_height);
        }
        break;
    }
    case 'm':
        tty_handle_sgr(tty);
        break;
    case 'n': { // device status report
        n = esc_param(tty, 0, 0);
        if (n == 6) {
            char resp[20];
            int len = 0;
            resp[len++] = '\x1b';
            resp[len++] = '[';
            // row (1-based)
            int row = tty->cursor_row + 1;
            if (row >= 100) resp[len++] = '0' + row / 100;
            if (row >= 10) resp[len++] = '0' + (row / 10) % 10;
            resp[len++] = '0' + row % 10;
            resp[len++] = ';';
            // col (1-based)
            int col = tty->cursor_col + 1;
            if (col >= 100) resp[len++] = '0' + col / 100;
            if (col >= 10) resp[len++] = '0' + (col / 10) % 10;
            resp[len++] = '0' + col % 10;
            resp[len++] = 'R';
            for (int i = 0; i < len; i++)
                tty_input_push(tty, resp[i]);
        }
        break;
    }
    }
}

void tty_putchar(tty_t *tty, char c)
{
    switch (tty->parse_state) {
    case TTY_STATE_NORMAL:
        if (c == '\x1b') {
            tty->parse_state = TTY_STATE_ESC;
        } else {
            tty_raw_putchar(tty, c);
        }
        break;

    case TTY_STATE_ESC:
        if (c == '[') {
            tty->parse_state = TTY_STATE_CSI;
            tty->esc_param_count = 0;
            tty->esc_has_param = false;
            for (int i = 0; i < TTY_ESC_PARAMS_MAX; i++)
                tty->esc_params[i] = -1;
        } else {
            tty->parse_state = TTY_STATE_NORMAL;
        }
        break;

    case TTY_STATE_CSI:
        if (c >= '0' && c <= '9') {
            if (!tty->esc_has_param) {
                tty->esc_has_param = true;
                if (tty->esc_param_count < TTY_ESC_PARAMS_MAX) {
                    tty->esc_params[tty->esc_param_count] = 0;
                }
            }
            if (tty->esc_param_count < TTY_ESC_PARAMS_MAX) {
                tty->esc_params[tty->esc_param_count] =
                    tty->esc_params[tty->esc_param_count] * 10 + (c - '0');
            }
        } else if (c == ';') {
            if (tty->esc_has_param) {
                tty->esc_param_count++;
            } else {
                if (tty->esc_param_count < TTY_ESC_PARAMS_MAX)
                    tty->esc_params[tty->esc_param_count] = -1;
                tty->esc_param_count++;
            }
            tty->esc_has_param = false;
        } else if (c >= 0x40 && c <= 0x7e) {
            if (tty->esc_has_param)
                tty->esc_param_count++;
            tty_handle_csi(tty, c);
            tty->parse_state = TTY_STATE_NORMAL;
        } else {
            tty->parse_state = TTY_STATE_NORMAL;
        }
        break;
    }
}

void tty_write(tty_t *tty, const char *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        tty_putchar(tty, buf[i]);
}

bool tty_input_push(tty_t *tty, char c)
{
    uint16_t next = (tty->input_head + 1) % TTY_INPUT_BUF_SIZE;
    if (next == tty->input_tail)
        return false;
    tty->input_buf[tty->input_head] = c;
    tty->input_head = next;
    return true;
}

bool tty_input_pop(tty_t *tty, char *c)
{
    if (tty->input_tail == tty->input_head)
        return false;
    *c = tty->input_buf[tty->input_tail];
    tty->input_tail = (tty->input_tail + 1) % TTY_INPUT_BUF_SIZE;
    return true;
}

void tty_input_flush(tty_t *tty)
{
    tty->input_head = 0;
    tty->input_tail = 0;
    tty->line_len = 0;
}

void tty_input_scancode(tty_t *tty, uint8_t scancode, char ascii,
                        bool ctrl, bool shift)
{
    if (tty->input_mode == TTY_MODE_RAW) {
        // Arrow keys generate escape sequences
        switch (scancode) {
        case KEY_ARROW_UP:
            tty_input_push(tty, '\x1b');
            tty_input_push(tty, '[');
            tty_input_push(tty, 'A');
            waitqueue_wake_all(&tty->input_wq);
            return;
        case KEY_ARROW_DOWN:
            tty_input_push(tty, '\x1b');
            tty_input_push(tty, '[');
            tty_input_push(tty, 'B');
            waitqueue_wake_all(&tty->input_wq);
            return;
        case KEY_ARROW_RIGHT:
            tty_input_push(tty, '\x1b');
            tty_input_push(tty, '[');
            tty_input_push(tty, 'C');
            waitqueue_wake_all(&tty->input_wq);
            return;
        case KEY_ARROW_LEFT:
            tty_input_push(tty, '\x1b');
            tty_input_push(tty, '[');
            tty_input_push(tty, 'D');
            waitqueue_wake_all(&tty->input_wq);
            return;
        case KEY_DELETE:
            tty_input_push(tty, '\x1b');
            tty_input_push(tty, '[');
            tty_input_push(tty, '3');
            tty_input_push(tty, '~');
            waitqueue_wake_all(&tty->input_wq);
            return;
        }

        // Home/End via scancode (these map to no ascii in scancode_map)
        // Home and End aren't in the scancode enum; handle if needed

        if (ascii) {
            if (ctrl) {
                ascii = ascii & 0x1f;
            } else if (shift) {
                char shifted = scancode_shift_map[scancode];
                if (shifted)
                    ascii = shifted;
            }
            tty_input_push(tty, ascii);
            waitqueue_wake_all(&tty->input_wq);
        }
        return;
    }

    // Cooked mode: arrow keys are ignored
    if (scancode == KEY_ARROW_UP || scancode == KEY_ARROW_DOWN ||
        scancode == KEY_ARROW_LEFT || scancode == KEY_ARROW_RIGHT ||
        scancode == KEY_DELETE)
        return;

    if (!ascii)
        return;

    char ch = ascii;
    if (ctrl) {
        ch = ch & 0x1f;
    } else if (shift) {
        char shifted = scancode_shift_map[scancode];
        if (shifted)
            ch = shifted;
    }

    if (ch == '\x03') {
        // Ctrl+C: push it directly so reader can handle it
        tty->line_len = 0;
        tty_input_push(tty, ch);
        waitqueue_wake_all(&tty->input_wq);
        return;
    }

    if (ch == '\x04') {
        // Ctrl+D: EOF
        if (tty->line_len > 0) {
            // flush current line without newline
            for (uint16_t i = 0; i < tty->line_len; i++)
                tty_input_push(tty, tty->line_buf[i]);
            tty->line_len = 0;
        } else {
            tty_input_push(tty, '\x04');
        }
        waitqueue_wake_all(&tty->input_wq);
        return;
    }

    if (ch == '\n' || ch == '\r') {
        tty_raw_putchar(tty, '\n');
        for (uint16_t i = 0; i < tty->line_len; i++)
            tty_input_push(tty, tty->line_buf[i]);
        tty_input_push(tty, '\n');
        tty->line_len = 0;
        waitqueue_wake_all(&tty->input_wq);
        return;
    }

    if (ch == '\b') {
        if (tty->line_len > 0) {
            tty->line_len--;
            tty_raw_putchar(tty, '\b');
        }
        return;
    }

    if (tty->line_len < TTY_LINE_BUF_SIZE - 1) {
        tty->line_buf[tty->line_len++] = ch;
        tty_raw_putchar(tty, ch);
    }
}

int tty_set_mode(tty_t *tty, tty_input_mode_t mode)
{
    int prev = tty->input_mode;
    tty->input_mode = mode;
    if (mode == TTY_MODE_RAW)
        tty->line_len = 0;
    return prev;
}

void tty_sync_from_fb(tty_t *tty)
{
    tty->cursor_col = cursor.x / char_width;
    tty->cursor_row = cursor.y / char_height;
}

void tty_attach_keyboard(tty_t *tty) { tty->keyboard_attached = true; }
void tty_detach_keyboard(tty_t *tty) { tty->keyboard_attached = false; }

tty_t *tty_get(int id)
{
    if (id < 0 || id >= TTY_MAX)
        return &ttys[0];
    return &ttys[id];
}

void tty_init(void)
{
    uint32_t cols = fb->width / char_width;
    uint32_t rows = fb->height / char_height;

    for (int i = 0; i < TTY_MAX; i++) {
        memset(&ttys[i], 0, sizeof(tty_t));
        ttys[i].id = i;
        ttys[i].cols = cols;
        ttys[i].rows = rows;
        ttys[i].fg = 0xFFFFFF;
        ttys[i].bg = 0x000000;
        ttys[i].input_mode = TTY_MODE_COOKED;
        ttys[i].parse_state = TTY_STATE_NORMAL;
        waitqueue_init(&ttys[i].input_wq);
        ttys[i].active = (i == 0);
    }
}
