__attribute__((noreturn)) void reboot();
__attribute__((noreturn)) void shutdown();
void sys_reset();
void sys_poweroff();
static void wait_for_render();