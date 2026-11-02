#include "target.h"
#include "graphics/CairnClock.h"
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <helpers/ArduinoHelpers.h>

SensecapBoard board;
static SPIClass radioSpi;
static CustomSX1262 radio(new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, radioSpi));
SensecapRadio radio_driver(radio, board);
static ESP32RTCClock fallbackClock;
SensecapRTCClock rtc_clock(fallbackClock);
EnvironmentSensorManager sensors;

void SensecapRTCClock::setCurrentTime(uint32_t epoch)
{
    AutoDiscoverRTCClock::setCurrentTime(epoch);
    if (acceptExternal && epoch >= 1704067200) {
        const timeval value{time_t(epoch), 0};
        if (settimeofday(&value, nullptr) == 0)
            CairnClock::markTimeSynchronized();
    }
}

void SensecapBoard::begin()
{
    ESP32Board::begin();
    fallbackClock.begin();
    if (fallbackClock.getCurrentTime() <= RTC_TIME_MIN + 5) {
        const timeval unset{};
        settimeofday(&unset, nullptr);
    }
    setInhibitSleep(true);
    pinMode(72, OUTPUT);
    digitalWrite(72, HIGH);
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
}

void SensecapBoard::powerOff()
{
    radio_driver.powerOff();
    Serial.flush();
    esp_deep_sleep_start();
}

bool radio_init()
{
    rtc_clock.begin(Wire);
    return radio.std_init(&radioSpi);
}

mesh::LocalIdentity radio_new_identity()
{
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}
