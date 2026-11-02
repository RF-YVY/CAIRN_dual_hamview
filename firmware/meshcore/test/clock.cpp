// Runs the shared production clock formatter across both Chicago DST boundaries,
// UTC/local date rollover, 12-hour display, and invalid zone rejection.
#include "CairnClock.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void check(time_t epoch, const char *expected, const char *zone)
{
    char clock[16], date[64], label[16];
    CairnClock::format(epoch, false, clock, sizeof(clock), date, sizeof(date), label, sizeof(label));
    assert(strcmp(clock, expected) == 0);
    assert(strcmp(label, zone) == 0);
}

int main()
{
    assert(CairnClock::save(false, 1));
    check(1793516340, "06:59", "UTC");
    assert(CairnClock::save(true, 1));
    check(1793516340, "01:59", "CDT");
    check(1793516400, "01:00", "CST");
    check(1772956740, "01:59", "CST");
    check(1772956800, "03:00", "CDT");
    char clock[16], date[64], zone[16];
    CairnClock::format(1793579400, true, clock, sizeof(clock), date, sizeof(date), zone, sizeof(zone));
    assert(strcmp(clock, "06:30") == 0 && strstr(date, "Nov 01") && strstr(date, "PM"));
    assert(!CairnClock::save(true, 99));
    puts("Shared UTC/local clock, DST boundaries and date rollover checks passed");
}
