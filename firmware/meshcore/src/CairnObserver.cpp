#include "CairnObserver.h"
#include "UptimeClock.h"
#include "graphics/CairnClock.h"
#include "mesh/Throttle.h"
#include <ArduinoJson.h>
#include <Packet.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <atomic>
#include <cctype>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sys/time.h>

namespace CairnObserver
{
namespace
{
constexpr const char *HOST = "mqtt.msmesh.org";
struct Settings {
    uint8_t enabled = 0;
    char region[4] = {};
} settings;
struct Reception {
    uint32_t generation;
    timeval timestamp;
    uint8_t length;
    float snr;
    int16_t rssi;
    uint8_t raw[255];
};
enum State { Off, Starting, NeedRegion, Wifi, Clock, Connecting, Online, Retry, Unavailable };
std::atomic<unsigned> state{Off}, uploaded{0}, dropped{0}, received{0};
std::atomic<int> lastError{0};
std::atomic<unsigned> stackRemaining{0};
std::atomic<uint32_t> generation{1};
std::atomic<bool> accepting{false};
portMUX_TYPE settingsLock = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t queue = nullptr;
StaticQueue_t queueState;
uint8_t *queueStorage = nullptr;
char *publishBuffer = nullptr;
char origin[64], publicKey[65], radioName[96];
bool loaded = false;
StaticTask_t workerState;
StackType_t *workerStack = nullptr;
class JsonMemory : public ArduinoJson::Allocator
{
  public:
    void *allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *pointer) override { heap_caps_free(pointer); }
    void *reallocate(void *pointer, size_t size) override
    {
        return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
} jsonMemory;

void hex(const uint8_t *bytes, size_t length, char *out)
{
    constexpr char DIGITS[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        out[2 * i] = DIGITS[bytes[i] >> 4];
        out[2 * i + 1] = DIGITS[bytes[i] & 15];
    }
    out[2 * length] = 0;
}
bool validRegion(const char *value)
{
    return value && strlen(value) == 3 && isalpha(static_cast<unsigned char>(value[0])) &&
           isalpha(static_cast<unsigned char>(value[1])) && isalpha(static_cast<unsigned char>(value[2]));
}
void load()
{
    if (loaded)
        return;
    loaded = true;
    Preferences prefs;
    if (prefs.begin("cairn_observer", true)) {
        Settings value;
        if (prefs.getBytesLength("config") == sizeof(value) && prefs.getBytes("config", &value, sizeof(value)) == sizeof(value)) {
            value.region[3] = 0;
            if (value.enabled <= 1 && (!value.enabled || validRegion(value.region)))
                settings = value;
        }
        prefs.end();
    }
}
void stamp(JsonDocument &doc, const timeval &value)
{
    tm utc{};
    gmtime_r(&value.tv_sec, &utc);
    char time[16], date[16], timestamp[40];
    strftime(time, sizeof(time), "%H:%M:%S", &utc);
    strftime(date, sizeof(date), "%d/%m/%Y", &utc);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &utc);
    snprintf(timestamp + 19, sizeof(timestamp) - 19, ".%06ld+00:00", long(value.tv_usec));
    doc["timestamp"] = timestamp;
    doc["time"] = time;
    doc["date"] = date;
    doc["origin"] = origin;
    doc["origin_id"] = publicKey;
}
bool publish(PubSubClient &mqtt, const char *topic, JsonDocument &doc, bool retain = false)
{
    const size_t size = measureJson(doc);
    if (doc.overflowed() || size >= 1800)
        return false;
    serializeJson(doc, publishBuffer, 1800);
    return mqtt.publish(topic, publishBuffer, retain);
}
bool publishStatus(PubSubClient &mqtt, const char *topic)
{
    JsonDocument doc(&jsonMemory);
    timeval now;
    gettimeofday(&now, nullptr);
    stamp(doc, now);
    doc["status"] = "online";
    doc["model"] = "SenseCAP Indicator";
    doc["firmware_version"] = "CAIRN dual 0.1.2-preview";
    doc["client_version"] = "cairn-observer/1";
    doc["radio"] = radioName;
    doc["repeat"] = "off";
    doc["stats"]["uptime_secs"] = Time::getUptimeSecs();
    doc["stats"]["packets_received"] = received.load();
    doc["stats"]["queue_len"] = uxQueueMessagesWaiting(queue);
    doc["stats"]["errors"] = dropped.load();
    return publish(mqtt, topic, doc, true);
}
void offlineMessage(char *out, unsigned capacity)
{
    JsonDocument doc(&jsonMemory);
    doc["status"] = "offline";
    doc["origin"] = origin;
    doc["origin_id"] = publicKey;
    serializeJson(doc, out, capacity);
}
bool publishReception(PubSubClient &mqtt, const char *topic, const Reception &rx)
{
    // Validate bounds before the pinned MeshCore parser copies path bytes.
    const unsigned pathOffset = (rx.raw[0] & 3) == 0 || (rx.raw[0] & 3) == 3 ? 5 : 1;
    if (rx.length <= pathOffset || !mesh::Packet::isValidPathLen(rx.raw[pathOffset]))
        return false;
    const unsigned pathBytes = (rx.raw[pathOffset] & 63) * ((rx.raw[pathOffset] >> 6) + 1);
    if (pathOffset + 1 + pathBytes >= rx.length)
        return false;
    mesh::Packet packet;
    if (!packet.readFrom(rx.raw, rx.length))
        return false;
    char raw[511], hash[2 * MAX_HASH_SIZE + 1];
    uint8_t digest[MAX_HASH_SIZE];
    packet.calculatePacketHash(digest);
    hex(digest, sizeof(digest), hash);
    hex(rx.raw, rx.length, raw);
    JsonDocument doc(&jsonMemory);
    stamp(doc, rx.timestamp);
    doc["type"] = "PACKET";
    doc["direction"] = "rx";
    doc["raw"] = raw;
    doc["hash"] = hash;
    doc["len"] = String(rx.length);
    doc["packet_type"] = String(packet.getPayloadType());
    doc["route"] = packet.isRouteDirect() ? "D" : "F";
    doc["payload_len"] = String(packet.payload_len);
    doc["SNR"] = String(rx.snr, 1);
    doc["RSSI"] = String(rx.rssi);
    if (packet.isRouteDirect() && packet.getPathHashCount()) {
        auto path = doc["path"].to<JsonArray>();
        for (unsigned i = 0; i < packet.getPathHashCount(); ++i) {
            char hop[7];
            hex(packet.path + i * packet.getPathHashSize(), packet.getPathHashSize(), hop);
            for (char *p = hop; *p; ++p)
                *p = tolower(static_cast<unsigned char>(*p));
            path.add(hop);
        }
    }
    return publish(mqtt, topic, doc);
}
void worker(void *)
{
    WiFiClient transport;
    transport.setConnectionTimeout(1000);
    transport.setTimeout(1000);
    PubSubClient mqtt(transport);
    mqtt.setServer(HOST, 1883);
    mqtt.setSocketTimeout(2);
    mqtt.setKeepAlive(30);
    if (!mqtt.setBufferSize(2048)) {
        state = Unavailable;
        vTaskDelete(nullptr);
        return;
    }
    uint32_t version = 0, lastAttempt = 0, lastStatus = 0;
    bool attempted = false;
    Settings current;
    char packetsTopic[110], statusTopic[110], clientId[90], will[640];
    for (;;) {
        stackRemaining = uxTaskGetStackHighWaterMark(nullptr);
        if (version != generation.load()) {
            if (mqtt.connected()) {
                offlineMessage(will, sizeof(will));
                mqtt.publish(statusTopic, will, true);
                mqtt.disconnect();
            }
            portENTER_CRITICAL(&settingsLock);
            current = settings;
            version = generation.load();
            portEXIT_CRITICAL(&settingsLock);
            snprintf(packetsTopic, sizeof(packetsTopic), "meshcore/%s/%s/packets", current.region, publicKey);
            snprintf(statusTopic, sizeof(statusTopic), "meshcore/%s/%s/status", current.region, publicKey);
            snprintf(clientId, sizeof(clientId), "cairn-%s", publicKey);
            attempted = false;
        }
        accepting = false;
        if (!current.enabled)
            state = Off;
        else if (!validRegion(current.region))
            state = NeedRegion;
        else if (WiFi.status() != WL_CONNECTED) {
            state = Wifi;
            transport.stop();
        } else if (!CairnClock::timeSynchronized())
            state = Clock;
        else {
            accepting = true;
            if (!mqtt.connected() && (!attempted || Throttle::hasElapsed(lastAttempt, 15000))) {
                state = Connecting;
                lastAttempt = Time::getMillis();
                attempted = true;
                offlineMessage(will, sizeof(will));
                if (mqtt.connect(clientId, nullptr, nullptr, statusTopic, 0, true, will)) {
                    lastError = 0;
                    publishStatus(mqtt, statusTopic);
                    lastStatus = Time::getMillis();
                } else
                    lastError = mqtt.state();
            }
            if (mqtt.connected()) {
                state = Online;
                mqtt.loop();
                Reception rx;
                if (xQueuePeek(queue, &rx, 0) == pdTRUE) {
                    if (rx.generation != version)
                        xQueueReceive(queue, &rx, 0);
                    else if (publishReception(mqtt, packetsTopic, rx)) {
                        xQueueReceive(queue, &rx, 0);
                        ++uploaded;
                    } else {
                        xQueueReceive(queue, &rx, 0);
                        ++dropped;
                    }
                }
                if (Throttle::hasElapsed(lastStatus, 60000)) {
                    publishStatus(mqtt, statusTopic);
                    lastStatus = Time::getMillis();
                }
            } else
                state = Retry;
        }
        if (!accepting.load()) {
            Reception discarded;
            while (xQueueReceive(queue, &discarded, 0) == pdTRUE)
                ++dropped;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
} // namespace
bool begin(const uint8_t *key, const char *name, const char *radio)
{
    if (queue)
        return true;
    load();
    hex(key, 32, publicKey);
    strlcpy(origin, name, sizeof(origin));
    strlcpy(radioName, radio, sizeof(radioName));
    queueStorage = static_cast<uint8_t *>(heap_caps_malloc(16 * sizeof(Reception), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    publishBuffer = static_cast<char *>(heap_caps_malloc(1800, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (queueStorage)
        queue = xQueueCreateStatic(16, sizeof(Reception), queueStorage, &queueState);
    state = Starting;
    workerStack = static_cast<StackType_t *>(heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!queue || !publishBuffer || !workerStack ||
        !xTaskCreateStatic(worker, "cairn-observer", 16384, nullptr, 1, workerStack, &workerState)) {
        if (queue)
            vQueueDelete(queue);
        queue = nullptr;
        heap_caps_free(workerStack);
        workerStack = nullptr;
        heap_caps_free(queueStorage);
        queueStorage = nullptr;
        heap_caps_free(publishBuffer);
        publishBuffer = nullptr;
        state = Unavailable;
        return false;
    }
    return true;
}
bool save(bool on, const char *code)
{
    load();
    if ((on || (code && *code)) && !validRegion(code))
        return false;
    Settings value;
    value.enabled = on;
    for (unsigned i = 0; code && i < 3 && code[i]; ++i)
        value.region[i] = toupper(static_cast<unsigned char>(code[i]));
    Preferences prefs;
    if (!prefs.begin("cairn_observer", false))
        return false;
    const bool ok = prefs.putBytes("config", &value, sizeof(value)) == sizeof(value);
    prefs.end();
    if (ok) {
        accepting = false;
        portENTER_CRITICAL(&settingsLock);
        settings = value;
        ++generation;
        portEXIT_CRITICAL(&settingsLock);
    }
    return ok;
}
bool enabled()
{
    load();
    return settings.enabled;
}
const char *region()
{
    load();
    return settings.region;
}
void capture(float snr, float rssi, const uint8_t *raw, size_t length)
{
    if (!queue || !accepting.load() || !raw || length < 2 || length > 255)
        return;
    Reception rx{};
    rx.generation = generation.load();
    gettimeofday(&rx.timestamp, nullptr);
    rx.length = length;
    rx.snr = snr;
    rx.rssi = rssi;
    memcpy(rx.raw, raw, rx.length);
    ++received;
    if (xQueueSend(queue, &rx, 0) != pdTRUE)
        ++dropped;
}
void status(char *out, unsigned capacity)
{
    const char *labels[] = {"Observer off",
                            "Starting observer",
                            "Choose a three-letter region",
                            "Waiting for Wi-Fi",
                            "Waiting for clock sync",
                            "Connecting to Mississippi Mesh",
                            "Connected to Mississippi Mesh",
                            "Connection failed; retrying",
                            "Observer could not start"};
    snprintf(out, capacity, "%s\nUploaded: %u   Queued: %u\nDropped: %u   Connection code: %d", labels[state.load()],
             uploaded.load(), queue ? unsigned(uxQueueMessagesWaiting(queue)) : 0, dropped.load(), lastError.load());
}
unsigned diagnostics(char *out, unsigned capacity)
{
    return snprintf(out, capacity,
                    "{\"enabled\":%s,\"region\":\"%s\",\"state\":%u,\"uploaded\":%u,\"received\":%u,"
                    "\"dropped\":%u,\"queued\":%u,\"error\":%d,\"stackFree\":%u}",
                    enabled() ? "true" : "false", region(), state.load(), uploaded.load(), received.load(), dropped.load(),
                    queue ? unsigned(uxQueueMessagesWaiting(queue)) : 0, lastError.load(), stackRemaining.load());
}
} // namespace CairnObserver
