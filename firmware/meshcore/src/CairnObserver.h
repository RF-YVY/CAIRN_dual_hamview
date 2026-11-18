#pragma once
#include "CairnObserverConfig.h"
#include <cstddef>
#include <cstdint>

namespace mesh
{
class Packet;
}

namespace CairnObserver
{
bool begin(const uint8_t *publicKey, const char *name, const char *radio);
bool save(bool enabled, const char *region);
bool save(const Configuration &configuration);
Configuration configuration();
bool enabled();
const char *region();
void capture(float snr, float rssi, const uint8_t *raw, size_t length);
void captureOwnAdvert(const mesh::Packet &packet);
unsigned diagnostics(char *out, unsigned capacity);
void status(char *out, unsigned capacity);
} // namespace CairnObserver
