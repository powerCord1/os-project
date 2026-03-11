#pragma once

#include <stdint.h>

/* Linux x86-64 syscall numbers */
#define SYS_READ              0
#define SYS_WRITE             1
#define SYS_OPEN              2
#define SYS_CLOSE             3
#define SYS_STAT              4
#define SYS_FSTAT             5
#define SYS_LSTAT             6
#define SYS_POLL              7
#define SYS_LSEEK             8
#define SYS_MMAP              9
#define SYS_MPROTECT         10
#define SYS_MUNMAP           11
#define SYS_BRK              12
#define SYS_RT_SIGACTION     13
#define SYS_RT_SIGPROCMASK   14
#define SYS_IOCTL            16
#define SYS_PREAD64          17
#define SYS_PWRITE64         18
#define SYS_READV            19
#define SYS_WRITEV           20
#define SYS_ACCESS           21
#define SYS_PIPE             22
#define SYS_SCHED_YIELD      24
#define SYS_MADVISE          28
#define SYS_DUP              32
#define SYS_DUP2             33
#define SYS_NANOSLEEP        35
#define SYS_GETPID           39
#define SYS_SOCKET           41
#define SYS_CONNECT          42
#define SYS_SENDMSG          46
#define SYS_EXIT             60
#define SYS_KILL             62
#define SYS_UNAME            63
#define SYS_FCNTL            72
#define SYS_FLOCK            73
#define SYS_FSYNC            74
#define SYS_FDATASYNC        75
#define SYS_FTRUNCATE        77
#define SYS_GETCWD           79
#define SYS_CHDIR            80
#define SYS_RENAME           82
#define SYS_MKDIR            83
#define SYS_RMDIR            84
#define SYS_UNLINK           87
#define SYS_SYMLINK          88
#define SYS_READLINK         89
#define SYS_CHMOD            90
#define SYS_FCHMOD           91
#define SYS_CHOWN            92
#define SYS_FCHOWN           93
#define SYS_UMASK            95
#define SYS_GETTIMEOFDAY     96
#define SYS_GETRLIMIT        97
#define SYS_GETRUSAGE        98
#define SYS_SYSINFO          99
#define SYS_GETUID          102
#define SYS_GETGID          104
#define SYS_SETUID          105
#define SYS_SETGID          106
#define SYS_GETEUID         107
#define SYS_GETEGID         108
#define SYS_SETPGID         109
#define SYS_GETPPID         110
#define SYS_SETSID          112
#define SYS_GETPGID         121
#define SYS_SIGALTSTACK     131
#define SYS_SCHED_SETSCHEDULER 144
#define SYS_PRCTL           157
#define SYS_GETTID          186
#define SYS_FUTEX           202
#define SYS_SCHED_GETAFFINITY 204
#define SYS_GETDENTS64      217
#define SYS_SET_TID_ADDRESS 218
#define SYS_FADVISE         221
#define SYS_CLOCK_GETTIME   228
#define SYS_CLOCK_GETRES    229
#define SYS_CLOCK_NANOSLEEP 230
#define SYS_EXIT_GROUP      231
#define SYS_UTIMES          235
#define SYS_OPENAT          257
#define SYS_MKDIRAT         258
#define SYS_FUTIMESAT       261
#define SYS_NEWFSTATAT      262
#define SYS_UNLINKAT        263
#define SYS_RENAMEAT2       316
#define SYS_GETRANDOM       318
#define SYS_FACCESSAT       269
#define SYS_SET_ROBUST_LIST 273
#define SYS_UTIMENSAT       280
#define SYS_DUP3            292
#define SYS_PIPE2           293
#define SYS_PRLIMIT64       302
#define SYS_PPOLL           271
#define SYS_STATX           332
#define SYS_MREMAP          25
#define SYS_WAIT4           61
#define SYS_EXECVE          59
#define SYS_FCHDIR          81
#define SYS_TKILL           200
#define SYS_PTRACE          101
#define SYS_FACCESSAT2      439

/* Linux errno codes */
#define L_EPERM          1
#define L_ENOENT         2
#define L_ESRCH          3
#define L_EINTR          4
#define L_EIO            5
#define L_ENXIO          6
#define L_EBADF          9
#define L_EAGAIN        11
#define L_ENOMEM        12
#define L_EACCES        13
#define L_EFAULT        14
#define L_EEXIST        17
#define L_ENODEV        19
#define L_ENOTDIR       20
#define L_EISDIR        21
#define L_EINVAL        22
#define L_ENFILE        23
#define L_EMFILE        24
#define L_ENOTTY        25
#define L_ENOSPC        28
#define L_ENOSYS        38
#define L_ENOTEMPTY     39
#define L_ERANGE        34
#define L_ELOOP         40
#define L_ENAMETOOLONG  36
#define L_ESPIPE        29

