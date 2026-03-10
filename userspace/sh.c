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

void _start(void)
{
    char line[256];

    while (1) {
        puts("$ ");
        int len = read_line(line, sizeof(line));
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
            int code = waitpid(pid);
            if (code != 0 && code != 130) {
                puts("exit code: ");
                print_num(code);
                puts("\n");
            }
            continue;
        }

        // Create pipes between stages
        int pipes[7]; // pipe_ids
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

        // Close all pipe refs held by the shell
        for (int i = 0; i < num_stages - 1; i++) {
            pipe_close_read(pipes[i]);
            pipe_close_write(pipes[i]);
        }

        // Wait for all children
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
