#include "CairnMap.h"
#include "IndicatorFiles.h"
#include "graphics/CairnClock.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

extern const uint8_t mapCertificatesStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t mapCertificatesEnd[] asm("_binary_x509_crt_bundle_end");

namespace CairnMap
{
namespace
{
constexpr size_t LIMIT = 128 * 1024;
constexpr double MAP_PI = 3.14159265358979323846;
struct Settings {
    uint32_t magic = 0x434D5031;
    char url[192] = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    char credit[96] = "© OpenStreetMap contributors";
} settings;
struct Job {
    uint32_t generation;
    unsigned index;
    char url[224], path[80];
};
struct Result {
    uint32_t generation;
    unsigned index;
    uint8_t *data;
    size_t size;
};
QueueHandle_t jobs = nullptr, results = nullptr;
std::atomic<uint32_t> generation{1};
uint8_t *tileData[TILES]{};
size_t tileSize[TILES]{};
unsigned finished = 0, loaded = 0;
bool initialized = false;
char message[128] = "Viewed tiles are cached on the SD card.";

uint32_t readBig(const uint8_t *bytes)
{
    return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
}
bool validPNG(const uint8_t *bytes, size_t length)
{
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    return bytes && length >= 24 && !memcmp(bytes, signature, 8) && readBig(bytes + 16) == 256 && readBig(bytes + 20) == 256;
}
class Download : public Stream
{
  public:
    uint8_t *bytes;
    size_t length = 0;
    uint32_t expected;
    Download(uint8_t *buffer, uint32_t value) : bytes(buffer), expected(value) {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t *value, size_t count) override
    {
        if (generation.load() != expected || count > LIMIT - length)
            return 0;
        memcpy(bytes + length, value, count);
        length += count;
        return count;
    }
};
void worker(void *)
{
    Job job;
    for (;;) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE || job.generation != generation.load())
            continue;
        uint8_t *bytes = nullptr;
        size_t length = 0;
        uint8_t *chunk = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM));
        uint32_t got = 0, total = 0;
        if (chunk && indicatorFiles.read(job.path, 0, chunk, 4096, got, total) && total >= 24 && total <= LIMIT) {
            bytes = static_cast<uint8_t *>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM));
            while (bytes && got && length + got <= total) {
                memcpy(bytes + length, chunk, got);
                length += got;
                if (length == total || job.generation != generation.load())
                    break;
                if (!indicatorFiles.read(job.path, length, chunk, std::min<uint32_t>(4096, total - length), got, total))
                    break;
                vTaskDelay(1);
            }
            if (length != total || !validPNG(bytes, length)) {
                heap_caps_free(bytes);
                bytes = nullptr;
                length = 0;
            }
        }
        heap_caps_free(chunk);
        if (!bytes && job.generation == generation.load() && WiFi.status() == WL_CONNECTED && CairnClock::timeSynchronized()) {
            auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(LIMIT, MALLOC_CAP_SPIRAM));
            if (buffer) {
                NetworkClientSecure secure;
                NetworkClient plain;
                secure.setCACertBundle(mapCertificatesStart, mapCertificatesEnd - mapCertificatesStart);
                HTTPClient http;
                NetworkClient &client = strncmp(job.url, "https://", 8) == 0 ? static_cast<NetworkClient &>(secure) : plain;
                http.setConnectTimeout(8000);
                http.setTimeout(8000);
                http.setUserAgent("SenseCAP-HamView-Mesh/1.0 (+https://github.com/RF-YVY/HamView_SenseCAP)");
                if (http.begin(client, job.url) && http.GET() == HTTP_CODE_OK && http.getSize() <= int(LIMIT)) {
                    Download output(buffer, job.generation);
                    const int received = http.writeToStream(&output);
                    if (received > 0 && validPNG(buffer, output.length)) {
                        bytes = buffer;
                        length = output.length;
                        buffer = nullptr;
                        for (size_t offset = 0; offset < length && job.generation == generation.load(); offset += 4096) {
                            const size_t count = std::min<size_t>(4096, length - offset);
                            if (!indicatorFiles.write(job.path, offset, bytes + offset, count, offset == 0))
                                break;
                            vTaskDelay(1);
                        }
                    }
                }
                http.end();
                heap_caps_free(buffer);
            }
        }
        Result result{job.generation, job.index, bytes, length};
        if (xQueueSend(results, &result, 0) != pdTRUE)
            heap_caps_free(bytes);
    }
}
} // namespace
bool begin()
{
    if (initialized)
        return true;
    Preferences prefs;
    Settings value;
    if (prefs.begin("cairn_map", true)) {
        if (prefs.getBytesLength("provider") == sizeof(value) &&
            prefs.getBytes("provider", &value, sizeof(value)) == sizeof(value) && value.magic == settings.magic) {
            value.url[191] = value.credit[95] = 0;
            settings = value;
        }
        prefs.end();
    }
    if (!jobs)
        jobs = xQueueCreate(8, sizeof(Job));
    if (!results)
        results = xQueueCreate(16, sizeof(Result));
    if (!jobs || !results)
        return false;
    initialized = xTaskCreatePinnedToCore(worker, "cairn-map", 8192, nullptr, 1, nullptr, 0) == pdPASS;
    return initialized;
}
void close()
{
    ++generation;
    if (jobs)
        xQueueReset(jobs);
    for (unsigned i = 0; i < TILES; ++i) {
        heap_caps_free(tileData[i]);
        tileData[i] = nullptr;
        tileSize[i] = 0;
    }
    finished = loaded = 0;
}
void request(unsigned index, unsigned zoom, int x, int y)
{
    if (!initialized || index >= TILES || zoom > 18)
        return;
    const int count = 1 << zoom;
    x = (x % count + count) % count;
    y = std::max(0, std::min(count - 1, y));
    String url(settings.url);
    url.replace("{z}", String(zoom));
    url.replace("{x}", String(x));
    url.replace("{y}", String(y));
    uint32_t hash = 2166136261UL;
    for (const char *p = settings.url; *p; ++p)
        hash = (hash ^ uint8_t(*p)) * 16777619UL;
    Job job{};
    job.generation = generation.load();
    job.index = index;
    strlcpy(job.url, url.c_str(), sizeof(job.url));
    snprintf(job.path, sizeof(job.path), "/.cairn-map-%08lx-%u-%d-%d.png", (unsigned long)hash, zoom, x, y);
    xQueueSend(jobs, &job, 0);
    strlcpy(message, "Loading viewed tiles; cached tiles work without Wi-Fi.", sizeof(message));
}
bool poll(unsigned &index)
{
    if (!results)
        return false;
    Result result;
    while (xQueueReceive(results, &result, 0) == pdTRUE) {
        if (result.generation != generation.load() || result.index >= TILES) {
            heap_caps_free(result.data);
            continue;
        }
        index = result.index;
        heap_caps_free(tileData[index]);
        tileData[index] = result.data;
        tileSize[index] = result.size;
        ++finished;
        if (result.data)
            ++loaded;
        snprintf(message, sizeof(message), "Tiles %u / %u%s", loaded, TILES,
                 finished == TILES && loaded < TILES ? " — connect Wi-Fi / check map source" : "");
        return true;
    }
    return false;
}
const uint8_t *data(unsigned index)
{
    return index < TILES ? tileData[index] : nullptr;
}
size_t size(unsigned index)
{
    return index < TILES ? tileSize[index] : 0;
}
const char *status()
{
    return message;
}
const char *provider()
{
    return settings.url;
}
const char *attribution()
{
    return settings.credit;
}
bool saveProvider(const char *url, const char *credit)
{
    const String text(url);
    if ((!text.startsWith("https://") && !text.startsWith("http://")) || text.indexOf("{z}") < 0 || text.indexOf("{x}") < 0 ||
        text.indexOf("{y}") < 0 || strlen(url) >= sizeof(settings.url) || !credit[0] || strlen(credit) >= sizeof(settings.credit))
        return false;
    for (const char *p = url; *p; ++p)
        if (uint8_t(*p) <= 32)
            return false;
    Settings value;
    strlcpy(value.url, url, sizeof(value.url));
    strlcpy(value.credit, credit, sizeof(value.credit));
    Preferences prefs;
    if (!prefs.begin("cairn_map", false))
        return false;
    const bool saved = prefs.putBytes("provider", &value, sizeof(value)) == sizeof(value);
    prefs.end();
    if (saved)
        settings = value;
    return saved;
}
void project(double latitude, double longitude, unsigned zoom, double &x, double &y)
{
    latitude = std::max(-85.05112878, std::min(85.05112878, latitude));
    const double scale = 256.0 * (1 << zoom);
    const double rad = latitude * MAP_PI / 180;
    x = (longitude + 180) / 360 * scale;
    y = (1 - std::log(std::tan(rad) + 1 / std::cos(rad)) / MAP_PI) / 2 * scale;
}
void unproject(double x, double y, unsigned zoom, double &latitude, double &longitude)
{
    const double scale = 256.0 * (1 << zoom);
    longitude = std::fmod(x / scale * 360 + 540, 360) - 180;
    latitude = std::atan(std::sinh(MAP_PI * (1 - 2 * y / scale))) * 180 / MAP_PI;
    latitude = std::max(-85.05112878, std::min(85.05112878, latitude));
}
} // namespace CairnMap
