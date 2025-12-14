#include <cmos.h>

static datetime_t boot_time;

typedef struct {
    const char *msg;
    void (*func)(void);
} boot_task_t;

void sys_init();
void init_early();
void store_boot_time();
void log_boot_info();
void init_late();
void execute_tasks(boot_task_t *tasks, size_t num_tasks);