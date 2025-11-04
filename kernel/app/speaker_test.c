#include <app.h>
#include <pit.h>
#include <stdio.h>

void spk_test_init()
{
    printf("sound test!");
    pit_sound_test();
}