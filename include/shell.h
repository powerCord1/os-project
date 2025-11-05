typedef struct {
    char name[32];
    void (*func)(int argc, char **argv);
} cmd_list_t;

void process_cmd(char *cmd);
void cmd_clear(int argc, char **argv);
void cmd_exit(int argc, char **argv);
