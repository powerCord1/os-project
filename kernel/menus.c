#include <app.h>
#include <debug.h>
#include <framebuffer.h>
#include <menu.h>
#include <power.h>

void main_menu()
{
    menu_t apps[] = {{"Typewriter", &typewriter_main},
                     {"Key notes", &key_notes_main},
                     {"Random test", &random_test_main},
                     {"Shell", &shell_main},
                     {"Stack Smash Test", &ssp_test_main},
                     {"Element drawing test", &element_test},
                     {"PIT test", &pit_test_main},
                     {"Sine Wave Test", &sin_test_main}};
    create_menu("Main menu", "Choose an app to launch", apps,
                sizeof(apps) / sizeof(menu_t));
}

void power_menu()
{
    log_info("Entering power menu");
    menu_t options[] = {{"Reboot", &reboot}, {"Shutdown", &shutdown}};
    create_menu("Power menu", "Select an option:", options,
                sizeof(options) / sizeof(menu_t));
    fb_clear();
}