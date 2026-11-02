#pragma once
#include <stdint.h>
namespace CairnRadioTools
{
constexpr unsigned BINS = 53;
bool start(float frequency, float bandwidth, uint8_t sf, uint8_t cr);
void stop();
bool active();
int tick();
int16_t value(unsigned index);
int16_t peak(unsigned index);
} // namespace CairnRadioTools
