#pragma once

#include <Arduino.h>

struct CairnMessage {
    uint32_t magic, sequence, id, timestamp, ack;
    uint8_t peer[32];
    uint8_t channel, direction, status, reserved;
    char name[32];
    char text[161];
    uint8_t padding[3];
    uint32_t checksum;
};
static_assert(sizeof(CairnMessage) == 256, "Journal record layout changed");

class CairnJournal
{
  public:
    enum Status : uint8_t { Received, Queued, Sent, Delivered, Failed };
    CairnJournal() = default;
    ~CairnJournal();
    CairnJournal(const CairnJournal &) = delete;
    CairnJournal &operator=(const CairnJournal &) = delete;
    bool begin();
    uint32_t add(const char *name, const char *text, uint32_t timestamp, const uint8_t *peer, uint8_t channel, bool outgoing);
    bool update(uint32_t id, Status status, uint32_t ack = 0);
    void acknowledge(uint32_t ack);
    size_t count() const { return used; }
    const CairnMessage *at(size_t newestIndex) const;
    bool healthy() const { return available && writeHealthy; }
    uint32_t revision() const { return sequence; }

  private:
    bool append(CairnMessage &message);
    void remember(const CairnMessage &message);
    CairnMessage *messages = nullptr;
    size_t used = 0;
    uint32_t sequence = 0;
    bool available = false, writeHealthy = true;
};

extern CairnJournal messageJournal;
