#include <app.h>
#include <array.h>
#include <debug.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <menu.h>
#include <menus.h>
#include <power.h>
#include <tests.h>
#include <tty.h>
#include <wasm_runner.h>

static void wasm_init(void)
{
    fb_clear();
    fb_set_cursor(0, 0);
    tty_sync_from_fb(tty_get(0));
    char *argv[] = {"init"};
    wasm_run_file("/init.wm", 1, argv);
}

void main_menu()
{
    menu_t apps[] = {
        {"Typewriter", &typewriter_main},
        {"Key notes", &key_notes_main},
        {"Shell", &shell_main},
        {"WASM Init", &wasm_init},
        {"Test menu", &test_menu},
    };
    create_menu("Main menu", "Choose an app to launch", apps, ARRAY_SIZE(apps));
}

void power_menu()
{
    log_info("Entering power menu");
    menu_t options[] = {
        {"Reboot", &sys_reboot},
        {"Shutdown", &sys_shutdown},
    };
    create_menu("Power menu", "Select an option:", options,
                ARRAY_SIZE(options));
}

void test_menu()
{
    log_info("Entering test menu");
    create_menu("Test menu", "Select a test to run:", tests, ARRAY_SIZE(tests));
}