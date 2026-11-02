#include "CairnRadioTools.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include "target.h"
#include <algorithm>
namespace CairnRadioTools
{
namespace
{
bool scanning = false;
float savedFrequency, savedBandwidth;
uint8_t savedSf, savedCr;
unsigned index = 0;
uint32_t tunedAt = 0;
int16_t samples[BINS], peaks[BINS];
} // namespace
bool start(float frequency, float bandwidth, uint8_t sf, uint8_t cr)
{
    if (scanning || !radio_driver.scanAllowed())
        return false;
    savedFrequency = frequency;
    savedBandwidth = bandwidth;
    savedSf = sf;
    savedCr = cr;
    for (unsigned i = 0; i < BINS; ++i)
        samples[i] = peaks[i] = -140;
    radio_driver.setTxEnabled(false);
    radio_driver.applyParams(902, 500, sf, cr);
    if (!radio_driver.scanTune(902)) {
        radio_driver.applyParams(savedFrequency, savedBandwidth, savedSf, savedCr);
        radio_driver.resumeReceive();
        radio_driver.setTxEnabled(true);
        return false;
    }
    index = 0;
    scanning = true;
    tunedAt = Time::getMillis();
    return true;
}
void stop()
{
    if (!scanning)
        return;
    scanning = false;
    radio_driver.applyParams(savedFrequency, savedBandwidth, savedSf, savedCr);
    radio_driver.resumeReceive();
    radio_driver.setTxEnabled(true);
}
bool active()
{
    return scanning;
}
int tick()
{
    if (!scanning || !Throttle::hasElapsed(tunedAt, 15))
        return -1;
    const unsigned sampled = index;
    samples[index] = std::max<int16_t>(-140, std::min<int16_t>(-20, radio_driver.getCurrentRSSI()));
    peaks[index] = std::max(peaks[index], samples[index]);
    index = (index + 1) % BINS;
    if (!radio_driver.scanTune(902.0f + index * 0.5f))
        stop();
    tunedAt = Time::getMillis();
    return sampled;
}
int16_t value(unsigned index)
{
    return index < BINS ? samples[index] : -140;
}
int16_t peak(unsigned index)
{
    return index < BINS ? peaks[index] : -140;
}
} // namespace CairnRadioTools
