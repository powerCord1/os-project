#include "platform_api_vmcore.h"
#include <pit.h>

uint64
os_time_get_boot_us(void)
{
    return (uint64)system_ticks * 1000;
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

uint64
bh_get_tick_ms(void)
{
    return os_time_get_boot_us() / 1000;
}
