#include <stdbool.h>
#include <string.h>

#include <app.h>
#include <cmos.h>
#include <debug.h>
#include <keyboard.h>
#include <pit.h>
#include <power.h>
#include <shell.h>
#include <stdio.h>
#include <tty.h>

const char *shell_prompt = "> ";
char input_buffer[256];
bool exit;

const cmd_list_t cmds[] = {{"clear", &cmd_clear},         {"exit", &cmd_exit},
                           {"poweroff", &shutdown},       {"reboot", &reboot},
                           {"panic", &cmd_panic},         {"echo", &cmd_echo},
                           {"help", &cmd_help},           {"date", &cmd_date},
                           {"soundtest", &pit_sound_test}};

uint8_t cmd_count = sizeof(cmds) / sizeof(cmd_list_t);

void shell_main()
{
    exit = false;
    uint16_t i = 0;
    char c = 0;
    char sc = 0;
    while (!exit) {
        printf("%s", shell_prompt);
        i = 0;
        memset(input_buffer, 0, sizeof(input_buffer));
        while (sc != KEY_ENTER) {
            sc = kbd_get_scancode(true);
            if (sc == KEY_ENTER) {
                putchar('\n');
                break;
            } else if (sc == KEY_BACKSPACE) {
                if (i > 0) {
                    i--;
                    input_buffer[i] = '\0';
                    putchar('\b');
                }
                continue;
            }
            c = scancode_map[(uint8_t)sc];
            if (i < (sizeof(input_buffer) - 1) && c != 0 && c != '\n') {
                input_buffer[i++] = c;
                putchar(c);
            }
        }
        if (i > 0) {
            process_cmd(input_buffer);
        }
        sc = 0;
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

void cmd_clear(int argc, char **argv)
{
    term_clear();
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
    datetime_t datetime;
    cmos_get_datetime(&datetime);

    printf("%d/%d/%d %d:%d:%d\n", datetime.day, datetime.month, datetime.year,
           datetime.hour, datetime.minute, datetime.second);
}