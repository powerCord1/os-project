#include <app.h>
#include <array.h>
#include <debug.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <menu.h>
#include <menus.h>
#include <power.h>
#include <tests.h>

void main_menu()
{
    menu_t apps[] = {
        {"Typewriter", &typewriter_main},
        {"Key notes", &key_notes_main},
        {"Shell", &shell_main},
        {"Test menu", &test_menu},
    };
    create_menu("Main menu", "Choose an app to launch", apps, ARRAY_SIZE(apps));
}

void power_menu()
{
    log_info("Entering power menu");
    menu_t options[] = {{"Reboot", &reboot}, {"Shutdown", &shutdown}};
    create_menu("Power menu", "Select an option:", options,
                ARRAY_SIZE(options));
}

void test_menu()
{
    log_info("Entering test menu");
    create_menu("Test menu", "Select a test to run:", tests, ARRAY_SIZE(tests));
}