/*
    Definitions for app main() functions
*/

#define DEFINE_APP(name, identifier)                                           \
    void identifier##_main();                                                  \
    void identifier##_exit();

typedef struct app {
    char *name;
    void (*main)();
    void (*exit)();
} app_t;

void typewriter_main();
void key_notes_main();
void shell_main();
void file_manager_main();