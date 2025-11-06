#include <cmos.h>
#include <io.h>

#define CMOS_REG_SECOND 0x00
#define CMOS_REG_MINUTE 0x02
#define CMOS_REG_HOUR 0x04
#define CMOS_REG_WEEKDAY 0x06
#define CMOS_REG_DAY 0x07
#define CMOS_REG_MONTH 0x08
#define CMOS_REG_YEAR 0x09
#define CMOS_REG_CENTURY 0x32
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

enum { CMOS_ADDR = 0x70, CMOS_DATA = 0x71 };

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int get_update_in_progress_flag()
{
    return cmos_read(CMOS_REG_STATUS_A) & 0x80;
}

void cmos_get_datetime(datetime_t *dt)
{
    datetime_t last_dt;
    uint8_t register_b;

    cmos_wait_for_update();
    cmos_get_data(dt);

    do {
        last_dt = *dt;
        cmos_wait_for_update();
        cmos_get_data(dt);
    } while ((last_dt.second != dt->second) || (last_dt.minute != dt->minute) ||
             (last_dt.hour != dt->hour) || (last_dt.day != dt->day) ||
             (last_dt.month != dt->month) || (last_dt.year != dt->year));

    register_b = cmos_read(CMOS_REG_STATUS_B);

    // convert BCD to binary if necessary
    if (!(register_b & 0x04)) {
        dt->second = (dt->second & 0x0F) + ((dt->second / 16) * 10);
        dt->minute = (dt->minute & 0x0F) + ((dt->minute / 16) * 10);
        dt->hour = ((dt->hour & 0x0F) + (((dt->hour & 0x70) / 16) * 10)) |
                   (dt->hour & 0x80);
        dt->day = (dt->day & 0x0F) + ((dt->day / 16) * 10);
        dt->month = (dt->month & 0x0F) + ((dt->month / 16) * 10);
        dt->year = (dt->year & 0x0F) + ((dt->year / 16) * 10);
    }

    // 12h to 24h
    if (!(register_b & 0x02) && (dt->hour & 0x80)) {
        dt->hour = ((dt->hour & 0x7F) + 12) % 24;
    }

    dt->year += 2000; // TODO: read century reg if enabled
}

void cmos_wait_for_update()
{
    while (get_update_in_progress_flag())
        ;
}

void cmos_get_data(datetime_t *dt)
{
    dt->second = cmos_read(CMOS_REG_SECOND);
    dt->minute = cmos_read(CMOS_REG_MINUTE);
    dt->hour = cmos_read(CMOS_REG_HOUR);
    dt->day = cmos_read(CMOS_REG_DAY);
    dt->month = cmos_read(CMOS_REG_MONTH);
    dt->year = cmos_read(CMOS_REG_YEAR);
}