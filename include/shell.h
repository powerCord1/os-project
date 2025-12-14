#pragma once

#include <fs.h>
#include <stdint.h>

typedef void (*cmd_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    cmd_func_t func;
} cmd_list_t;

#define MAX_HISTORY 1024

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
void cmd_meminfo(int argc, char **argv);
void cmd_fbtest(int argc, char **argv);
void cmd_rgbtest(int argc, char **argv);
void cmd_memtest(int argc, char **argv);
void cmd_lsblk(int argc, char **argv);
void cmd_mount(int argc, char **argv);
void cmd_umount(int argc, char **argv);
void cmd_ls(int argc, char **argv);
void cmd_cat(int argc, char **argv);
void cmd_write(int argc, char **argv);
void cmd_rm(int argc, char **argv);
void cmd_mkdir(int argc, char **argv);
void cmd_rmdir(int argc, char **argv);

extern const cmd_list_t cmds[];
extern uint8_t cmd_count;
extern bool exit;
extern char *command_history[MAX_HISTORY];
extern int history_count;
static fat32_fs_t *mounted_fs;

void shell_main();
void process_cmd(char *cmd);