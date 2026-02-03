__attribute__((noreturn)) void reboot();
__attribute__((noreturn)) void shutdown();

// Pulse the reset pin
void sys_reset();

// Shut down the system
void sys_poweroff();

void do_shutdown_calls();