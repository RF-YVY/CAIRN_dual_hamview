#if defined(CAIRN_MUI) || defined(CAIRN_BOOTLOADER)
#include "CairnClock.h"
#include <Preferences.h>
#include <atomic>
#include <stdlib.h>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_private/esp_clk.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <soc/rtc.h>
#include <sys/time.h>
#endif

namespace CairnClock
{
namespace
{
struct Settings {
    uint32_t magic = 0x43434C31;
    uint8_t local = 0, zone = 1, reserved[2] = {};
} settings;
bool loaded = false;
unsigned lastHandoff = 0;
#if defined(CAIRN_CORE_PROFILE) || defined(CAIRN_BOOTLOADER)
std::atomic<bool> clockReady{false};
#else
std::atomic<bool> clockReady{true};
#endif
struct ClockStamp {
    uint64_t epoch, rtc;
    uint32_t calibration, magic;
};
const char *zones[] = {"EST5EDT,M3.2.0/2,M11.1.0/2",
                       "CST6CDT,M3.2.0/2,M11.1.0/2",
                       "MST7MDT,M3.2.0/2,M11.1.0/2",
                       "PST8PDT,M3.2.0/2,M11.1.0/2",
                       "AKST9AKDT,M3.2.0/2,M11.1.0/2",
                       "HST10",
                       "MST7",
                       "UTC0"};
void load()
{
    if (loaded)
        return;
    loaded = true;
    Preferences prefs;
    Settings value;
    if (prefs.begin("cairn_ui", true)) {
        if (prefs.getBytesLength("clockzone") == sizeof(value) &&
            prefs.getBytes("clockzone", &value, sizeof(value)) == sizeof(value) && value.magic == settings.magic &&
            value.zone < 8 && value.local < 2)
            settings = value;
        prefs.end();
    }
}
} // namespace
bool local()
{
    load();
    return settings.local;
}
unsigned zone()
{
    load();
    return settings.zone;
}
const char *zoneOptions()
{
    return "US Eastern\nUS Central (Chicago)\nUS Mountain\nUS Pacific\nAlaska\nHawaii\nArizona\nUTC";
}
const char *timezone()
{
    load();
    return settings.local ? zones[settings.zone] : "UTC0";
}
bool save(bool localTime, unsigned timeZone)
{
    if (timeZone >= 8)
        return false;
    Settings value;
    value.local = localTime;
    value.zone = timeZone;
    Preferences prefs;
    if (!prefs.begin("cairn_ui", false))
        return false;
    const bool saved = prefs.putBytes("clockzone", &value, sizeof(value)) == sizeof(value);
    prefs.end();
    if (saved) {
        settings = value;
        loaded = true;
    }
    return saved;
}
bool checkpointTime()
{
#if defined(ARDUINO_ARCH_ESP32)
    timeval now{};
    gettimeofday(&now, nullptr);
    if (!clockReady.load() || now.tv_sec < 1704067200)
        return false;
    // A one-use checkpoint also covers SDKs that reset the RTC counter at boot.
    const ClockStamp stamp{uint64_t(now.tv_sec) * 1000000 + uint64_t(now.tv_usec), rtc_time_get(), esp_clk_slowclk_cal_get(),
                           0x43435433};
    if (!stamp.calibration)
        return false;
    Preferences prefs;
    if (!prefs.begin("cairn_ui", false))
        return false;
    const bool saved = prefs.putBytes("clockstamp", &stamp, sizeof(stamp)) == sizeof(stamp);
    prefs.end();
    return saved;
#else
    return false;
#endif
}
bool restoreTime()
{
#if defined(ARDUINO_ARCH_ESP32)
#if defined(CAIRN_CORE_PROFILE) || defined(CAIRN_BOOTLOADER)
    clockReady.store(false);
#endif
    ClockStamp stamp{};
    Preferences prefs;
    if (!prefs.begin("cairn_ui", false))
        return false;
    const bool found = prefs.getBytesLength("clockstamp") == sizeof(stamp) &&
                       prefs.getBytes("clockstamp", &stamp, sizeof(stamp)) == sizeof(stamp);
    if (!found || stamp.magic != 0x43435433) {
        prefs.end();
        lastHandoff = 3;
        return false;
    }
    ClockStamp consumed = stamp;
    consumed.magic = 0x43435434;
    const bool saved = prefs.putBytes("clockstamp", &consumed, sizeof(consumed)) == sizeof(consumed);
    prefs.end();
    const auto reason = esp_reset_reason();
    if (!saved || reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) {
        lastHandoff = 4;
        return false;
    }
    const uint64_t rtc = rtc_time_get();
    if (!stamp.calibration || stamp.epoch < 1704067200000000ULL)
        return false;
    uint64_t elapsed = rtc >= stamp.rtc ? rtc_time_slowclk_to_us(rtc - stamp.rtc, stamp.calibration) : UINT64_MAX;
    lastHandoff = 1;
    if (elapsed > 60000000ULL) {
        if (reason != ESP_RST_SW)
            return false;
        elapsed = esp_timer_get_time();
        lastHandoff = 2;
    }
    if (elapsed > 60000000ULL)
        return false;
    const uint64_t epoch = stamp.epoch + elapsed;
    const timeval now{time_t(epoch / 1000000), suseconds_t(epoch % 1000000)};
    const bool restored = settimeofday(&now, nullptr) == 0;
    clockReady.store(restored);
    return restored;
#else
    return false;
#endif
}
unsigned handoffStatus()
{
    return lastHandoff;
}
bool timeSynchronized()
{
    return clockReady.load();
}
void markTimeSynchronized()
{
    clockReady.store(true);
}
void format(time_t epoch, bool twelveHour, char *clock, size_t clockSize, char *date, size_t dateSize, char *label,
            size_t labelSize)
{
    const char *selected = timezone();
    const char *current = getenv("TZ");
    if (!current || strcmp(current, selected)) {
        setenv("TZ", selected, 1);
        tzset();
    }
    tm value{};
    localtime_r(&epoch, &value);
    strftime(clock, clockSize, twelveHour ? "%I:%M" : "%H:%M", &value);
    strftime(date, dateSize, twelveHour ? "%a, %b %d  %p" : "%A, %b %d", &value);
    strftime(label, labelSize, "%Z", &value);
}
} // namespace CairnClock
#endif
