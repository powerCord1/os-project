typedef struct {
    const char *name;
    void (*entry)(void);
} app_t;

void typewriter_main(void);
void spk_test_main(void);
void heap_test_main(void);
void key_notes_main(void);
void shell_main(void);