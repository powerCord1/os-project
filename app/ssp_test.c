#include <app.h>
#include <stdio.h>
#include <string.h>

void ssp_test_main()
{
    printf("Smashing the stack...\n");
    char buffer[10];
    memset(buffer, 'A', 100);
    printf("Stack killed successfully");
}