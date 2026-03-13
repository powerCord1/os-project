#include "api.h"

void _start(void)
{
    char *argv[] = {"sh"};
    int pid = spawn_cmd("/sh.wm", argv, 1);
    if (pid >= 0)
        waitpid(pid);
}
