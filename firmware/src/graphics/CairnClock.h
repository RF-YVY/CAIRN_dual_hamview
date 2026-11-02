#pragma once
#include <stddef.h>
#include <time.h>

namespace CairnClock
{
bool local();
unsigned zone();
const char *zoneOptions();
const char *timezone();
bool save(bool localTime, unsigned timeZone);
bool checkpointTime();
bool restoreTime();
unsigned handoffStatus();
bool timeSynchronized();
void markTimeSynchronized();
void format(time_t epoch, bool twelveHour, char *clock, size_t clockSize, char *date, size_t dateSize, char *zone,
            size_t zoneSize);
} // namespace CairnClock
