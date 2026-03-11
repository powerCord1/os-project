#include "api.h"

#define MAX_LINES 512
#define MAX_LINE_LEN 256
#define MAX_FILE_SIZE (MAX_LINES * MAX_LINE_LEN)

static char lines[MAX_LINES][MAX_LINE_LEN];
static int num_lines;
static int cx, cy;
static int scroll_offset;
static int screen_rows, screen_cols;
static char filepath[256];
static int dirty;

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static void strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++));
}

static void clear_screen(void)
{
    puts("\x1b[2J\x1b[H");
}

static void move_cursor(int row, int col)
{
    puts("\x1b[");
    print_num(row + 1);
    putchar(';');
    print_num(col + 1);
    putchar('H');
}

static void draw_status_bar(void)
{
    move_cursor(screen_rows - 1, 0);
    puts("\x1b[7m");
    puts(" ");
    puts(filepath[0] ? filepath : "[new]");
    if (dirty)
        puts(" [modified]");
    puts(" L");
    print_num(cy + 1);
    puts("/");
    print_num(num_lines);
    puts(" C");
    print_num(cx + 1);
    // pad with spaces
    puts("\x1b[K");
    puts("\x1b[0m");
}

static void draw_line(int screen_row, int file_line)
{
    move_cursor(screen_row, 0);
    puts("\x1b[K");
    if (file_line < num_lines) {
        int len = strlen(lines[file_line]);
        if (len > screen_cols)
            len = screen_cols;
        print(lines[file_line], len);
    } else {
        puts("\x1b[34m~\x1b[0m");
    }
}

static void draw_screen(void)
{
    int editable_rows = screen_rows - 1;
    for (int i = 0; i < editable_rows; i++)
        draw_line(i, scroll_offset + i);
    draw_status_bar();
    move_cursor(cy - scroll_offset, cx);
}

static void ensure_visible(void)
{
    int editable_rows = screen_rows - 1;
    if (cy < scroll_offset)
        scroll_offset = cy;
    if (cy >= scroll_offset + editable_rows)
        scroll_offset = cy - editable_rows + 1;
}

static void insert_char(char c)
{
    int len = strlen(lines[cy]);
    if (len >= MAX_LINE_LEN - 1)
        return;
    memmove(lines[cy] + cx + 1, lines[cy] + cx, len - cx + 1);
    lines[cy][cx] = c;
    cx++;
    dirty = 1;
}

static void insert_newline(void)
{
    if (num_lines >= MAX_LINES)
        return;
    memmove(lines + cy + 2, lines + cy + 1,
            (num_lines - cy - 1) * MAX_LINE_LEN);
    num_lines++;
    int tail_len = strlen(lines[cy]) - cx;
    memcpy(lines[cy + 1], lines[cy] + cx, tail_len);
    lines[cy + 1][tail_len] = '\0';
    lines[cy][cx] = '\0';
    cy++;
    cx = 0;
    dirty = 1;
}

static void delete_char(void)
{
    int len = strlen(lines[cy]);
    if (cx < len) {
        memmove(lines[cy] + cx, lines[cy] + cx + 1, len - cx);
        dirty = 1;
    } else if (cy + 1 < num_lines) {
        int next_len = strlen(lines[cy + 1]);
        if (len + next_len < MAX_LINE_LEN) {
            memcpy(lines[cy] + len, lines[cy + 1], next_len + 1);
            memmove(lines + cy + 1, lines + cy + 2,
                    (num_lines - cy - 2) * MAX_LINE_LEN);
            num_lines--;
            dirty = 1;
        }
    }
}

static void backspace(void)
{
    if (cx > 0) {
        cx--;
        delete_char();
    } else if (cy > 0) {
        cx = strlen(lines[cy - 1]);
        cy--;
        delete_char();
    }
}

static void save_file(void)
{
    if (!filepath[0])
        return;

    char buf[MAX_FILE_SIZE];
    int pos = 0;
    for (int i = 0; i < num_lines; i++) {
        int len = strlen(lines[i]);
        memcpy(buf + pos, lines[i], len);
        pos += len;
        if (i < num_lines - 1)
            buf[pos++] = '\n';
    }

    int fd = open_path(filepath, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        move_cursor(screen_rows - 1, 0);
        puts("\x1b[7m Save failed! \x1b[0m");
        return;
    }
    write(fd, buf, pos);
    close(fd);
    dirty = 0;
}

static void load_file(const char *path)
{
    strcpy(filepath, path);

    int fd = open_path(path, O_RDONLY);
    if (fd < 0) {
        num_lines = 1;
        lines[0][0] = '\0';
        return;
    }

    char buf[MAX_FILE_SIZE];
    int total = read(fd, buf, sizeof(buf));
    close(fd);

    num_lines = 0;
    int line_start = 0;
    for (int i = 0; i <= total; i++) {
        if (i == total || buf[i] == '\n') {
            int len = i - line_start;
            if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
            if (num_lines < MAX_LINES) {
                memcpy(lines[num_lines], buf + line_start, len);
                lines[num_lines][len] = '\0';
                num_lines++;
            }
            line_start = i + 1;
        }
    }
    if (num_lines == 0) {
        num_lines = 1;
        lines[0][0] = '\0';
    }
}

void _start(void)
{
    int size = tty_get_size();
    screen_cols = size & 0xFFFF;
    screen_rows = (size >> 16) & 0xFFFF;
    if (screen_rows < 3) screen_rows = 25;
    if (screen_cols < 10) screen_cols = 80;

    num_lines = 1;
    lines[0][0] = '\0';
    filepath[0] = '\0';
    cx = cy = 0;
    scroll_offset = 0;
    dirty = 0;

    if (get_argc() > 1) {
        char arg[256];
        get_argv(1, arg, sizeof(arg));
        load_file(arg);
    }

    tty_set_mode(1);
    clear_screen();
    draw_screen();

    while (1) {
        char c;
        if (read(0, &c, 1) <= 0)
            break;

        if (c == '\x1b') {
            char seq[3];
            if (read(0, &seq[0], 1) <= 0) break;
            if (seq[0] == '[') {
                if (read(0, &seq[1], 1) <= 0) break;
                if (seq[1] == 'A') {
                    if (cy > 0) cy--;
                    int len = strlen(lines[cy]);
                    if (cx > len) cx = len;
                } else if (seq[1] == 'B') {
                    if (cy < num_lines - 1) cy++;
                    int len = strlen(lines[cy]);
                    if (cx > len) cx = len;
                } else if (seq[1] == 'C') {
                    if (cx < strlen(lines[cy])) cx++;
                } else if (seq[1] == 'D') {
                    if (cx > 0) cx--;
                } else if (seq[1] == 'H') {
                    cx = 0;
                } else if (seq[1] == 'F') {
                    cx = strlen(lines[cy]);
                } else if (seq[1] == '3') {
                    char tilde;
                    if (read(0, &tilde, 1) > 0 && tilde == '~')
                        delete_char();
                }
            }
        } else if (c == 0x13) {
            // Ctrl+S
            save_file();
        } else if (c == 0x11) {
            // Ctrl+Q
            break;
        } else if (c == '\n' || c == '\r') {
            insert_newline();
        } else if (c == '\b' || c == 127) {
            backspace();
        } else if (c == '\t') {
            for (int i = 0; i < 4; i++)
                insert_char(' ');
        } else if (c >= 32) {
            insert_char(c);
        }

        ensure_visible();
        draw_screen();
    }

    tty_set_mode(0);
    clear_screen();
}
