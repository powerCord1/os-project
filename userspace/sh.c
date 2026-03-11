#include "api.h"

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

static void build_path(const char *cmd, char *path)
{
    path[0] = '/';
    int pi = 1;
    for (int i = 0; cmd[i] && pi < 60; i++)
        path[pi++] = to_upper(cmd[i]);
    path[pi++] = '.';
    path[pi++] = 'W';
    path[pi++] = 'M';
    path[pi] = '\0';
}

typedef struct {
    char *argv[16];
    int argc;
    char *stdin_file;
    char *stdout_file;
    char *stderr_file;
    int stdout_append;
} stage_t;

static void parse_redirections(stage_t *s)
{
    int out = 0;
    for (int i = 0; i < s->argc; i++) {
        if (strcmp(s->argv[i], "<") == 0 && i + 1 < s->argc) {
            s->stdin_file = s->argv[++i];
        } else if (strcmp(s->argv[i], ">>") == 0 && i + 1 < s->argc) {
            s->stdout_file = s->argv[++i];
            s->stdout_append = 1;
        } else if (strcmp(s->argv[i], ">") == 0 && i + 1 < s->argc) {
            s->stdout_file = s->argv[++i];
            s->stdout_append = 0;
        } else if (strcmp(s->argv[i], "2>") == 0 && i + 1 < s->argc) {
            s->stderr_file = s->argv[++i];
        } else {
            s->argv[out++] = s->argv[i];
        }
    }
    s->argc = out;
}

static int build_redir_buf(stage_t *s, int pipe_in, int pipe_out,
                           char *buf)
{
    int pos = 0;
    int *ip;

    if (pipe_in >= 0) {
        ip = (int *)(buf + pos);
        ip[0] = 0;
        ip[1] = FD_SETUP_PIPE_READ;
        ip[2] = pipe_in;
        pos += 12;
    }
    if (pipe_out >= 0) {
        ip = (int *)(buf + pos);
        ip[0] = 1;
        ip[1] = FD_SETUP_PIPE_WRITE;
        ip[2] = pipe_out;
        pos += 12;
    }
    if (s->stdin_file) {
        ip = (int *)(buf + pos);
        ip[0] = 0;
        ip[1] = FD_SETUP_FILE_READ;
        int plen = strlen(s->stdin_file);
        ip[2] = plen;
        pos += 12;
        for (int j = 0; j < plen; j++)
            buf[pos++] = s->stdin_file[j];
    }
    if (s->stdout_file) {
        ip = (int *)(buf + pos);
        ip[0] = 1;
        ip[1] = s->stdout_append ? FD_SETUP_FILE_APPEND : FD_SETUP_FILE_WRITE;
        int plen = strlen(s->stdout_file);
        ip[2] = plen;
        pos += 12;
        for (int j = 0; j < plen; j++)
            buf[pos++] = s->stdout_file[j];
    }
    if (s->stderr_file) {
        ip = (int *)(buf + pos);
        ip[0] = 2;
        ip[1] = FD_SETUP_FILE_WRITE;
        int plen = strlen(s->stderr_file);
        ip[2] = plen;
        pos += 12;
        for (int j = 0; j < plen; j++)
            buf[pos++] = s->stderr_file[j];
    }
    return pos;
}

static int spawn_stage(stage_t *s, int pipe_in, int pipe_out)
{
    char path[64];
    build_path(s->argv[0], path);

    char redir_buf[256];
    int rlen = build_redir_buf(s, pipe_in, pipe_out, redir_buf);

    if (rlen > 0)
        return spawn_cmd_redirected(path, s->argv, s->argc,
                                    redir_buf, rlen);
    else
        return spawn_cmd(path, s->argv, s->argc);
}

