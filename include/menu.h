#include <stddef.h>

// Menu entry
typedef struct {
    const char *name;
    void (*entry)(void);
} menu_t;

/// @brief Display a menu on the screen
/// @param title The title of the menu which appears at the top of the screen,
/// e.g. 'Power menu'
/// @param prompt Text that appears before the list of items, e.g. 'Choose an
/// option'
/// @param menu Pointer to array of menu entries
/// @param item_count Number of menu entries in the array
void create_menu(const char *title, const char *prompt, menu_t *menu,
                 size_t item_count); // item_count needs to be a paramater due
                                     // to pointer decay