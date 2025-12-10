#include <stdbool.h>

#include <ata.h>
#include <cmos.h>
#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <fs.h>
#include <heap.h>
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
        // TODO: handle date changes when the time crosses midnight.
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

void cmd_lsblk(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Disks and Partitions:\n");
    for (uint8_t drive = 0; drive < 4; drive++) {
        if (ata_is_drive_present(drive)) {
            printf("Drive %d:\n", drive);
            partition_entry_t partitions[4];
            ata_read_partition_table(drive, partitions);

            for (int i = 0; i < 4; i++) {
                if (partitions[i].num_sectors > 0) {
                    printf("  Partition %d: Type 0x%x, LBA 0x%x, Sectors %u\n",
                           i, partitions[i].type, partitions[i].lba_start,
                           partitions[i].num_sectors);
                } else {
                    log_warn("partitions[%d].num_sectors == 0", i);
                }
            }
        }
    }
}

void cmd_mount(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: mount <drive> <partition>\n");
        return;
    }

    if (mounted_fs) {
        printf("A filesystem is already mounted. Unmount first.\n");
        return;
    }

    uint8_t drive = atoi(argv[1]);
    uint8_t part_num = atoi(argv[2]);

    log_verbose("mount: drive: %d, part_num: %d", drive, part_num);

    if (drive >= 4 || !ata_is_drive_present(drive)) {
        printf("Invalid or non-existent drive.\n");
        return;
    }

    partition_entry_t partitions[4];
    ata_read_partition_table(drive, partitions);

    if (part_num >= 4 || partitions[part_num].num_sectors == 0) {
        printf("Invalid or non-existent partition.\n");
        return;
    }

    partition_entry_t *p = &partitions[part_num];
    if (fat32_mount(drive, p->lba_start, p->num_sectors)) {
        printf("Successfully mounted partition %d on drive %d.\n", part_num,
               drive);
    } else {
        printf("Failed to mount partition.\n");
    }
}

void cmd_umount(int argc, char **argv)
{
    if (argc != 1) {
        printf("Usage: umount\n");
        return;
    }

    if (fat32_unmount()) {
        printf("Filesystem unmounted successfully.\n");
    } else {
        printf("No filesystem to unmount.\n");
    }
}

void cmd_ls(int argc, char **argv)
{
    if (argc > 2) {
        printf("Usage: ls [directory]\n");
        return;
    }

    fat32_fs_t *mounted_fs;
    if (!(mounted_fs = fat32_get_mounted_fs())) {
        printf("No filesystem mounted. Use 'mount' first.\n");
        return;
    }
    uint32_t target_cluster = mounted_fs->root_cluster;
    if (argc == 2) {
        char *path = argv[1];
        int len = strlen(path);

        // trim trailing "/."
        if (len > 2 && strcmp(path + len - 2, "/.") == 0) {
            path[len - 2] = '\0';
            len -= 2;
        }

        // trim trailing "/"
        if (len > 1 && path[len - 1] == '/') {
            path[len - 1] = '\0';
        }

        // if path is not empty, not just root, and not current dir '.'
        if (len > 0 && strcmp(path, "/") != 0 && strcmp(path, ".") != 0) {
            fat32_dir_entry_t *dir_entry =
                fat32_find_file(mounted_fs->root_cluster, path);
            if (!dir_entry) {
                printf("Directory not found: %s\n", path);
                return;
            }
            if (!(dir_entry->attributes & FAT32_ATTRIBUTE_DIRECTORY)) {
                printf("%s is not a directory.\n", argv[1]);
                free(dir_entry);
                return;
            }
            target_cluster = (dir_entry->first_cluster_high << 16) |
                             dir_entry->first_cluster_low;
            free(dir_entry);
        }
    }

    int dir_count = 0;
    char **dir_entries = fat32_list_directory(target_cluster, &dir_count);

    if (dir_entries) {
        for (int i = 0; i < dir_count; i++) {
            printf("%s\n", dir_entries[i]);
            free(dir_entries[i]);
        }
        free(dir_entries);
    }
}

void cmd_cat(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: cat <filename>\n");
        return;
    }

    if (!fat32_is_mounted()) {
        printf("No filesystem mounted. Use 'mount' first.\n");
        return;
    }

    char *file_content =
        fat32_read_file(fat32_get_mounted_fs()->root_cluster, argv[1]);
    if (file_content) {
        printf("%s\n", file_content);
        free(file_content);
    } else {
        printf("Failed to read file '%s'.\n", argv[1]);
    }
}

void cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: write <filename> <content>\n");
        return;
    }

    if (!fat32_is_mounted()) {
        printf("No filesystem mounted. Use 'mount' first.\n");
        return;
    }

    fat32_fs_t *mounted_fs = fat32_get_mounted_fs();
    const char *filename = argv[1];
    const char *content = argv[2];
    uint32_t content_size = strlen(content);

    // TODO: write to the current directory instead of the root directory.
    uint32_t parent_cluster = mounted_fs->root_cluster;

    if (fat32_write_file(mounted_fs, parent_cluster, filename,
                         (const uint8_t *)content, content_size)) {
        printf("File '%s' written successfully.\n", filename);
    } else {
        printf("Failed to write file '%s'.\n", filename);
    }
}

void cmd_rm(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: rm <filename>\n");
        return;
    }

    if (!fat32_is_mounted()) {
        printf("No filesystem mounted. Use 'mount' first.\n");
        return;
    }

    fat32_fs_t *mounted_fs = fat32_get_mounted_fs();
    const char *filename = argv[1];

    // TODO: delete from the current directory instead of the root directory.
    uint32_t parent_cluster = mounted_fs->root_cluster;

    if (fat32_delete_file(parent_cluster, filename)) {
        printf("File '%s' deleted successfully.\n", filename);
    } else {
        printf("Failed to delete file '%s'.\n", filename);
    }
}

void cmd_mkdir(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: mkdir <dirname>\n");
        return;
    }

    if (!fat32_is_mounted()) {
        printf("No filesystem mounted. Use 'mount' first.\n");
        return;
    }

    fat32_fs_t *mounted_fs = fat32_get_mounted_fs();
    const char *dirname = argv[1];

    uint32_t parent_cluster = mounted_fs->root_cluster;

    if (fat32_create_directory(parent_cluster, dirname)) {
        printf("Directory '%s' created successfully.\n", dirname);
    } else {
        printf("Failed to create directory '%s'.\n", dirname);
    }
}

void cmd_rmdir(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: rmdir <dirname>\n");
        return;
    }

    if (!fat32_is_mounted()) {
        printf("No filesystem mounted. Use 'mount' first.\n");
        return;
    }

    fat32_fs_t *mounted_fs = fat32_get_mounted_fs();
    const char *dirname = argv[1];

    uint32_t parent_cluster = mounted_fs->root_cluster;

    if (fat32_delete_directory(parent_cluster, dirname)) {
        printf("Directory '%s' deleted successfully.\n", dirname);
    } else {
        printf("Failed to delete directory '%s'.\n", dirname);
    }
}
