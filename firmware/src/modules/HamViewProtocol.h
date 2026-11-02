#pragma once
#include <stddef.h>
#include <stdint.h>

struct HamViewSpot {
    char callsign[20] = {};
    char frequency[16] = {};
    char mode[12] = {};
    char spotter[20] = {};
    char comment[80] = {};
    uint32_t receivedAt = 0;
};

namespace HamViewProtocol
{
bool parseSpot(const char *line, HamViewSpot &spot);
bool containsIgnoreCase(const char *text, const char *needle);
} // namespace HamViewProtocol
