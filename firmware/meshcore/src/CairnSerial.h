#pragma once

#include "IndicatorFiles.h"
#include <helpers/BaseSerialInterface.h>

class CairnSerial : public BaseSerialInterface
{
  public:
    void begin(Stream &stream);
    void enable() override { enabled = rx && tx && scratch; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return true; }
    bool isWriteBusy() const override { return false; }
    size_t writeFrame(const uint8_t *bytes, size_t length) override;
    size_t checkRecvFrame(uint8_t *out) override;
    void serviceOnly();

  private:
    static constexpr unsigned CAPACITY = 4608;
    static constexpr unsigned SCRATCH_CAPACITY = 4096;
    Stream *io = nullptr;
    bool enabled = false;
    bool vendor = false;
    uint8_t state = 0;
    uint16_t expected = 0;
    uint16_t received = 0;
    uint16_t discard = 0;
    uint32_t lastByte = 0;
    uint32_t baud = 115200;
    uint8_t *rx = nullptr, *tx = nullptr, *scratch = nullptr;
    meshtastic_DirectoryListing directory = meshtastic_DirectoryListing_init_zero;
    void handle(size_t length);
    void reply(uint8_t operation, uint32_t id, bool ok, unsigned bodyLength = 0);
};
