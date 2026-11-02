#include "CairnCompanion.h"
#include "CairnBLE.h"
#include "CairnInterfaces.h"
#include <Preferences.h>
#include <esp_random.h>
#include <helpers/wifi/SerialWifiInterface.h>

namespace CairnCompanion
{
namespace
{
CairnBLE bluetooth;
SerialWifiInterface wifi;
bool useWifi = false, wifiStarted = false;
bool loaded = false, useBluetooth = true, started = false;
uint32_t pairingPin = 0;
char deviceName[48] = "MeshCore-SenseCAP";
struct Settings {
    uint32_t pin;
    uint8_t enabled, wifi, reserved[2];
};
void load()
{
    if (loaded)
        return;
    loaded = true;
    Preferences prefs;
    if (prefs.begin("cairn_link", false)) {
        Settings value{};
        const bool valid = prefs.getBytesLength("config") == sizeof(value) &&
                           prefs.getBytes("config", &value, sizeof(value)) == sizeof(value) && value.pin >= 100000 &&
                           value.pin <= 999999 && value.enabled <= 1;
        if (!valid) {
            value = {100000 + esp_random() % 900000, 1, 0, {}};
            if (prefs.putBytes("config", &value, sizeof(value)) != sizeof(value))
                value.enabled = 0;
        }
        pairingPin = value.pin;
        useBluetooth = value.enabled;
        useWifi = value.wifi == 1;
        prefs.end();
    } else {
        useBluetooth = false;
    }
}
} // namespace
bool begin(CairnInterfaces &interfaces, const char *nodeName)
{
    load();
    char name[32];
    strlcpy(name, nodeName && nodeName[0] ? nodeName : "SenseCAP", sizeof(name));
    snprintf(deviceName, sizeof(deviceName), "MeshCore-%s", name);
    bool success = true;
    if (useBluetooth) {
        started = bluetooth.begin(deviceName, pairingPin) && interfaces.addInterface(InterfaceType::Bluetooth, &bluetooth);
        success = started;
    }
    if (useWifi) {
        wifi.begin(5000);
        wifiStarted = interfaces.addInterface(InterfaceType::WiFi, &wifi);
        success = success && wifiStarted;
    }
    return success;
}
bool enabled()
{
    load();
    return started && bluetooth.isEnabled();
}
bool connected()
{
    return started && bluetooth.isConnected();
}
uint32_t pin()
{
    load();
    return pairingPin;
}
const char *name()
{
    return deviceName;
}
bool save(bool enabled, uint32_t pin)
{
    if (pin < 100000 || pin > 999999)
        return false;
    Preferences prefs;
    if (!prefs.begin("cairn_link", false))
        return false;
    const Settings value{pin, uint8_t(enabled), uint8_t(useWifi), {}};
    const bool saved = prefs.putBytes("config", &value, sizeof(value)) == sizeof(value);
    prefs.end();
    return saved;
}
bool wifiEnabled()
{
    load();
    return useWifi;
}
bool wifiConnected()
{
    return wifiStarted && wifi.isConnected();
}
bool saveWifi(bool enabled)
{
    load();
    Preferences prefs;
    if (!prefs.begin("cairn_link", false))
        return false;
    const Settings value{pairingPin, uint8_t(useBluetooth), uint8_t(enabled), {}};
    const bool saved = prefs.putBytes("config", &value, sizeof(value)) == sizeof(value);
    prefs.end();
    return saved;
}
} // namespace CairnCompanion
