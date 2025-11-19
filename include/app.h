typedef struct {
    const char *name;
    void (*entry)(void);
} app_t;

void typewriter_main();
void heap_test_main();
void key_notes_main();
void shell_main();
void ssp_test_main();