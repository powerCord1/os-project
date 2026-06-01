#include <process.h>
#include <string.h>
#include <waitqueue.h>

static proc_entry_t proc_table[PROC_MAX];
static int32_t fg_pid = 0;

void proc_table_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    fg_pid = 0;
}

int32_t proc_alloc(int32_t parent_pid)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].state == PROC_FREE) {
            memset(&proc_table[i], 0, sizeof(proc_entry_t));
            proc_table[i].pid = i + 1;
            proc_table[i].parent_pid = parent_pid;
            proc_table[i].state = PROC_RUNNING;
            waitqueue_init(&proc_table[i].exit_wq);
            waitqueue_init(&proc_table[i].ptrace_wq);
            return i + 1;
        }
    }
    return -1;
}

proc_entry_t *proc_get(int32_t pid)
{
    if (pid < 1 || pid > PROC_MAX)
        return NULL;
    if (proc_table[pid - 1].state == PROC_FREE)
        return NULL;
    return &proc_table[pid - 1];
}

void proc_free(int32_t pid)
{
    if (pid < 1 || pid > PROC_MAX)
        return;
    proc_table[pid - 1].state = PROC_FREE;
}

int32_t proc_foreground_pid(void)
{
    return fg_pid;
}

void proc_set_foreground(int32_t pid)
{
    fg_pid = pid;
}

void proc_mark_exited(int32_t pid, int32_t exit_code)
{
    if (pid < 1 || pid > PROC_MAX)
        return;
    proc_entry_t *e = &proc_table[pid - 1];
    e->state = PROC_EXITED;
    e->exit_code = exit_code;
    waitqueue_wake_all(&e->exit_wq);
    waitqueue_wake_all(&e->ptrace_wq);
}
