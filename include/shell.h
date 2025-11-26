#include <stdint.h>

typedef void (*cmd_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    cmd_func_t func;
} cmd_list_t;

void shell_main();
void process_cmd(char *cmd);

void cmd_clear(int argc, char **argv);
void cmd_exit(int argc, char **argv);
void cmd_shutdown(int argc, char **argv);
void cmd_reboot(int argc, char **argv);
void cmd_panic(int argc, char **argv);
void cmd_echo(int argc, char **argv);
void cmd_help(int argc, char **argv);
void cmd_date(int argc, char **argv);
void cmd_sound_test(int argc, char **argv);
void cmd_history(int argc, char **argv);
void cmd_sysinfo(int argc, char **argv);
void cmd_fbtest(int argc, char **argv);
void cmd_rgbtest(int argc, char **argv);