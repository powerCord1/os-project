#include <stddef.h>

typedef struct {
    const char *name;
    void (*entry)(void);
} menu_t;

void create_menu(const char *title, const char *prompt, menu_t *menu,
                 size_t item_count); // item_count needs to be a paramater due
                                     // to pointer decay