static int read_line_raw(char *line, int max_len)
{
    tty_set_mode(1);
    int len = 0;
    int pos = 0;
    char c;

    while (1) {
        int r = read(0, &c, 1);
        if (r <= 0) {
            tty_set_mode(0);
            return -1;
        }

        if (c == '\x1b') {
            char seq[3];
            if (read(0, &seq[0], 1) <= 0) break;
            if (seq[0] == '[') {
                if (read(0, &seq[1], 1) <= 0) break;
                if (seq[1] == 'C') {
                    // right arrow
                    if (pos < len) {
                        pos++;
                        puts("\x1b[C");
                    }
                    continue;
                } else if (seq[1] == 'D') {
                    // left arrow
                    if (pos > 0) {
                        pos--;
                        puts("\x1b[D");
                    }
                    continue;
                } else if (seq[1] == 'H') {
                    // home
                    while (pos > 0) {
                        puts("\x1b[D");
                        pos--;
                    }
                    continue;
                } else if (seq[1] == 'F') {
                    // end
                    while (pos < len) {
                        puts("\x1b[C");
                        pos++;
                    }
                    continue;
                } else if (seq[1] == '3') {
                    char tilde;
                    if (read(0, &tilde, 1) <= 0) break;
                    if (tilde == '~' && pos < len) {
                        // delete key
                        memmove(line + pos, line + pos + 1, len - pos - 1);
                        len--;
                        // redraw from cursor to end, clear last char
                        print(line + pos, len - pos);
                        putchar(' ');
                        // move cursor back
                        for (int i = 0; i <= len - pos; i++)
                            puts("\x1b[D");
                    }
                    continue;
                }
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            putchar('\n');
            tty_set_mode(0);
            line[len] = '\0';
            return len;
        }

        if (c == '\x03') {
            // Ctrl+C: cancel line
            putchar('\n');
            tty_set_mode(0);
            line[0] = '\0';
            return 0;
        }

        if (c == '\x04') {
            // Ctrl+D: EOF
            if (len == 0) {
                tty_set_mode(0);
                return -1;
            }
            continue;
        }

        if (c == '\b' || c == 127) {
            if (pos > 0) {
                memmove(line + pos - 1, line + pos, len - pos);
                pos--;
                len--;
                puts("\x1b[D");
                print(line + pos, len - pos);
                putchar(' ');
                for (int i = 0; i <= len - pos; i++)
                    puts("\x1b[D");
            }
            continue;
        }

        if (c < 32)
            continue;

        if (len >= max_len - 1)
            continue;

        if (pos < len)
            memmove(line + pos + 1, line + pos, len - pos);
        line[pos] = c;
        len++;
        print(line + pos, len - pos);
        pos++;
        // move cursor back to pos
        for (int i = 0; i < len - pos; i++)
            puts("\x1b[D");
    }

    tty_set_mode(0);
    line[len] = '\0';
    return len;
}

static void int_to_str(int n, char *buf)
{
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[12];
    int i = 0;
    while (n > 0) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = 0; j < i; j++)
        buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

static int contains(const char *hay, const char *needle)
{
    int nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        int match = 1;
        for (int i = 0; i < nlen; i++) {
            if (p[i] != needle[i]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 1;
    }
    return 0;
}

static int parse_field(const char *buf, const char *key)
{
    int klen = strlen(key);
    for (const char *p = buf; *p; p++) {
        int match = 1;
        for (int i = 0; i < klen; i++) {
            if (p[i] != key[i]) {
                match = 0;
                break;
            }
        }
        if (match) {
            p += klen;
            int val = 0;
            while (*p >= '0' && *p <= '9')
                val = val * 10 + (*p++ - '0');
            return val;
        }
    }
    return 0;
}

static void show_loading(int pid)
{
    char proc_path[32];
    char pid_str[12];
    int_to_str(pid, pid_str);

    int pi = 0;
    const char *prefix = "/proc/";
    for (int i = 0; prefix[i]; i++)
        proc_path[pi++] = prefix[i];
    for (int i = 0; pid_str[i]; i++)
        proc_path[pi++] = pid_str[i];
    const char *suffix = "/status";
    for (int i = 0; suffix[i]; i++)
        proc_path[pi++] = suffix[i];
    proc_path[pi] = '\0';

    int bar_shown = 0;
    unsigned long long start = get_ticks();

    while (1) {
        int fd = open_path(proc_path, O_RDONLY);
        if (fd < 0)
            break;

        char buf[256];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0)
            break;
        buf[n] = '\0';

        if (parse_field(buf, "LoadDone:\t") || contains(buf, "State:\tZ"))
            break;

        if (get_ticks() - start < 50)
            continue;

        int bytes_read = parse_field(buf, "LoadRead:\t");
        int bytes_total = parse_field(buf, "LoadTotal:\t");
        int heap_kb = parse_field(buf, "HeapUsed:\t") / 1024;

        puts("\r\x1b[K");
        if (bytes_total > 0 && bytes_read < bytes_total) {
            int pct = bytes_read * 100 / bytes_total;
            int filled = pct / 5;
            puts("disk [");
            for (int i = 0; i < 20; i++)
                putchar(i < filled ? '=' : ' ');
            puts("] ");
            print_num(pct);
            puts("% ");
            print_num(bytes_read / 1024);
            puts("/");
            print_num(bytes_total / 1024);
            puts("KB");
        } else {
            puts("init ");
            print_num(heap_kb);
            puts("KB");
        }
        bar_shown = 1;

        unsigned long long target = get_ticks() + 100;
        while (get_ticks() < target)
            ;
    }

    if (bar_shown)
        puts("\r\x1b[K");
}

void _start(void)
{
    char line[256];

    while (1) {
        puts("$ ");
        int len = read_line_raw(line, sizeof(line));
        if (len < 0)
            break;
        if (len == 0)
            continue;

        if (strcmp(line, "q") == 0)
            break;

        char *stages_raw[8];
        int num_stages = 0;
        char *sp = line;
        stages_raw[num_stages++] = sp;
        while (*sp) {
            if (*sp == '|') {
                *sp = '\0';
                sp++;
                while (*sp == ' ') sp++;
                if (*sp && num_stages < 8)
                    stages_raw[num_stages++] = sp;
            } else {
                sp++;
            }
        }

        stage_t stages[8];
        for (int i = 0; i < num_stages; i++) {
            stages[i].argc = 0;
            stages[i].stdin_file = 0;
            stages[i].stdout_file = 0;
            stages[i].stderr_file = 0;
            stages[i].stdout_append = 0;

            char *p = stages_raw[i];
            while (*p && stages[i].argc < 16) {
                while (*p == ' ') p++;
                if (!*p) break;
                stages[i].argv[stages[i].argc++] = p;
                while (*p && *p != ' ') p++;
                if (*p) *p++ = '\0';
            }
            parse_redirections(&stages[i]);
        }

        if (stages[0].argc == 0)
            continue;

        if (num_stages == 1) {
            int pid = spawn_stage(&stages[0], -1, -1);
            if (pid < 0) {
                puts(stages[0].argv[0]);
                puts(": not found\n");
                continue;
            }
            show_loading(pid);
            int code = waitpid(pid);
            if (code != 0 && code != 130) {
                puts("exit code: ");
                print_num(code);
                puts("\n");
            }
            continue;
        }

        int pipes[7];
        for (int i = 0; i < num_stages - 1; i++) {
            pipes[i] = pipe_create();
            if (pipes[i] < 0) {
                puts("pipe failed\n");
                for (int j = 0; j < i; j++) {
                    pipe_close_read(pipes[j]);
                    pipe_close_write(pipes[j]);
                }
                goto next_cmd;
            }
        }

        int pids[8];
        int pid_count = 0;
        for (int i = 0; i < num_stages; i++) {
            int pin = (i > 0) ? pipes[i - 1] : -1;
            int pout = (i < num_stages - 1) ? pipes[i] : -1;
            pids[pid_count] = spawn_stage(&stages[i], pin, pout);
            if (pids[pid_count] < 0) {
                puts(stages[i].argv[0]);
                puts(": not found\n");
            } else {
                pid_count++;
            }
        }

        for (int i = 0; i < num_stages - 1; i++) {
            pipe_close_read(pipes[i]);
            pipe_close_write(pipes[i]);
        }

        int last_code = 0;
        for (int i = 0; i < pid_count; i++)
            last_code = waitpid(pids[i]);

        if (last_code != 0 && last_code != 130) {
            puts("exit code: ");
            print_num(last_code);
            puts("\n");
        }
next_cmd:
        (void)0;
    }
}
