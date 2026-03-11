#include "api.h"

static const char *syscall_name(int nr)
{
    switch (nr) {
    case 0:   return "read";
    case 1:   return "write";
    case 2:   return "open";
    case 3:   return "close";
    case 4:   return "stat";
    case 5:   return "fstat";
    case 6:   return "lstat";
    case 7:   return "poll";
    case 8:   return "lseek";
    case 9:   return "mmap";
    case 10:  return "mprotect";
    case 11:  return "munmap";
    case 12:  return "brk";
    case 13:  return "rt_sigaction";
    case 14:  return "rt_sigprocmask";
    case 16:  return "ioctl";
    case 17:  return "pread64";
    case 18:  return "pwrite64";
    case 19:  return "readv";
    case 20:  return "writev";
    case 21:  return "access";
    case 22:  return "pipe";
    case 24:  return "sched_yield";
    case 25:  return "mremap";
    case 28:  return "madvise";
    case 32:  return "dup";
    case 33:  return "dup2";
    case 35:  return "nanosleep";
    case 39:  return "getpid";
    case 41:  return "socket";
    case 42:  return "connect";
    case 46:  return "sendmsg";
    case 59:  return "execve";
    case 60:  return "exit";
    case 61:  return "wait4";
    case 62:  return "kill";
    case 63:  return "uname";
    case 72:  return "fcntl";
    case 73:  return "flock";
    case 74:  return "fsync";
    case 75:  return "fdatasync";
    case 77:  return "ftruncate";
    case 79:  return "getcwd";
    case 80:  return "chdir";
    case 81:  return "fchdir";
    case 82:  return "rename";
    case 83:  return "mkdir";
    case 84:  return "rmdir";
    case 87:  return "unlink";
    case 88:  return "symlink";
    case 89:  return "readlink";
    case 90:  return "chmod";
    case 91:  return "fchmod";
    case 92:  return "chown";
    case 93:  return "fchown";
    case 95:  return "umask";
    case 96:  return "gettimeofday";
    case 97:  return "getrlimit";
    case 98:  return "getrusage";
    case 99:  return "sysinfo";
    case 101: return "ptrace";
    case 102: return "getuid";
    case 104: return "getgid";
    case 105: return "setuid";
    case 106: return "setgid";
    case 107: return "geteuid";
    case 108: return "getegid";
    case 109: return "setpgid";
    case 110: return "getppid";
    case 112: return "setsid";
    case 121: return "getpgid";
    case 131: return "sigaltstack";
    case 144: return "sched_setscheduler";
    case 157: return "prctl";
    case 186: return "gettid";
    case 200: return "tkill";
    case 202: return "futex";
    case 204: return "sched_getaffinity";
    case 217: return "getdents64";
    case 218: return "set_tid_address";
    case 221: return "fadvise64";
    case 228: return "clock_gettime";
    case 229: return "clock_getres";
    case 230: return "clock_nanosleep";
    case 231: return "exit_group";
    case 235: return "utimes";
    case 257: return "openat";
    case 258: return "mkdirat";
    case 261: return "futimesat";
    case 262: return "newfstatat";
    case 263: return "unlinkat";
    case 269: return "faccessat";
    case 271: return "ppoll";
    case 273: return "set_robust_list";
    case 280: return "utimensat";
    case 292: return "dup3";
    case 293: return "pipe2";
    case 302: return "prlimit64";
    case 316: return "renameat2";
    case 318: return "getrandom";
    case 332: return "statx";
    case 439: return "faccessat2";
    default:  return 0;
    }
}

static void print_hex(int val)
{
    char buf[12];
    int i = 0;
    unsigned int v = (unsigned int)val;

    if (v == 0) {
        puts("0x0");
        return;
    }

    buf[i++] = '0';
    buf[i++] = 'x';

    char tmp[8];
    int n = 0;
    while (v > 0) {
        int d = v & 0xf;
        tmp[n++] = d < 10 ? '0' + d : 'a' + d - 10;
        v >>= 4;
    }
    for (int j = n - 1; j >= 0; j--)
        buf[i++] = tmp[j];

    print(buf, i);
}

static void print_ll(long long val)
{
    if (val < 0) {
        putchar('-');
        val = -val;
    }
    print_num((int)val);
}

static void print_syscall_entry(ptrace_info_t *info)
{
    const char *name = syscall_name(info->syscall_nr);
    if (name)
        puts(name);
    else {
        puts("syscall_");
        print_num(info->syscall_nr);
    }

    putchar('(');
    for (int i = 0; i < PTRACE_MAX_ARGS; i++) {
        if (i > 0)
            puts(", ");
        print_hex((int)info->args[i]);
    }
}

static void print_syscall_exit(ptrace_info_t *info)
{
    puts(") = ");
    print_ll(info->ret);
    putchar('\n');
}

void _start(void)
{
    int argc = get_argc();
    if (argc < 2) {
        puts("usage: strace <program> [args...]\n");
        exit(1);
    }

    char prog[256];
    get_argv(1, prog, sizeof(prog));

    char *child_argv[16];
    char argv_bufs[16][256];
    int child_argc = argc - 1;
    if (child_argc > 16)
        child_argc = 16;

    for (int i = 0; i < child_argc; i++) {
        get_argv(i + 1, argv_bufs[i], 256);
        child_argv[i] = argv_bufs[i];
    }

    int child_pid = spawn_cmd(prog, child_argv, child_argc);
    if (child_pid < 0) {
        puts("strace: failed to spawn ");
        puts(prog);
        putchar('\n');
        exit(1);
    }

    ptrace(PTRACE_SYSCALL, child_pid, 0, 0);

    int wstatus;
    ptrace_info_t info;

    while (1) {
        int ret = wait4(child_pid, &wstatus);
        if (ret < 0)
            break;

        /* check if child exited: low 7 bits != 0x7f means not stopped */
        if ((wstatus & 0x7f) != 0x7f) {
            puts("+++ exited with ");
            print_num((wstatus >> 8) & 0xff);
            puts(" +++\n");
            break;
        }

        ptrace(PTRACE_GETREGS, child_pid, 0, (int)&info);

        if (info.at_entry)
            print_syscall_entry(&info);
        else
            print_syscall_exit(&info);

        ptrace(PTRACE_SYSCALL, child_pid, 0, 0);
    }
}
