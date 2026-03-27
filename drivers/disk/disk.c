#include <ata.h>
#include <debug.h>
#include <disk.h>
#include <nvme.h>
#include <pmm.h>
#include <sata.h>
#include <string.h>
#include <vmm.h>

disk_t disks[MAX_DISKS];
int disk_count = 0;

bool ata_disk_read(disk_t *d, uint64_t lba, uint32_t count, void *buf)
{
    return ata_read_sectors((uint8_t)(uintptr_t)d->driver_data, lba, count,
                            buf);
}

bool ata_disk_write(disk_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    return ata_write_sectors((uint8_t)(uintptr_t)d->driver_data, lba, count,
                             buf);
}

static disk_driver_t ata_driver = {
    .name = "ata",
    .read_sectors = ata_disk_read,
    .write_sectors = ata_disk_write,
};

#define SATA_MAX_PAGES 32

bool sata_disk_read(disk_t *d, uint64_t lba, uint32_t count, void *buf)
{
    uint8_t *dst = (uint8_t *)buf;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = remaining > (SATA_MAX_PAGES * 8)
                             ? (SATA_MAX_PAGES * 8)
                             : remaining;
        int num_pages = (chunk * 512 + 4095) / 4096;

        void *phys_pages[SATA_MAX_PAGES];
        for (int i = 0; i < num_pages; i++) {
            phys_pages[i] = pmm_alloc_page();
            if (!phys_pages[i]) {
                for (int j = 0; j < i; j++)
                    pmm_free_page(phys_pages[j]);
                return false;
            }
        }

        if (!sata_read_pages((hba_port_t *)d->driver_data, lba, chunk,
                             phys_pages, num_pages)) {
            for (int i = 0; i < num_pages; i++)
                pmm_free_page(phys_pages[i]);
            return false;
        }

        uint32_t bytes_left = chunk * 512;
        for (int i = 0; i < num_pages && bytes_left > 0; i++) {
            uint32_t n = bytes_left > 4096 ? 4096 : bytes_left;
            memcpy(dst, phys_to_virt(phys_pages[i]), n);
            pmm_free_page(phys_pages[i]);
            dst += n;
            bytes_left -= n;
        }

        lba += chunk;
        remaining -= chunk;
    }

    return true;
}

bool sata_disk_write(disk_t *d, uint64_t lba, uint32_t count, const void *buf)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = remaining > (SATA_MAX_PAGES * 8)
                             ? (SATA_MAX_PAGES * 8)
                             : remaining;
        int num_pages = (chunk * 512 + 4095) / 4096;

        void *phys_pages[SATA_MAX_PAGES];
        for (int i = 0; i < num_pages; i++) {
            phys_pages[i] = pmm_alloc_page();
            if (!phys_pages[i]) {
                for (int j = 0; j < i; j++)
                    pmm_free_page(phys_pages[j]);
                return false;
            }
        }

        uint32_t bytes_left = chunk * 512;
        for (int i = 0; i < num_pages && bytes_left > 0; i++) {
            uint32_t n = bytes_left > 4096 ? 4096 : bytes_left;
            memcpy(phys_to_virt(phys_pages[i]), src, n);
            src += n;
            bytes_left -= n;
        }

        if (!sata_write_pages((hba_port_t *)d->driver_data, lba, chunk,
                              phys_pages, num_pages)) {
            for (int i = 0; i < num_pages; i++)
                pmm_free_page(phys_pages[i]);
            return false;
        }

        for (int i = 0; i < num_pages; i++)
            pmm_free_page(phys_pages[i]);

        lba += chunk;
        remaining -= chunk;
    }

    return true;
}

static disk_driver_t sata_driver = {
    .name = "sata",
    .read_sectors = sata_disk_read,
    .write_sectors = sata_disk_write,
};

static disk_driver_t nvme_driver = {
    .name = "nvme",
    .read_sectors = nvme_read,
    .write_sectors = nvme_write,
};

void disk_init()
{
    log_info("Disk: Initializing disk subsystem...");
    disk_count = 0;

    log_verbose("Loading ATA drives");
    for (uint8_t i = 0; i < 4; i++) {
        if (ata_is_drive_present(i)) {
            char name[6];
            strcpy(name, "hd");
            name[2] = 'a' + disk_count;
            name[3] = '\0';
            register_disk(&ata_driver, (void *)(uintptr_t)i, name, 0);
        }
    }

    log_verbose("Loading SATA drives");
    sata_init(&sata_driver);
    // nvme_init(&nvme_driver);
}

void register_disk(disk_driver_t *driver, void *driver_data, const char *name,
                   uint64_t num_sectors)
{
    if (disk_count >= MAX_DISKS) {
        log_warn("Disk: Maximum number of disks reached.");
        return;
    }
    disks[disk_count].id = disk_count;
    disks[disk_count].driver = driver;
    disks[disk_count].driver_data = driver_data;
    strcpy(disks[disk_count].name, name);
    disks[disk_count].num_sectors = num_sectors;
    log_info("Disk: Registered %s (%s) as disk %d", name, driver->name,
             disk_count);
    disk_count++;
}

bool disk_read(int disk_id, uint64_t lba, uint32_t count, void *buf)
{
    if (disk_id >= disk_count) {
        return false;
    }
    return disks[disk_id].driver->read_sectors(&disks[disk_id], lba, count,
                                               buf);
}

bool disk_write(int disk_id, uint64_t lba, uint32_t count, const void *buf)
{
    if (disk_id >= disk_count || !disks[disk_id].driver->write_sectors) {
        return false;
    }
    return disks[disk_id].driver->write_sectors(&disks[disk_id], lba, count,
                                                buf);
}

disk_t *disk_get(int id)
{
    if (id < 0 || id >= disk_count)
        return NULL;
    return &disks[id];
}

int disk_get_count()
{
    return disk_count;
}

bool disk_read_partition_table(int disk_id, partition_entry_t *partitions)
{
    uint8_t mbr[512];
    if (!disk_read(disk_id, 0, 1, mbr))
        return false;

    if (mbr[510] != 0x55 || mbr[511] != 0xAA)
        return false;

    memcpy(partitions, &mbr[446], 4 * sizeof(partition_entry_t));
    return true;
}