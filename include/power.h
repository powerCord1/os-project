__attribute__((noreturn)) void sys_reboot();
__attribute__((noreturn)) void sys_shutdown();
void sys_suspend();

// Pulse the reset pin
__attribute__((noreturn)) void sys_reset();

// Shut down the system
static void sys_do_poweroff();
static void sys_do_reboot();
static void sys_do_suspend();

void do_shutdown_calls();