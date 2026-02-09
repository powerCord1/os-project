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
        {"Random test", &random_test_main},
        {"Shell", &shell_main},
        {"Stack Smash Test", &ssp_test_main},
        {"Element drawing test", &element_test},
        {"PIT test", &pit_test_main},
        {"Sine Wave Test", &sin_test_main},
        {"BMP test", &bmp_test_main},
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