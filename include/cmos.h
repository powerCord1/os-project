#pragma once

#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} datetime_t;

void cmos_get_datetime(datetime_t *);
void cmos_wait_for_update(void);
void cmos_get_data(datetime_t *);