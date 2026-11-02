#pragma once

#include "CairnRadioSetup.h"
#include <Arduino.h>

#ifndef DEFAULT_FLOOD_SCOPE_NAME
#if defined(CAIRN_PUBLIC_RELEASE)
#define DEFAULT_FLOOD_SCOPE_NAME ""
#else
#define DEFAULT_FLOOD_SCOPE_NAME "us-ms"
#endif
#endif

#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

class SensecapBoard : public ESP32Board
{
  public:
    void begin();
    const char *getManufacturerName() const override { return "SenseCAP Indicator"; }
    void sleep(uint32_t) override { delay(1); }
    void powerOff() override;
};

class SensecapRTCClock : public AutoDiscoverRTCClock
{
    bool acceptExternal = false;

  public:
    explicit SensecapRTCClock(mesh::RTCClock &fallback) : AutoDiscoverRTCClock(fallback) {}
    void setCurrentTime(uint32_t epoch) override;
    void enableExternalTime() { acceptExternal = true; }
};

class SensecapRadio : public CustomSX1262Wrapper
{
  public:
    SensecapRadio(CustomSX1262 &radio, SensecapBoard &board) : CustomSX1262Wrapper(radio, board) {}
    void setTxEnabled(bool enabled) { txEnabled = enabled && CairnRadioSetup::confirmed(); }
    bool scanAllowed() { return isInRecvMode() && !isReceivingPacket(); }
    bool scanTune(float frequency)
    {
        idle();
        const bool ok = static_cast<CustomSX1262 *>(_radio)->setFrequency(frequency) == RADIOLIB_ERR_NONE;
        startRecv();
        return ok;
    }
    void resumeReceive() { startRecv(); }
    void applyParams(float frequency, float bandwidth, uint8_t sf, uint8_t cr)
    {
        idle();
        setParams(frequency, bandwidth, sf, cr);
    }
    bool startSendRaw(const uint8_t *bytes, int length) override
    {
        return txEnabled && CustomSX1262Wrapper::startSendRaw(bytes, length);
    }

  private:
    bool txEnabled = false;
};

extern SensecapBoard board;
extern SensecapRadio radio_driver;
extern SensecapRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
