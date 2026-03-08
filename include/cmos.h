#pragma once

#include <stdint.h>

#include "time.h"

void cmos_get_data(datetime_t *dt);
void cmos_wait_for_update();
void cmos_get_datetime(datetime_t *dt);