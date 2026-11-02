#pragma once

#include "configuration.h"

#if defined(HAMVIEW_MUI) && defined(ARCH_ESP32)

#include "HamViewProtocol.h"
#include "concurrency/OSThread.h"
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct HamViewSettings {
    char username[64] = {};
    char telnetPassword[64] = {};
    char telnetHost[64] = "hamalert.org";
    uint16_t telnetPort = 7300;
    char callsignFilter[24] = {};
    uint16_t spotTtlMinutes = 30;
    bool showSpotBanner = true;
};

struct HamViewStatus {
    bool wifiConnected = false;
    bool hamAlertConnected = false;
    uint8_t spotCount = 0;
    uint32_t spotSequence = 0;
    bool showSpotBanner = true;
    char message[64] = {};
};

class HamViewModule : private concurrency::OSThread
{
  public:
    static constexpr size_t MAX_SPOTS = 24;
    HamViewModule();
    ~HamViewModule() override;

    void getSettings(HamViewSettings &out);
    void saveSettings(const HamViewSettings &settings);
    HamViewStatus getSnapshot(HamViewSpot *spots, size_t capacity);

  protected:
    int32_t runOnce() override;

  private:
    WiFiClient client;
    SemaphoreHandle_t mutex = nullptr;
    HamViewSettings settings;
    HamViewSpot spots[MAX_SPOTS];
    size_t spotCount = 0;
    uint32_t spotSequence = 0;
    bool authenticated = false;
    volatile bool reconnectRequested = false;
    uint32_t lastKeepalive = 0;
    String inputLine;
    char statusMessage[64] = "Configure HamAlert Telnet login";

    void loadSettings();
    void disconnect(const char *reason);
    bool connectAndLogin();
    bool waitForServer(const char *expected, uint32_t timeoutMs);
    void consumeInput();
    void processJsonLine(const char *line);
    void pruneExpired();
};

extern HamViewModule *hamViewModule;

#endif
