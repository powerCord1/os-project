/*
    Definitions for app main() functions
*/

typedef struct app {
    char *name;
    void (*main)();
    void (*exit)();
} app_t;

#define DECLARE_APP(_name, identifier)                                         \
    void identifier##_main();                                                  \
    void identifier##_exit();                                                  \
    static const app_t identifier##_app_entry = {                              \
        .name = _name,                                                         \
        .main = identifier##_main,                                             \
        .exit = identifier##_exit,                                             \
    }

DECLARE_APP("Typewriter", typewriter);
DECLARE_APP("Key notes", key_notes);
DECLARE_APP("Shell", shell);
DECLARE_APP("File manager", fm);