#include "CairnBLE.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"
#include <esp_mac.h>

namespace
{
constexpr const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char *RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char *TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
} // namespace
bool CairnBLE::begin(const char *name, uint32_t pin)
{
    pairingPin = pin;
    rx = xQueueCreateStatic(DEPTH, sizeof(Frame), rxStorage, &rxState);
    tx = xQueueCreateStatic(DEPTH, sizeof(Frame), txStorage, &txState);
    if (!rx || !tx || !BLEDevice::init(name))
        return false;
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    mac[0] = (mac[0] | 0xC0) ^ 0x02;
    char text[18];
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    BLEAddress address(String(text), BLE_ADDR_RANDOM);
    if (!BLEDevice::setOwnAddr(address) || !BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM))
        return false;
    BLEDevice::setMTU(247);
    BLEDevice::setSecurityCallbacks(this);
    BLESecurity::setAuthenticationMode(true, true, true);
    BLESecurity::setCapability(BLE_HS_IO_DISPLAY_ONLY);
    BLESecurity::setPassKey(true, pin);
    server = BLEDevice::createServer();
    if (!server)
        return false;
    server->setCallbacks(this);
    auto *service = server->createService(SERVICE_UUID);
    if (!service)
        return false;
    notification =
        service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY |
                                              BLECharacteristic::PROPERTY_READ_ENC | BLECharacteristic::PROPERTY_READ_AUTHEN);
    auto *input = service->createCharacteristic(RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC |
                                                        BLECharacteristic::PROPERTY_WRITE_AUTHEN);
    if (!notification || !input)
        return false;
    input->setCallbacks(this);
    service->start();
    auto *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    return true;
}
void CairnBLE::enable()
{
    if (!server || active.load())
        return;
    server->advertiseOnDisconnect(true);
    active.store(BLEDevice::getAdvertising()->start());
}
void CairnBLE::disable()
{
    active.store(false);
    authenticated.store(false);
    if (server) {
        server->advertiseOnDisconnect(false);
        BLEDevice::getAdvertising()->stop();
        const auto id = connection.load();
        if (id != 0xffff)
            server->disconnect(id);
    }
    if (rx)
        xQueueReset(rx);
    if (tx)
        xQueueReset(tx);
}
void CairnBLE::onConnect(BLEServer *, ble_gap_conn_desc *desc)
{
    connection.store(desc->conn_handle);
    authenticated.store(desc->sec_state.encrypted && desc->sec_state.authenticated);
}
void CairnBLE::onDisconnect(BLEServer *, ble_gap_conn_desc *)
{
    connection.store(0xffff);
    authenticated.store(false);
    xQueueReset(rx);
    xQueueReset(tx);
}
void CairnBLE::onAuthenticationComplete(ble_gap_conn_desc *desc)
{
    const bool secure = desc->sec_state.encrypted && desc->sec_state.authenticated;
    authenticated.store(secure);
    if (!secure)
        server->disconnect(desc->conn_handle);
}
void CairnBLE::onWrite(BLECharacteristic *characteristic, ble_gap_conn_desc *desc)
{
    if (!active.load() || !desc->sec_state.encrypted || !desc->sec_state.authenticated)
        return;
    const auto value = characteristic->getValue();
    if (!value.length() || value.length() > MAX_FRAME_SIZE)
        return;
    Frame frame{};
    frame.length = value.length();
    memcpy(frame.data, value.c_str(), frame.length);
    xQueueSend(rx, &frame, 0);
}
size_t CairnBLE::writeFrame(const uint8_t *bytes, size_t length)
{
    if (!isConnected() || !length || length > MAX_FRAME_SIZE)
        return 0;
    Frame frame{};
    frame.length = length;
    memcpy(frame.data, bytes, length);
    return xQueueSend(tx, &frame, 0) == pdTRUE ? length : 0;
}
size_t CairnBLE::checkRecvFrame(uint8_t *out)
{
    if (!isEnabled())
        return 0;
    Frame frame{};
    if (isConnected() && Throttle::hasElapsed(lastSend, 60) && xQueueReceive(tx, &frame, 0) == pdTRUE) {
        lastSend = Time::getMillis();
        notification->setValue(frame.data, frame.length);
        notification->notify();
    }
    if (xQueueReceive(rx, &frame, 0) != pdTRUE)
        return 0;
    memcpy(out, frame.data, frame.length);
    return frame.length;
}
