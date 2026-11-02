#pragma once
#include <stdint.h>
class CairnInterfaces;

namespace CairnCompanion
{
bool begin(CairnInterfaces &interfaces, const char *nodeName);
bool enabled();
bool connected();
uint32_t pin();
const char *name();
bool save(bool enabled, uint32_t pin);
bool wifiEnabled();
bool wifiConnected();
bool saveWifi(bool enabled);
} // namespace CairnCompanion
