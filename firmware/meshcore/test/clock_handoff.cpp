// Exercises the production UTC checkpoint and restore path with independent RTC
// ticks, changed calibration, cold resets, invalid records, and stale checkpoints.
#include "CairnClock.h"
#include <Preferences.h>
#include <cassert>
#include <cstdio>
#include <esp_system.h>
#include <sys/time.h>

static timeval wall{1793579400, 250000};
static uint64_t ticks = 1000000;
static uint32_t calibration = 1 << 19;
static esp_reset_reason_t reset = ESP_RST_SW;
static int64_t bootUs = 2000000;
int64_t esp_timer_get_time()
{
    return bootUs;
}

extern "C" int gettimeofday(timeval *out, void *) noexcept
{
    *out = wall;
    return 0;
}
extern "C" int settimeofday(const timeval *value, const struct timezone *) noexcept
{
    wall = *value;
    return 0;
}
uint64_t rtc_time_get()
{
    return ticks;
}
uint32_t esp_clk_slowclk_cal_get()
{
    return calibration;
}
uint64_t rtc_time_slowclk_to_us(uint64_t delta, uint32_t period)
{
    return (delta * period) >> 19;
}
esp_reset_reason_t esp_reset_reason()
{
    return reset;
}

int main()
{
    assert(!CairnClock::timeSynchronized());
    assert(!CairnClock::checkpointTime());
    assert(!CairnClock::restoreTime());
    auto seed = [] {
        wall = {1793579400, 250000};
        ticks = 1000000;
        calibration = 1 << 19;
        reset = ESP_RST_SW;
        bootUs = 2000000;
        CairnClock::markTimeSynchronized();
        assert(CairnClock::checkpointTime());
    };
    seed();
    ticks += 5000000;
    calibration *= 2;
    wall = {};
    assert(CairnClock::restoreTime());
    assert(wall.tv_sec == 1793579405 && wall.tv_usec == 250000);
    assert(CairnClock::timeSynchronized());
    assert(!CairnClock::restoreTime());

    seed();
    reset = ESP_RST_POWERON;
    wall = {};
    assert(!CairnClock::restoreTime() && wall.tv_sec == 0);
    assert(!CairnClock::timeSynchronized());
    reset = ESP_RST_SW;
    assert(!CairnClock::restoreTime());
    seed();
    reset = ESP_RST_BROWNOUT;
    assert(!CairnClock::restoreTime());
    seed();
    ticks = 1;
    wall = {};
    assert(CairnClock::restoreTime() && wall.tv_sec == 1793579402 && CairnClock::handoffStatus() == 2);
    seed();
    ticks = 1000000 + 86401000000ULL;
    assert(CairnClock::restoreTime() && wall.tv_sec == 1793579402);
    seed();
    ticks = 1;
    bootUs = 61000000;
    assert(!CairnClock::restoreTime());

    seed();
    wall = {1, 0};
    assert(!CairnClock::checkpointTime());
    ticks = 2000000;
    assert(CairnClock::restoreTime() && wall.tv_sec == 1793579401);
    Preferences prefs;
    prefs.begin("cairn_ui", false);
    uint8_t invalid[24]{};
    prefs.putBytes("clockstamp", invalid, sizeof(invalid));
    assert(!CairnClock::restoreTime());
    prefs.putBytes("clockstamp", invalid, 3);
    assert(!CairnClock::restoreTime());
    puts("Clock handoff, saved calibration, reset and checkpoint rejection checks passed");
}
