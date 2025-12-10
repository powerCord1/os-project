#include <stdbool.h>
#include <string.h>

#include <app.h>
#include <cmos.h>
#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <heap.h>
#include <keyboard.h>
#include <panic.h>
#include <pit.h>
#include <power.h>
#include <shell.h>
#include <stdio.h>

const char *shell_prompt = "> ";
char input_buffer[512];

const cmd_list_t cmds[] = {
    {"clear", &cmd_clear},
    {"exit", &cmd_exit},
    {"poweroff", &cmd_shutdown},
    {"reboot", &cmd_reboot},
    {"panic", &cmd_panic},
    {"echo", &cmd_echo},
    {"help", &cmd_help},
    {"date", &cmd_date},
    {"soundtest", &cmd_sound_test},
    {"history", &cmd_history},
    {"sysinfo", &cmd_sysinfo},
    {"fbtest", &cmd_fbtest},
    {"rgbtest", &cmd_rgbtest},
    {"memtest", &cmd_memtest},
    {"lsblk", &cmd_lsblk},
    {"mount", &cmd_mount},
    {"umount", &cmd_umount},
    {"ls", &cmd_ls},
    {"cat", &cmd_cat},
    {"writefile", &cmd_write_file},
};
uint8_t cmd_count;
bool exit;
char *command_history[MAX_HISTORY];
int history_count = 0;

static char current_input_buffer[512];
static bool is_recall_active = false;

void shell_main()
{
    static int history_index = 0;
    cmd_count = sizeof(cmds) / sizeof(cmd_list_t);

    exit = false;
    uint16_t i = 0;
    key_t key;
    while (!exit) {
        printf("%s", shell_prompt);
        i = 0;
        memset(input_buffer, 0, sizeof(input_buffer));
        while (key.scancode != KEY_ENTER) {
            key = kbd_get_key(true);
            if (key.scancode == KEY_ENTER) {
                putchar('\n');
                is_recall_active = false;
                memset(current_input_buffer, 0, sizeof(current_input_buffer));
                break;
            } else if (key.scancode == KEY_BACKSPACE) {
                if (i > 0) {
                    i--;
                    input_buffer[i] = '\0';
                    putchar('\b');
                }
                continue;
            } else if (key.scancode == KEY_DELETE) {
                fb_delete();
                continue;
            } else if (key.key == '\t') {
                for (int j = 0; j < INDENT_WIDTH; j++) {
                    if (i < (sizeof(input_buffer) - 1)) {
                        input_buffer[i++] = ' ';
                        putchar(' ');
                    } else {
                        break;
                    }
                }
                continue;
            } else if (key.scancode == KEY_ARROW_UP) {
                if (history_count == 0) {
                    continue;
                }

                if (!is_recall_active) {
                    strcpy(current_input_buffer, input_buffer);
                    is_recall_active = true;
                }

                if (history_index > 0) {
                    history_index--;
                    for (int j = 0; j < i; j++) {
                        putchar('\b');
                    }
                    memset(input_buffer, 0, sizeof(input_buffer));
                    strcpy(input_buffer, command_history[history_index]);
                    i = strlen(input_buffer);
                    printf("%s", input_buffer);
                }
                continue;
            } else if (key.scancode == KEY_ARROW_DOWN) {
                if (!is_recall_active) {
                    continue;
                }

                if (history_index < history_count) {
                    history_index++;
                    for (int j = 0; j < i; j++) {
                        putchar('\b');
                    }
                    memset(input_buffer, 0, sizeof(input_buffer));

                    if (history_index == history_count) {
                        strcpy(input_buffer, current_input_buffer);
                        is_recall_active = false;
                    } else {
                        strcpy(input_buffer, command_history[history_index]);
                    }
                    i = strlen(input_buffer);
                    printf("%s", input_buffer);
                }
                continue;
            } else if (kbd_modifiers.ctrl && key.scancode == KEY_C) {
                printf("^C\n");
                i = 0;
                memset(input_buffer, 0, sizeof(input_buffer));
                key.scancode = KEY_ENTER; // break out of the inner loop
                continue;
            }

            if (i < (sizeof(input_buffer) - 1) && key.key != 0 &&
                key.key != '\n') {
                input_buffer[i++] = key.key;
                putchar(key.key);
            }
        }
        if (i > 0) {
            if (history_count < MAX_HISTORY) {
                command_history[history_count] =
                    malloc(strlen(input_buffer) + 1);
                strcpy(command_history[history_count], input_buffer);
                history_count++;
            } else {
                free(command_history[0]);
                for (int j = 0; j < MAX_HISTORY - 1; j++) {
                    command_history[j] = command_history[j + 1];
                }
                command_history[MAX_HISTORY - 1] =
                    malloc(strlen(input_buffer) + 1);
                strcpy(command_history[MAX_HISTORY - 1], input_buffer);
            }
            history_index = history_count;
            is_recall_active = false;
            memset(current_input_buffer, 0, sizeof(current_input_buffer));
            process_cmd(input_buffer);
        }
        key.key = 0;
        key.scancode = 0;
    }

    for (int i = 0; i < history_count; i++) {
        free(command_history[i]);
    }
}

void process_cmd(char *cmd)
{
    char *argv[16];
    int argc = 0;
    char *token = strtok(cmd, " ");
    while (token != NULL && argc < 15) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;

    if (argc == 0) {
        return;
    }

    for (uint8_t i = 0; i < cmd_count; i++) {
        if (strcmp(argv[0], cmds[i].name) == 0) {
            cmds[i].func(argc, argv);
            return;
        }
    }

    printf("Command not found: %s\n", argv[0]);
}