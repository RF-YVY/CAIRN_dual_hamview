#pragma once
#include <stdint.h>
uint64_t rtc_time_get();
uint64_t rtc_time_slowclk_to_us(uint64_t ticks, uint32_t calibration);
