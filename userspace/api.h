#pragma once

#define WASM_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

#define O_RDONLY 0x0001
#define O_WRONLY 0x0002
#define O_RDWR   0x0003
#define O_CREAT  0x0100
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* IO */
extern void print(const char *ptr, int len) WASM_IMPORT(print);
extern void putchar(int c) WASM_IMPORT(putchar);
extern unsigned long long get_ticks(void) WASM_IMPORT(get_ticks);
extern void exit(int code) WASM_IMPORT(exit);
extern int getchar(void) WASM_IMPORT(getchar);
extern int read_line(char *buf, int max_len) WASM_IMPORT(read_line);
extern int get_argc(void) WASM_IMPORT(get_argc);
extern int get_argv(int index, char *buf, int buf_len) WASM_IMPORT(get_argv);

/* Filesystem */
extern int open(const char *path, int path_len, int flags) WASM_IMPORT(open);
extern int close(int fd) WASM_IMPORT(close);
extern int read(int fd, char *buf, int count) WASM_IMPORT(read);
extern int write(int fd, const char *buf, int count) WASM_IMPORT(write);
extern int seek(int fd, int offset, int whence) WASM_IMPORT(seek);
extern int stat(const char *path, int path_len, void *stat_buf) WASM_IMPORT(stat);
extern int readdir(const char *path, int path_len, char *buf, int buf_len) WASM_IMPORT(readdir);
extern int mkdir(const char *path, int path_len) WASM_IMPORT(mkdir);
extern int unlink(const char *path, int path_len) WASM_IMPORT(unlink);
extern int rmdir(const char *path, int path_len) WASM_IMPORT(rmdir);

/* Process */
extern int spawn(const char *path, int path_len, const char *args,
                 int args_len, int argc) WASM_IMPORT(spawn);
extern int waitpid(int pid) WASM_IMPORT(waitpid);
extern int kill(int pid) WASM_IMPORT(kill);
extern int getpid(void) WASM_IMPORT(getpid);
extern int ptrace(int request, int pid, int addr, int data) WASM_IMPORT(ptrace);
extern int wait4(int pid, int *wstatus) WASM_IMPORT(wait4);

#define PTRACE_TRACEME    0
#define PTRACE_CONT       7
#define PTRACE_GETREGS   12
#define PTRACE_SYSCALL   24

#define PTRACE_MAX_ARGS   6

typedef struct {
    int syscall_nr;
    long long args[PTRACE_MAX_ARGS];
    long long ret;
    int at_entry;
} ptrace_info_t;

/* TTY */
extern int tty_set_mode(int mode) WASM_IMPORT(tty_set_mode);
extern int tty_get_size(void) WASM_IMPORT(tty_get_size);

/* Graphics */
extern void fb_info(void *buf) WASM_IMPORT(fb_info);
extern unsigned int fb_alloc(int w, int h) WASM_IMPORT(fb_alloc);
extern void fb_flush(unsigned int src, int dst_x, int dst_y, int w, int h, int src_pitch) WASM_IMPORT(fb_flush);
extern void fb_sync(void) WASM_IMPORT(fb_sync);

/* Pipes & Redirection */
extern int dup2(int oldfd, int newfd) WASM_IMPORT(dup2);
extern int pipe(int *fds) WASM_IMPORT(pipe);
extern int pipe_create(void) WASM_IMPORT(pipe_create);
extern int pipe_close_read(int pipe_id) WASM_IMPORT(pipe_close_read);
extern int pipe_close_write(int pipe_id) WASM_IMPORT(pipe_close_write);
extern int spawn_redirected(const char *path, int path_len, const char *args,
                            int args_len, int argc, const void *redir_buf,
                            int redir_len) WASM_IMPORT(spawn_redirected);

#define FD_SETUP_NONE        0
#define FD_SETUP_PIPE_READ   1
#define FD_SETUP_PIPE_WRITE  2
#define FD_SETUP_FILE_READ   3
#define FD_SETUP_FILE_WRITE  4
#define FD_SETUP_FILE_APPEND 5

/* Helpers */

static void *memcpy(void *dst, const void *src, int n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (int i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

static void *memset(void *dst, int c, int n)
{
    char *d = (char *)dst;
    for (int i = 0; i < n; i++)
        d[i] = (char)c;
    return dst;
}

static void *memmove(void *dst, const void *src, int n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    if (d < s) {
        for (int i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (int i = n - 1; i >= 0; i--)
            d[i] = s[i];
    }
    return dst;
}

static int strlen(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    return len;
}

static void puts(const char *s)
{
    print(s, strlen(s));
}

static void print_num(int n)
{
    char buf[12];
    int i = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }
    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    if (neg)
        buf[i++] = '-';

    char rev[12];
    for (int j = 0; j < i; j++)
        rev[j] = buf[i - 1 - j];

    print(rev, i);
}

static int open_path(const char *path, int flags)
{
    return open(path, strlen(path), flags);
}

static int stat_path(const char *path, void *stat_buf)
{
    return stat(path, strlen(path), stat_buf);
}

static int readdir_path(const char *path, char *buf, int buf_len)
{
    return readdir(path, strlen(path), buf, buf_len);
}

static int mkdir_path(const char *path)
{
    return mkdir(path, strlen(path));
}

static int unlink_path(const char *path)
{
    return unlink(path, strlen(path));
}

static int rmdir_path(const char *path)
{
    return rmdir(path, strlen(path));
}

static int spawn_cmd(const char *path, char **argv, int argc)
{
    char args_buf[512];
    int pos = 0;
    for (int i = 0; i < argc; i++) {
        int len = strlen(argv[i]);
        if (pos + len + 1 > (int)sizeof(args_buf))
            break;
        for (int j = 0; j < len; j++)
            args_buf[pos++] = argv[i][j];
        args_buf[pos++] = '\0';
    }
    return spawn(path, strlen(path), args_buf, pos, argc);
}

static int spawn_cmd_redirected(const char *path, char **argv, int argc,
                                const void *redir_buf, int redir_len)
{
    char args_buf[512];
    int pos = 0;
    for (int i = 0; i < argc; i++) {
        int len = strlen(argv[i]);
        if (pos + len + 1 > (int)sizeof(args_buf))
            break;
        for (int j = 0; j < len; j++)
            args_buf[pos++] = argv[i][j];
        args_buf[pos++] = '\0';
    }
    return spawn_redirected(path, strlen(path), args_buf, pos, argc,
                            redir_buf, redir_len);
}
