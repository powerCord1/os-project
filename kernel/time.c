#include <stdbool.h>

#include <cmos.h>
#include <debug.h>
#include <stdio.h>
#include <time.h>

static int timezone_offset = 0;
static bool daylight_savings_enabled = false;

void set_timezone(int offset)
{
    log_verbose("Setting timezone offet to %d hours", offset);
    timezone_offset = offset;
}

int get_timezone()
{
    return timezone_offset;
}

void set_daylight_savings(bool enabled)
{
    daylight_savings_enabled = enabled;
}

bool get_daylight_savings()
{
    return daylight_savings_enabled;
}

static bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static uint8_t days_in_month(uint8_t month, uint16_t year)
{
    switch (month) {
    case 1:
        return 31;
    case 2:
        return is_leap_year(year) ? 29 : 28;
    case 3:
        return 31;
    case 4:
        return 30;
    case 5:
        return 31;
    case 6:
        return 30;
    case 7:
        return 31;
    case 8:
        return 31;
    case 9:
        return 30;
    case 10:
        return 31;
    case 11:
        return 30;
    case 12:
        return 31;
    default:
        return 0;
    }
}

datetime_t get_datetime()
{
    datetime_t dt;
    cmos_get_datetime(&dt);
    return dt;
}

datetime_t get_local_datetime()
{
    datetime_t dt = get_datetime();
    int new_hour = (int)dt.hour + timezone_offset;

    if (new_hour >= 24) {
        dt.hour = (uint8_t)(new_hour - 24);
        dt.day++;
        if (dt.day > days_in_month(dt.month, dt.year)) {
            dt.day = 1;
            dt.month++;
            if (dt.month > 12) {
                dt.month = 1;
                dt.year++;
            }
        }
    } else if (new_hour < 0) {
        dt.hour = (uint8_t)(new_hour + 24);
        if (dt.day == 1) {
            if (dt.month == 1) {
                dt.month = 12;
                dt.year--;
            } else {
                dt.month--;
            }
            dt.day = days_in_month(dt.month, dt.year);
        } else {
            dt.day--;
        }
    } else {
        dt.hour = (uint8_t)new_hour;
    }

    return dt;
}

void format_datetime(datetime_t *dt, char *buf, uint32_t size)
{
    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d", dt->year, dt->month,
             dt->day, dt->hour, dt->minute, dt->second);
}
