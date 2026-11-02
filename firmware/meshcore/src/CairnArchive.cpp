#include "CairnArchive.h"
#include "CairnJournal.h"
#include "IndicatorFiles.h"
#include "UptimeClock.h"
#include "graphics/CairnClock.h"
#include "mesh/Throttle.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

namespace CairnArchive
{
namespace
{
CairnMessage *records = nullptr;
char *buffer = nullptr;
unsigned total = 0, position = 0;
uint32_t offset = 0, lastWrite = 0;
bool running = false, headerWritten = false;
char filename[80]{}, message[160] = "Export message history to the SD card as JSON Lines.";
void finish(bool success)
{
    running = false;
    heap_caps_free(records);
    heap_caps_free(buffer);
    records = nullptr;
    buffer = nullptr;
    if (success)
        snprintf(message, sizeof(message), "Exported %u messages\n%s", total, filename);
    else
        snprintf(message, sizeof(message), "Export incomplete\n%s", filename);
}
} // namespace
bool start()
{
    if (running)
        return false;
    meshtastic_SdCardInfo card{};
    if (!indicatorFiles.info(card) || !card.present || card.busy) {
        strlcpy(message, "Insert an available FAT32 SD card.", sizeof(message));
        return false;
    }
    total = messageJournal.count();
    records = static_cast<CairnMessage *>(heap_caps_malloc((total ? total : 1) * sizeof(CairnMessage), MALLOC_CAP_SPIRAM));
    buffer = static_cast<char *>(heap_caps_malloc(2048, MALLOC_CAP_SPIRAM));
    if (!records || !buffer) {
        heap_caps_free(records);
        heap_caps_free(buffer);
        records = nullptr;
        buffer = nullptr;
        strlcpy(message, "Not enough memory for an export snapshot.", sizeof(message));
        return false;
    }
    for (unsigned i = 0; i < total; ++i)
        records[i] = *messageJournal.at(total - 1 - i);
    snprintf(filename, sizeof(filename), "/messages-%08lx-%08lx.jsonl", (unsigned long)time(nullptr),
             (unsigned long)esp_random());
    position = offset = 0;
    headerWritten = false;
    running = true;
    lastWrite = Time::getMillis();
    strlcpy(message, "Exporting message snapshot...", sizeof(message));
    return true;
}
void tick()
{
    if (!running || !Throttle::hasElapsed(lastWrite, 20))
        return;
    lastWrite = Time::getMillis();
    JsonDocument json;
    if (!headerWritten) {
        json["type"] = "cairn-message-export";
        json["version"] = 1;
        json["count"] = total;
        json["clockSynchronized"] = CairnClock::timeSynchronized();
    } else if (position < total) {
        const auto &record = records[position];
        json["id"] = record.id;
        json["timestamp"] = record.timestamp;
        json["name"] = record.name;
        json["text"] = record.text;
        json["channel"] = record.channel;
        json["outgoing"] = record.direction != 0;
        json["status"] = record.status;
        json["checksum"] = record.checksum;
        char peer[65];
        const char *hex = "0123456789abcdef";
        for (unsigned i = 0; i < 32; ++i) {
            peer[i * 2] = hex[record.peer[i] >> 4];
            peer[i * 2 + 1] = hex[record.peer[i] & 15];
        }
        peer[64] = 0;
        json["peer"] = peer;
    } else {
        finish(true);
        return;
    }
    if (json.overflowed()) {
        finish(false);
        return;
    }
    const size_t length = serializeJson(json, buffer, 2046);
    if (!length || length >= 2046) {
        finish(false);
        return;
    }
    buffer[length] = '\n';
    if (!indicatorFiles.write(filename, offset, reinterpret_cast<const uint8_t *>(buffer), length + 1, offset == 0)) {
        finish(false);
        return;
    }
    offset += length + 1;
    if (headerWritten)
        ++position;
    headerWritten = true;
    snprintf(message, sizeof(message), "Exporting %u / %u\n%s", position, total, filename);
}
bool active()
{
    return running;
}
const char *status()
{
    return message;
}
const char *path()
{
    return filename;
}
} // namespace CairnArchive
