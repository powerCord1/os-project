#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct datetime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} datetime_t;

datetime_t get_datetime();

void set_timezone(int offset_hours);
int get_timezone();
void set_daylight_savings(bool enabled);
bool get_daylight_savings();

// Adjusts the datetime according to the set timezone
datetime_t get_local_datetime();

// Formats a datetime struct into a string (e.g., "YYYY-MM-DD HH:MM:SS")
void format_datetime(datetime_t *dt, char *buf, uint32_t size);