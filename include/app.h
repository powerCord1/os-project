typedef struct {
    const char *name;
    void (*entry)(void);
} app_t;

void typewriter_init(void);
void spk_test_init(void);
void heap_test_init(void);
void key_notes_init(void);