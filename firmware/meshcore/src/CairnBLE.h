#pragma once
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <atomic>
#include <freertos/queue.h>
#include <helpers/BaseSerialInterface.h>

class CairnBLE : public BaseSerialInterface,
                 public BLEServerCallbacks,
                 public BLECharacteristicCallbacks,
                 public BLESecurityCallbacks
{
    struct Frame {
        uint16_t length;
        uint8_t data[MAX_FRAME_SIZE];
    };
    static constexpr unsigned DEPTH = 8;
    StaticQueue_t rxState{}, txState{};
    uint8_t rxStorage[DEPTH * sizeof(Frame)], txStorage[DEPTH * sizeof(Frame)];
    QueueHandle_t rx = nullptr, tx = nullptr;
    BLEServer *server = nullptr;
    BLECharacteristic *notification = nullptr;
    std::atomic<bool> active{false}, authenticated{false};
    std::atomic<uint16_t> connection{0xffff};
    uint32_t pairingPin = 0, lastSend = 0;
    uint32_t onPassKeyRequest() override { return pairingPin; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    bool onConfirmPIN(uint32_t) override { return true; }
    void onAuthenticationComplete(ble_gap_conn_desc *desc) override;
    void onConnect(BLEServer *, ble_gap_conn_desc *desc) override;
    void onDisconnect(BLEServer *, ble_gap_conn_desc *) override;
    void onWrite(BLECharacteristic *characteristic, ble_gap_conn_desc *desc) override;

  public:
    bool begin(const char *name, uint32_t pin);
    void enable() override;
    void disable() override;
    bool isEnabled() const override { return active.load(); }
    bool isConnected() const override { return active.load() && authenticated.load(); }
    bool isWriteBusy() const override { return tx && uxQueueMessagesWaiting(tx) >= DEPTH; }
    size_t writeFrame(const uint8_t *bytes, size_t length) override;
    size_t checkRecvFrame(uint8_t *out) override;
};
