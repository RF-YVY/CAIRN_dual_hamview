#include "CairnCompanion.h"
#include "CairnCoreUI.h"
#include "CairnInterfaces.h"
#include "CairnJournal.h"
#include "CairnLocalInterface.h"
#include "CairnRadioTools.h"
#include "CairnSerial.h"
#include "IndicatorFiles.h"
#include "MyMesh.h"
#include "graphics/CairnBoot.h"
#include "graphics/CairnClock.h"
#include "target.h"
#include <SPIFFS.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ArduinoSerialInterface.h>
#include <helpers/MultiSerialInterface.h>

CairnLocalInterface localInterface;

namespace
{
DataStore store(SPIFFS, rtc_clock);
StdRNG rng;
SimpleMeshTables tables;
MyMesh meshCore(radio_driver, rng, rtc_clock, tables, store);
CairnSerial usb;
CairnInterfaces interfaces;
bool ready = false;
} // namespace

void setup()
{
    Serial.setRxBufferSize(8192);
    Serial.begin(115200);
    Serial.println("SenseCAP MeshCore CAIRN interface");
    board.begin();
    Serial.println(indicatorFiles.begin() ? "RP2040 storage link ready" : "RP2040 link unavailable");
    if (!coreUiBegin(meshCore, store))
        Serial.println("MeshCore touchscreen initialization failed");
    else
        Serial.println("MeshCore touchscreen initialized: 480x480, shared HamView");
    usb.begin(Serial);
    // Resident layouts keep this volume separate from Meshtastic's LittleFS.
    const char *filesystem = CairnBoot::resident() ? "mcfs" : "spiffs";
    if (!SPIFFS.begin(false, "/meshcore", 5, filesystem)) {
        Serial.println("MeshCore storage unavailable; existing filesystem preserved");
        coreUiSetReady(false, "MeshCore storage unavailable");
        return;
    }
    if (!SPIFFS.exists("/meshcore.profile")) {
        SPIFFS.end();
        Serial.println("MeshCore profile marker missing; existing filesystem preserved");
        coreUiSetReady(false, "MeshCore profile unavailable");
        return;
    }
    if (!radio_init()) {
        Serial.println("MeshCore radio initialization failed");
        coreUiSetReady(false, "MeshCore radio unavailable");
        return;
    }
    rng.begin(radio_driver.getRngSeed());
    store.begin();
    meshCore.begin();
    rtc_clock.enableExternalTime();
    // Contact loading seeds a logical clock; restore current wall time afterward.
    CairnClock::restoreTime();
    messageJournal.begin();
    const NodePrefs *prefs = meshCore.getNodePrefs();
    Serial.printf("MeshCore ready: %.3f MHz, BW %.1f kHz, SF%u, CR4/%u, scope %s\n", prefs->freq, prefs->bw, prefs->sf, prefs->cr,
                  prefs->default_scope_name);
    interfaces.addInterface(InterfaceType::USB, &usb);
    interfaces.addInterface(InterfaceType::HardwareSerial, &localInterface);
    if (!CairnCompanion::begin(interfaces, meshCore.getNodePrefs()->node_name))
        Serial.println("Bluetooth companion unavailable");
    meshCore.startInterface(interfaces);
    coreUiStartNetwork();
    board.onBootComplete();
    ready = true;
    coreUiSetReady(true, "MeshCore ready");
}

void loop()
{
    if (ready) {
        if (!CairnRadioTools::active())
            meshCore.loop();
        else
            usb.serviceOnly();
        interfaces.loop();
        sensors.loop();
        rtc_clock.tick();
        board.loop();
    } else {
        usb.serviceOnly();
    }
    coreUiLoop();
    delay(1);
}
