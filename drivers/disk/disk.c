#include <ata.h>
#include <debug.h>
#include <disk.h>
#include <heap.h>
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

bool sata_disk_read(disk_t *d, uint64_t lba, uint32_t count, void *buf)
{
    void *phys = pmm_alloc_page();
    if (!phys)
        return false;

    void *dma_buf = phys_to_virt(phys);
    uint8_t *dst = (uint8_t *)buf;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = remaining > 8 ? 8 : remaining;
        if (!sata_read((hba_port_t *)d->driver_data, lba, chunk, dma_buf)) {
            pmm_free_page(phys);
            return false;
        }
        memcpy(dst, dma_buf, chunk * 512);
        dst += chunk * 512;
        lba += chunk;
        remaining -= chunk;
    }

    pmm_free_page(phys);
    return true;
}

static disk_driver_t sata_driver = {
    .name = "sata",
    .read_sectors = sata_disk_read,
    .write_sectors = NULL, // Not implemented yet
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