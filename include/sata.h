#pragma once

#include <stdint.h>
#include <ahci.h>

struct disk_driver;
typedef struct disk_driver disk_driver_t;

void sata_init(disk_driver_t *driver);
int sata_read(hba_port_t *port, uint64_t start, uint32_t count, uint16_t *buf);
