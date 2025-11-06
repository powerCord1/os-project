typedef struct {
    char name[32];
    void (*func)(int argc, char **argv);
} cmd_list_t;

void process_cmd(char *);
void cmd_clear(int argc, char **argv);
void cmd_exit(int argc, char **argv);
void cmd_panic(int argc, char **argv);
void cmd_echo(int argc, char **argv);
void cmd_help(int argc, char **argv);
void cmd_uptime(int argc, char **argv);
void cmd_date(int argc, char **argv);