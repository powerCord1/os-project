#include <stdbool.h>

#include <cmos.h>
#include <cpu.h>
#include <framebuffer.h>
#include <panic.h>
#include <power.h>
#include <shell.h>
#include <sound.h>
#include <stdio.h>
#include <string.h>

static bool daylight_savings_enabled = false;

void cmd_history(int argc, char **argv)
{
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", i + 1, command_history[i]);
    }
}

void cmd_clear(int argc, char **argv)
{
    fb_clear_vp();
}

void cmd_exit(int argc, char **argv)
{
    exit = true;
}

void cmd_panic(int argc, char **argv)
{
    panic("manually triggered panic");
}

void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            putchar(' ');
        }
    }
    putchar('\n');
}

void cmd_help(int argc, char **argv)
{
    printf("Available commands:\n");
    for (int i = 0; i < cmd_count; i++) {
        printf("- %s\n", cmds[i].name);
    }
}

void cmd_date(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--toggle-daylight-savings") == 0) {
        daylight_savings_enabled = !daylight_savings_enabled;
        printf("Daylight savings %s\n",
               daylight_savings_enabled ? "enabled" : "disabled");
        return;
    }

    datetime_t datetime;
    cmos_get_datetime(&datetime);

    if (daylight_savings_enabled) {
        datetime.hour = (datetime.hour + 1) % 24;
        // Note: This is a simplified implementation. A full implementation
        // would also handle date changes when the time crosses midnight.
    }

    printf("%02d/%02d/%04d %02d:%02d:%02d\n", datetime.day, datetime.month,
           datetime.year, datetime.hour, datetime.minute, datetime.second);
}

void cmd_shutdown(int argc, char **argv)
{
    shutdown();
}

void cmd_reboot(int argc, char **argv)
{
    reboot();
}

void cmd_sound_test(int argc, char **argv)
{
    printf("WHEEEE\n");
    sound_test();
}

void cmd_sysinfo(int argc, char **argv)
{
    printf("System information:\n");

    uint64_t cr0, cr2, cr3, cr4, rflags;

    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    asm volatile("pushfq; popq %0" : "=r"(rflags));

    printf("CR0:    0x%016lx\n", cr0);
    printf("CR2:    0x%016lx\n", cr2);
    printf("CR3:    0x%016lx\n", cr3);
    printf("CR4:    0x%016lx\n", cr4);
    printf("RFLAGS: 0x%016lx\n", rflags);

    printf("APIC:   %s\n\n", is_apic_enabled() ? "enabled" : "disabled");
    printf("Build version: %s\nBuild time: %s\nCommit: %s\n", BUILD_VERSION,
           BUILD_TIME, COMMIT);
}

void cmd_fbtest(int argc, char **argv)
{
    fb_matrix_test();
}

void cmd_rgbtest(int argc, char **argv)
{
    fb_rgb_test();
}

void cmd_memtest(int argc, char **argv)
{
    printf("Running memory test...\n");

    // memset
    char memset_buf[10];
    memset(memset_buf, 'A', 10);
    bool memset_ok = true;
    for (int i = 0; i < 10; i++) {
        if (memset_buf[i] != 'A') {
            memset_ok = false;
            break;
        }
    }
    printf("memset: %s\n", memset_ok ? "PASS" : "FAIL");

    // memcpy
    char memcpy_src[] = "Hello";
    char memcpy_dst[6];
    memcpy(memcpy_dst, memcpy_src, 6);
    printf("memcpy: %s\n",
           strcmp(memcpy_src, memcpy_dst) == 0 ? "PASS" : "FAIL");

    // memcmp
    char memcmp_buf1[] = "Test";
    char memcmp_buf2[] = "Test";
    char memcmp_buf3[] = "Fail";
    printf("memcmp (equal): %s\n",
           memcmp(memcmp_buf1, memcmp_buf2, 5) == 0 ? "PASS" : "FAIL");
    printf("memcmp (unequal): %s\n",
           memcmp(memcmp_buf1, memcmp_buf3, 5) != 0 ? "PASS" : "FAIL");

    // memmove
    char memmove_buf[] = "123456789";
    memmove(memmove_buf + 2, memmove_buf, 5); // overlapping
    printf("memmove (overlap): %s\n",
           strcmp(memmove_buf, "121234589") == 0 ? "PASS" : "FAIL");
}