/* Linux open flags */
#define L_O_RDONLY     0x0000
#define L_O_WRONLY     0x0001
#define L_O_RDWR       0x0002
#define L_O_CREAT      0x0040
#define L_O_EXCL       0x0080
#define L_O_TRUNC      0x0200
#define L_O_APPEND     0x0400
#define L_O_NONBLOCK   0x0800
#define L_O_DIRECTORY  0x10000
#define L_O_CLOEXEC    0x80000

/* AT constants */
#define L_AT_FDCWD       (-100)
#define L_AT_REMOVEDIR   0x200

/* File type and mode bits */
#define L_S_IFMT    0170000
#define L_S_IFREG   0100000
#define L_S_IFDIR   0040000
#define L_S_IFCHR   0020000
#define L_S_IFIFO   0010000

/* fcntl commands */
#define L_F_DUPFD    0
#define L_F_GETFD    1
#define L_F_SETFD    2
#define L_F_GETFL    3
#define L_F_SETFL    4

/* ioctl requests */
#define L_TCGETS     0x5401
#define L_TCSETS     0x5402
#define L_TCSETSW    0x5403
#define L_TCSETSF    0x5404
#define L_TIOCGWINSZ 0x5413
#define L_FIONREAD   0x541B

/* termios flags */
#define L_IGNBRK   0x0001
#define L_BRKINT   0x0002
#define L_ICRNL    0x0100
#define L_IXON     0x0400

#define L_OPOST    0x0001
#define L_ONLCR    0x0004

#define L_CS8      0x0030
#define L_CREAD    0x0080
#define L_B38400   0x000F

#define L_ISIG     0x0001
#define L_ICANON   0x0002
#define L_ECHO     0x0008
#define L_ECHOE    0x0010
#define L_ECHOK    0x0020
#define L_IEXTEN   0x8000

/* clock IDs */
#define L_CLOCK_REALTIME  0
#define L_CLOCK_MONOTONIC 1

/* poll events */
#define L_POLLIN   0x0001
#define L_POLLOUT  0x0004
#define L_POLLERR  0x0008
#define L_POLLHUP  0x0010
#define L_POLLNVAL 0x0020

/* resource limits */
#define L_RLIMIT_NOFILE 7
#define L_RLIMIT_STACK  3

/* d_type values */
#define L_DT_UNKNOWN 0
#define L_DT_REG     8
#define L_DT_DIR     4

/* MAP flags */
#define L_MAP_ANONYMOUS 0x20
#define L_MAP_PRIVATE   0x02

/* signal constants */
#define L_NSIG 64

/*
 * Linux x86-64 struct stat layout (144 bytes)
 * Matches wali.wit: dev, ino, nlink, mode, uid, gid, pad, rdev, size,
 *                   blksize, blocks, atim, mtim, ctim, unused[3]
 */
typedef struct {
    uint64_t st_dev;       /* 0 */
    uint64_t st_ino;       /* 8 */
    uint64_t st_nlink;     /* 16 */
    uint32_t st_mode;      /* 24 */
    uint32_t st_uid;       /* 28 */
    uint32_t st_gid;       /* 32 */
    uint32_t pad0;         /* 36 */
    uint64_t st_rdev;      /* 40 */
    int64_t  st_size;      /* 48 */
    int64_t  st_blksize;   /* 56 */
    int64_t  st_blocks;    /* 64 */
    int64_t  st_atim_sec;  /* 72 */
    int64_t  st_atim_nsec; /* 80 */
    int64_t  st_mtim_sec;  /* 88 */
    int64_t  st_mtim_nsec; /* 96 */
    int64_t  st_ctim_sec;  /* 104 */
    int64_t  st_ctim_nsec; /* 112 */
    int64_t  unused[3];    /* 120-143 */
} linux_stat_t;            /* 144 bytes total */

/* struct linux_dirent64 (variable-size) */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
} __attribute__((packed)) linux_dirent64_t;

/* struct iovec (wasm32: ptr + u32) */
typedef struct {
    uint32_t iov_base;
    uint32_t iov_len;
} wasm_iovec_t;

/* struct pollfd */
typedef struct {
    int32_t  fd;
    int16_t  events;
    int16_t  revents;
} linux_pollfd_t;

/* Linux termios (60 bytes) */
typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
} linux_termios_t;

/* struct winsize */
typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} linux_winsize_t;

/* struct timespec (wasm: two i64) */
typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} linux_timespec_t;

/* struct timeval */
typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} linux_timeval_t;

/* struct utsname (6 x 65 bytes) */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} linux_utsname_t;

/* struct rlimit */
typedef struct {
    uint64_t rlim_cur;
    uint64_t rlim_max;
} linux_rlimit_t;
