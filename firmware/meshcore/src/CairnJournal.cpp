#include "CairnJournal.h"
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <stddef.h>

namespace
{
constexpr uint32_t MAGIC = 0x434A5231;
constexpr unsigned CAPACITY = 128;
constexpr const char *PATH = "/cairn-messages.v1";
uint32_t checksum(const CairnMessage &message)
{
    uint32_t crc = UINT32_MAX;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&message);
    for (size_t i = 0; i < offsetof(CairnMessage, checksum); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}
} // namespace

CairnJournal messageJournal;

CairnJournal::~CairnJournal()
{
    heap_caps_free(messages);
}

bool CairnJournal::begin()
{
    if (available)
        return true;
    messages = static_cast<CairnMessage *>(heap_caps_calloc(CAPACITY, sizeof(CairnMessage), MALLOC_CAP_SPIRAM));
    if (!messages)
        return false;
    auto file = SPIFFS.open(PATH, FILE_READ);
    CairnMessage record;
    auto *records = static_cast<CairnMessage *>(heap_caps_malloc(CAPACITY * sizeof(CairnMessage), MALLOC_CAP_SPIRAM));
    if (!records) {
        heap_caps_free(messages);
        messages = nullptr;
        return false;
    }
    size_t count = 0;
    while (file && count < CAPACITY && file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
        if (record.magic == MAGIC && record.checksum == checksum(record) && record.name[31] == 0 && record.text[160] == 0) {
            records[count++] = record;
            sequence = max(sequence, record.sequence);
        }
    }
    file.close();
    for (size_t i = 0; i < count; ++i)
        for (size_t j = i + 1; j < count; ++j)
            if (records[j].sequence < records[i].sequence) {
                auto temp = records[i];
                records[i] = records[j];
                records[j] = temp;
            }
    for (size_t i = 0; i < count; ++i)
        remember(records[i]);
    heap_caps_free(records);
    available = true;
    return true;
}

void CairnJournal::remember(const CairnMessage &record)
{
    for (size_t i = 0; i < used; ++i)
        if (messages[i].id == record.id) {
            messages[i] = record;
            return;
        }
    size_t position = 0;
    while (position < used && messages[position].id > record.id)
        ++position;
    if (position == CAPACITY)
        return;
    const size_t end = used < CAPACITY ? used++ : CAPACITY - 1;
    memmove(messages + position + 1, messages + position, (end - position) * sizeof(CairnMessage));
    messages[position] = record;
}

bool CairnJournal::append(CairnMessage &record)
{
    if (!available)
        return false;
    record.magic = MAGIC;
    record.sequence = ++sequence;
    if (!record.id)
        record.id = sequence;
    record.checksum = checksum(record);
    auto file = SPIFFS.open(PATH, SPIFFS.exists(PATH) ? "r+" : "w+");
    const uint32_t offset = ((record.sequence - 1) % CAPACITY) * sizeof(record);
    const uint8_t empty[64] = {};
    while (file && file.size() < offset) {
        const size_t count = min(size_t(offset - file.size()), sizeof(empty));
        if (!file.seek(file.size()) || file.write(empty, count) != count)
            break;
    }
    writeHealthy =
        file && file.seek(offset) && file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
    if (file) {
        file.flush();
        file.close();
    }
    remember(record);
    return writeHealthy;
}

uint32_t CairnJournal::add(const char *name, const char *text, uint32_t timestamp, const uint8_t *peer, uint8_t channel,
                           bool outgoing)
{
    if (!available)
        return 0;
    CairnMessage record{};
    record.timestamp = timestamp;
    record.channel = channel;
    record.direction = outgoing;
    record.status = outgoing ? Queued : Received;
    if (peer)
        memcpy(record.peer, peer, sizeof(record.peer));
    strlcpy(record.name, name, sizeof(record.name));
    strlcpy(record.text, text, sizeof(record.text));
    append(record);
    return record.id;
}

bool CairnJournal::update(uint32_t id, Status status, uint32_t ack)
{
    for (size_t i = 0; i < used; ++i)
        if (messages[i].id == id) {
            auto record = messages[i];
            record.status = status;
            record.ack = ack;
            return append(record);
        }
    return false;
}

void CairnJournal::acknowledge(uint32_t ack)
{
    if (!ack)
        return;
    for (size_t i = 0; i < used; ++i)
        if (messages[i].direction && messages[i].ack == ack && messages[i].status != Delivered) {
            update(messages[i].id, Delivered, ack);
            return;
        }
}

const CairnMessage *CairnJournal::at(size_t index) const
{
    return messages && index < used ? &messages[index] : nullptr;
}
