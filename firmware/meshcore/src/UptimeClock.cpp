#include "UptimeClock.h"
#include <Arduino.h>
#include <esp_timer.h>

namespace Time
{
uint32_t getMillis()
{
    return millis();
}
uint64_t getMillisMonotonic()
{
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}
uint32_t getUptimeSecs()
{
    return getMillisMonotonic() / 1000;
}
// ESP timer maintains the 64-bit clock without a software wrap counter.
void serviceMonotonic() {}
} // namespace Time
