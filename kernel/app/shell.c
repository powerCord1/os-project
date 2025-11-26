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
bool exit;

#define MAX_HISTORY 1024
static char *command_history[MAX_HISTORY];
static int history_count = 0;
static int history_index = 0;
static char current_input_buffer[512];
static bool is_recall_active = false;
static bool daylight_savings_enabled = false;

const cmd_list_t cmds[] = {{"clear", &cmd_clear},
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
                           {"rgbtest", &cmd_rgbtest}};

uint8_t cmd_count = sizeof(cmds) / sizeof(cmd_list_t);

void shell_main()
{
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

void cmd_history(int argc, char **argv)
{
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", i + 1, command_history[i]);
    }
}

void cmd_clear(int argc, char **argv)
{
    fb_clear();
}

void cmd_exit(int argc, char **argv)
{
    exit = true;
}

void cmd_panic(int argc, char **argv)
{
    panic("manually triggered panic");
}

void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            putchar(' ');
        }
    }
    putchar('\n');
}

void cmd_help(int argc, char **argv)
{
    printf("Available commands:\n");
    for (int i = 0; i < cmd_count; i++) {
        printf("- %s\n", cmds[i].name);
    }
}

void cmd_date(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--toggle-daylight-savings") == 0) {
        daylight_savings_enabled = !daylight_savings_enabled;
        printf("Daylight savings %s\n",
               daylight_savings_enabled ? "enabled" : "disabled");
        return;
    }

    datetime_t datetime;
    cmos_get_datetime(&datetime);

    if (daylight_savings_enabled) {
        datetime.hour = (datetime.hour + 1) % 24;
        // Note: This is a simplified implementation. A full implementation
        // would also handle date changes when the time crosses midnight.
    }

    printf("%02d/%02d/%04d %02d:%02d:%02d\n", datetime.day, datetime.month,
           datetime.year, datetime.hour, datetime.minute, datetime.second);
}

void cmd_shutdown(int argc, char **argv)
{
    shutdown();
}

void cmd_reboot(int argc, char **argv)
{
    reboot();
}

void cmd_sound_test(int argc, char **argv)
{
    printf("WHEEEE\n");
    pit_sound_test();
}

void cmd_sysinfo(int argc, char **argv)
{
    printf("System information:\n");

    uint64_t cr0, cr2, cr3, cr4, rflags;

    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    asm volatile("pushfq; popq %0" : "=r"(rflags));

    printf("CR0:    0x%016lx\n", cr0);
    printf("CR2:    0x%016lx\n", cr2);
    printf("CR3:    0x%016lx\n", cr3);
    printf("CR4:    0x%016lx\n", cr4);
    printf("RFLAGS: 0x%016lx\n", rflags);

    printf("APIC:   %s\n", is_apic_enabled() ? "enabled" : "disabled");
}

void cmd_fbtest(int argc, char **argv)
{
    fb_matrix_test();
}

void cmd_rgbtest(int argc, char **argv)
{
    fb_rgb_test();
}